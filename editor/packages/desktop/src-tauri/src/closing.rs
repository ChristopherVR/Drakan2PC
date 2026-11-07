//! Closing the editor window: the page gets time to write what is not yet saved (the level autosave and the project
//! folder) before the window goes. The shell asks with the `app-close-requested` event; the page answers with commands:
//!
//! * `close_beat` every half second while it works (`busy`) or while it waits for the user to decide after a failed write
//!   (`ask`), so a long write is waited for and only a silent page is given up on after `SILENCE`;
//! * `close_ack` when everything is written (the window closes);
//! * `close_cancel` when the user chose to keep the window open (the window stays, a later close starts over).
//!
//! Further close requests while one is in progress are swallowed (the window stays open, nothing stacks).

use std::sync::atomic::{AtomicBool, AtomicU64, AtomicU8, Ordering};
use std::time::{Duration, Instant};

/// A page that sends no beat for this long is hung or still loading: close anyway.
pub const SILENCE: Duration = Duration::from_millis(3000);
/// The longest a flush may take overall, however steady the beat.
pub const OVERALL: Duration = Duration::from_secs(180);

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum Outcome {
    /// The page wrote everything.
    Acked,
    /// The user chose to stay.
    Cancelled,
    /// No beat for `SILENCE`: close.
    Silent,
    /// `OVERALL` passed.
    TimedOut,
}

const IDLE: u8 = 0;
const WAITING: u8 = 1;
const ACKED: u8 = 2;
const CANCELLED: u8 = 3;

/// The state of one close attempt, shared between the window event handler, the waiter thread and the page's commands.
pub struct CloseGate {
    state: AtomicU8,
    /// Milliseconds since `epoch` of the last beat.
    beat: AtomicU64,
    /// Milliseconds since `epoch` when the current stretch of work began (a user decision starts a new one).
    since: AtomicU64,
    /// The waiter finished and the next close request must go through.
    pub passthrough: AtomicBool,
    epoch: Instant,
}

impl CloseGate {
    pub fn new() -> Self {
        CloseGate {
            state: AtomicU8::new(IDLE),
            beat: AtomicU64::new(0),
            since: AtomicU64::new(0),
            passthrough: AtomicBool::new(false),
            epoch: Instant::now(),
        }
    }

    fn now_ms(&self) -> u64 {
        self.epoch.elapsed().as_millis() as u64
    }

    /// A close request arrived. Returns true when it starts a new attempt (the caller emits the event and waits); false
    /// when one is already in progress.
    pub fn begin(&self) -> bool {
        if self.state.compare_exchange(IDLE, WAITING, Ordering::SeqCst, Ordering::SeqCst).is_ok() {
            self.beat.store(self.now_ms(), Ordering::SeqCst);
            self.since.store(self.now_ms(), Ordering::SeqCst);
            true
        } else {
            false
        }
    }

    pub fn beat(&self) {
        self.beat.store(self.now_ms(), Ordering::SeqCst);
    }

    /// The page shows a question to the user: wait for the answer, however long, and give the next flush a fresh limit.
    pub fn ask_beat(&self) {
        self.beat();
        self.since.store(self.now_ms(), Ordering::SeqCst);
    }

    pub fn ack(&self) {
        let _ = self.state.compare_exchange(WAITING, ACKED, Ordering::SeqCst, Ordering::SeqCst);
    }

    pub fn cancel(&self) {
        let _ = self.state.compare_exchange(WAITING, CANCELLED, Ordering::SeqCst, Ordering::SeqCst);
    }

    /// Block until the attempt ends, then make the gate ready for the next one. `Cancelled` is the only outcome that keeps
    /// the window; for every other the caller closes it (`passthrough` is set so that close is not intercepted).
    pub fn wait(&self, silence: Duration, overall: Duration) -> Outcome {
        let outcome = loop {
            match self.state.load(Ordering::SeqCst) {
                ACKED => break Outcome::Acked,
                CANCELLED => break Outcome::Cancelled,
                _ => {}
            }
            if Duration::from_millis(self.now_ms().saturating_sub(self.since.load(Ordering::SeqCst))) >= overall {
                break Outcome::TimedOut;
            }
            let quiet = self.now_ms().saturating_sub(self.beat.load(Ordering::SeqCst));
            if Duration::from_millis(quiet) >= silence {
                break Outcome::Silent;
            }
            std::thread::sleep(Duration::from_millis(20));
        };
        if outcome != Outcome::Cancelled {
            self.passthrough.store(true, Ordering::SeqCst);
        }
        self.state.store(IDLE, Ordering::SeqCst);
        outcome
    }
}

impl Default for CloseGate {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;

    fn ms(n: u64) -> Duration {
        Duration::from_millis(n)
    }

    #[test]
    fn closes_as_soon_as_the_page_acks() {
        let g = Arc::new(CloseGate::new());
        assert!(g.begin());
        let a = g.clone();
        std::thread::spawn(move || {
            std::thread::sleep(ms(60));
            a.ack();
        });
        let t = Instant::now();
        assert_eq!(g.wait(SILENCE, Duration::from_secs(5)), Outcome::Acked);
        assert!(t.elapsed() < Duration::from_secs(1));
        assert!(g.passthrough.load(Ordering::SeqCst));
    }

    #[test]
    fn a_second_request_during_the_wait_is_swallowed() {
        let g = CloseGate::new();
        assert!(g.begin());
        assert!(!g.begin());
        assert!(!g.begin());
    }

    #[test]
    fn a_steady_beat_keeps_a_slow_flush_alive_past_the_silence_limit() {
        let g = Arc::new(CloseGate::new());
        assert!(g.begin());
        let a = g.clone();
        std::thread::spawn(move || {
            for _ in 0..12 {
                std::thread::sleep(ms(40));
                a.beat();
            }
            a.ack();
        });
        // 480 ms of work against a 150 ms silence limit.
        assert_eq!(g.wait(ms(150), Duration::from_secs(5)), Outcome::Acked);
    }

    #[test]
    fn a_page_that_never_answers_is_given_up_on() {
        let g = CloseGate::new();
        assert!(g.begin());
        assert_eq!(g.wait(ms(80), Duration::from_secs(5)), Outcome::Silent);
    }

    #[test]
    fn the_overall_limit_ends_a_beat_that_never_stops() {
        let g = Arc::new(CloseGate::new());
        assert!(g.begin());
        let a = g.clone();
        let stop = Arc::new(AtomicBool::new(false));
        let s = stop.clone();
        std::thread::spawn(move || {
            while !s.load(Ordering::SeqCst) {
                a.beat();
                std::thread::sleep(ms(10));
            }
        });
        assert_eq!(g.wait(ms(200), ms(150)), Outcome::TimedOut);
        stop.store(true, Ordering::SeqCst);
    }

    #[test]
    fn cancel_keeps_the_window_and_a_later_close_starts_over() {
        let g = Arc::new(CloseGate::new());
        assert!(g.begin());
        let a = g.clone();
        std::thread::spawn(move || {
            std::thread::sleep(ms(30));
            a.cancel();
        });
        assert_eq!(g.wait(SILENCE, Duration::from_secs(5)), Outcome::Cancelled);
        assert!(!g.passthrough.load(Ordering::SeqCst));
        assert!(g.begin());
    }

    #[test]
    fn a_question_to_the_user_restarts_the_overall_limit() {
        let g = Arc::new(CloseGate::new());
        assert!(g.begin());
        let a = g.clone();
        std::thread::spawn(move || {
            for _ in 0..20 {
                std::thread::sleep(ms(20));
                a.ask_beat();
            }
            a.ack();
        });
        // 400 ms of waiting for the user against a 150 ms overall limit.
        assert_eq!(g.wait(ms(200), ms(150)), Outcome::Acked);
    }

    #[test]
    fn an_ack_that_arrives_with_no_attempt_running_does_nothing() {
        let g = CloseGate::new();
        g.ack();
        assert!(g.begin());
        g.beat();
        g.cancel();
        assert_eq!(g.wait(SILENCE, Duration::from_secs(1)), Outcome::Cancelled);
    }
}

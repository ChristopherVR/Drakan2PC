//! Closing the editor window: the page gets a moment to write what is not yet saved (the level autosave and the project
//! folder) before the window goes. The shell asks with the `app-close-requested` event and the page answers with the
//! `close_ack` command; a page that does not answer (still loading, hung) is not waited for longer than `ACK_WAIT`.

use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

pub const ACK_WAIT: Duration = Duration::from_millis(3000);

/// Wait until `ack` is set or `max` has passed. Returns whether the page answered.
pub fn wait_for_ack(ack: &AtomicBool, max: Duration) -> bool {
    let end = Instant::now() + max;
    while Instant::now() < end {
        if ack.load(Ordering::SeqCst) {
            return true;
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    ack.load(Ordering::SeqCst)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;

    #[test]
    fn returns_as_soon_as_the_page_answers() {
        let ack = Arc::new(AtomicBool::new(false));
        let a = ack.clone();
        std::thread::spawn(move || {
            std::thread::sleep(Duration::from_millis(60));
            a.store(true, Ordering::SeqCst);
        });
        let t = Instant::now();
        assert!(wait_for_ack(&ack, Duration::from_secs(2)));
        assert!(t.elapsed() < Duration::from_secs(1));
    }

    #[test]
    fn gives_up_when_the_page_never_answers() {
        let ack = AtomicBool::new(false);
        assert!(!wait_for_ack(&ack, Duration::from_millis(80)));
    }
}

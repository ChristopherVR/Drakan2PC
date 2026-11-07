//! The pack the editor installs for one Play session (the converted Order of the Flame levels). It lives under its own id
//! (`editor-play-ootf`), never touches a pack the player installed, and is taken out again when the game closes (or at the
//! next start of the editor if the editor itself was closed first). Only the `mods.ini` keys Play changed are put back,
//! as line edits on the file as it is then, so a pack the player toggled in the meantime stays as they left it.

use serde::{Deserialize, Serialize};
use std::fs::File;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

/// What to put back: `mods.ini` before the Play session, the text Play wrote, and the pack file to delete.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Clone)]
pub struct Backup {
    pub mods: String,
    pub id: String,
    /// `mods.ini` before Play; None when there was no file (Play created it, restore removes it again).
    #[serde(default)]
    pub ini: Option<String>,
    /// The text Play wrote. Missing in a backup of an older editor: that restores `ini` wholesale.
    #[serde(default)]
    pub after: Option<String>,
    /// `mods.ini` is already back; only the pack zip could not be deleted yet (still open).
    #[serde(default)]
    pub restored: bool,
}

/// How far a restore got.
#[derive(Debug, PartialEq, Eq)]
pub enum Restored {
    /// There was no backup.
    Nothing,
    /// `mods.ini` is back and the zip is gone.
    Done,
    /// `mods.ini` is back but the zip is locked; the backup stays so the delete can be retried.
    Pending,
}

pub fn backup_path(config_dir: &Path) -> PathBuf {
    config_dir.join("play-pack-backup.json")
}

fn lock_path(config_dir: &Path) -> PathBuf {
    config_dir.join("play-pack.lock")
}

pub fn zip_path(mods: &Path, id: &str) -> PathBuf {
    mods.join(format!("{id}.zip"))
}

/// Write through a temp file and a rename, so a kill in the middle never leaves a truncated file.
pub fn write_atomic(path: &Path, text: &str) -> std::io::Result<()> {
    let mut tmp = path.as_os_str().to_owned();
    tmp.push(format!(".{}.tmp", std::process::id()));
    let tmp = PathBuf::from(tmp);
    std::fs::write(&tmp, text)?;
    std::fs::rename(&tmp, path).inspect_err(|_| {
        let _ = std::fs::remove_file(&tmp);
    })
}

// ---- owner lock --------------------------------------------------------------------------------------------------------
// The editor that started a Play session keeps `play-pack.lock` open without sharing for as long as it runs. A second
// editor cannot open it, so it knows a session is live and must not restore. The OS drops it when the process dies.

static OWNER: Mutex<Option<File>> = Mutex::new(None);

fn open_exclusive(path: &Path) -> std::io::Result<File> {
    let mut o = std::fs::OpenOptions::new();
    o.write(true).create(true).truncate(false);
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        o.share_mode(0);
    }
    o.open(path)
}

/// Take the lock for this process (kept until it exits).
pub fn hold_lock(config_dir: &Path) {
    let mut g = OWNER.lock().unwrap();
    if g.is_none() {
        let _ = std::fs::create_dir_all(config_dir);
        *g = open_exclusive(&lock_path(config_dir)).ok();
    }
}

/// No other running editor owns a Play session.
pub fn lock_is_free(config_dir: &Path) -> bool {
    if OWNER.lock().unwrap().is_some() {
        return true;
    }
    open_exclusive(&lock_path(config_dir)).is_ok()
}

// ---- game session lock -------------------------------------------------------------------------------------------------
// Two editor windows share one play folder, one mods folder and one `mods.ini`. While the game that one of them started is
// running, that window keeps `game-session.lock` open (Windows: without sharing). The other window sees it taken, reports the
// game as running and refuses to Play, so it cannot overwrite the pack the running game reads or have its own pack switched
// off when the first game closes.

static GAME: Mutex<Option<(u64, File)>> = Mutex::new(None);
static GAME_GEN: AtomicU64 = AtomicU64::new(0);

fn game_lock_path(config_dir: &Path) -> PathBuf {
    config_dir.join("game-session.lock")
}

/// Is the game started by another editor window still running?
pub fn game_session_elsewhere(config_dir: &Path) -> bool {
    if GAME.lock().unwrap().is_some() {
        return false;
    }
    match open_exclusive(&game_lock_path(config_dir)) {
        Ok(_) => false,
        Err(e) => is_sharing_violation(&e),
    }
}

/// A file another process holds open without sharing (Windows ERROR_SHARING_VIOLATION / ERROR_LOCK_VIOLATION).
fn is_sharing_violation(e: &std::io::Error) -> bool {
    matches!(e.raw_os_error(), Some(32) | Some(33))
}

/// This window starts the game: take the session lock. `Err` with a sentence when another window holds it.
pub fn begin_game_session(config_dir: &Path) -> Result<u64, String> {
    let mut g = GAME.lock().unwrap();
    *g = None; // a session of this window that already ended
    let _ = std::fs::create_dir_all(config_dir);
    match open_exclusive(&game_lock_path(config_dir)) {
        Ok(f) => {
            let gen = GAME_GEN.fetch_add(1, Ordering::SeqCst) + 1;
            *g = Some((gen, f));
            Ok(gen)
        }
        Err(e) if is_sharing_violation(&e) => {
            Err("The game is already running from another editor window. Close that game first.".to_string())
        }
        Err(_) => Ok(0), // no lock possible (read-only config folder): play on, unprotected
    }
}

/// The game of session `gen` has closed: let the other windows play again.
pub fn end_game_session(gen: u64) {
    let mut g = GAME.lock().unwrap();
    if g.as_ref().is_some_and(|(n, _)| *n == gen) {
        *g = None;
    }
}

// ---- mods.ini line edits -----------------------------------------------------------------------------------------------

/// `(line index, name, value)` of every `name = value` line in the `[mods]` section.
fn mods_entries(text: &str) -> Vec<(usize, String, String)> {
    let mut out = Vec::new();
    let mut in_mods = false;
    for (i, raw) in text.lines().enumerate() {
        let t = raw.trim();
        if t.starts_with('[') && t.ends_with(']') {
            in_mods = t[1..t.len() - 1].trim().eq_ignore_ascii_case("mods");
            continue;
        }
        if !in_mods || t.starts_with(';') || t.starts_with('#') {
            continue;
        }
        if let Some((k, v)) = t.split_once('=') {
            if !k.trim().is_empty() {
                out.push((i, k.trim().to_string(), v.trim().to_string()));
            }
        }
    }
    out
}

fn value_of(text: &str, name: &str) -> Option<String> {
    mods_entries(text).into_iter().find(|(_, k, _)| k == name).map(|(_, _, v)| v)
}

/// Set (`Some`) or remove (`None`) one key in the `[mods]` section, keeping everything else and the line ending style.
fn set_key(text: &str, name: &str, value: Option<&str>) -> String {
    let nl = if text.contains("\r\n") { "\r\n" } else { "\n" };
    let mut lines: Vec<String> = text.lines().map(str::to_string).collect();
    let found = mods_entries(text).into_iter().find(|(_, k, _)| k == name).map(|(i, _, _)| i);
    match (found, value) {
        (Some(i), Some(v)) => lines[i] = format!("{name} = {v}"),
        (Some(i), None) => {
            lines.remove(i);
        }
        (None, Some(v)) => {
            let mut last = None;
            let mut in_mods = false;
            for (i, raw) in lines.iter().enumerate() {
                let t = raw.trim();
                if t.starts_with('[') && t.ends_with(']') {
                    in_mods = t[1..t.len() - 1].trim().eq_ignore_ascii_case("mods");
                    if in_mods {
                        last = Some(i);
                    }
                } else if in_mods && !t.is_empty() {
                    last = Some(i);
                }
            }
            match last {
                Some(i) => lines.insert(i + 1, format!("{name} = {v}")),
                None => {
                    lines.push("[mods]".into());
                    lines.push(format!("{name} = {v}"));
                }
            }
        }
        (None, None) => return text.to_string(),
    }
    if lines.is_empty() {
        String::new()
    } else {
        lines.join(nl) + nl
    }
}

/// `current` with every key that differs between `before` and `after` put back, but only when the key still holds the value
/// Play wrote (a key the player changed since is theirs now).
pub fn revert_keys(current: &str, before: &str, after: &str) -> String {
    let mut names: Vec<String> = Vec::new();
    for (_, k, _) in mods_entries(before).into_iter().chain(mods_entries(after)) {
        if !names.contains(&k) {
            names.push(k);
        }
    }
    let same = |x: &Option<String>, y: &Option<String>| match (x, y) {
        (Some(x), Some(y)) => x.eq_ignore_ascii_case(y),
        (None, None) => true,
        _ => false,
    };
    let mut out = current.to_string();
    for k in names {
        let (b, a) = (value_of(before, &k), value_of(after, &k));
        if same(&b, &a) {
            continue;
        }
        if same(&value_of(&out, &k), &a) {
            out = set_key(&out, &k, b.as_deref());
        }
    }
    // Same keys as before Play: give the text back byte for byte (blank lines at the end included).
    if out.trim_end() == before.trim_end() {
        return before.to_string();
    }
    out
}

// ---- backup / restore --------------------------------------------------------------------------------------------------

fn read_backup(config_dir: &Path) -> Option<Backup> {
    serde_json::from_str(&std::fs::read_to_string(backup_path(config_dir)).ok()?).ok()
}

fn same_dir(a: &str, b: &str) -> bool {
    let norm = |s: &str| s.replace('\\', "/").trim_end_matches('/').to_ascii_lowercase();
    norm(a) == norm(b)
}

/// Remember the state before a Play session. A backup of the same mods folder is kept (it holds the player's real
/// `mods.ini`, while the file on disk may already be the one of an earlier session that never got restored); only the text
/// Play wrote is refreshed. A backup of another mods folder (the game folder was changed) is restored there first.
pub fn save_backup(config_dir: &Path, backup: &Backup) -> std::io::Result<()> {
    std::fs::create_dir_all(config_dir)?;
    let file = backup_path(config_dir);
    if let Some(old) = read_backup(config_dir) {
        if !old.restored && same_dir(&old.mods, &backup.mods) {
            let kept = Backup { after: backup.after.clone(), ..old };
            return write_atomic(&file, &serde_json::to_string(&kept).unwrap_or_default());
        }
        // Another folder, or an old session whose mods.ini is already back: finish that one, then start fresh.
        let _ = restore(config_dir);
        let _ = std::fs::remove_file(&file);
    } else if file.exists() {
        let _ = std::fs::remove_file(&file);
    }
    write_atomic(&file, &serde_json::to_string(backup).unwrap_or_default())
}

/// Put `mods.ini` back (only the keys Play changed) and delete the Play pack.
pub fn restore(config_dir: &Path) -> std::io::Result<Restored> {
    let file = backup_path(config_dir);
    let Ok(text) = std::fs::read_to_string(&file) else {
        return Ok(Restored::Nothing);
    };
    let Ok(mut b) = serde_json::from_str::<Backup>(&text) else {
        std::fs::remove_file(&file)?;
        return Ok(Restored::Nothing);
    };
    let mods = PathBuf::from(&b.mods);
    if !b.restored && mods.is_dir() {
        let ini_path = mods.join("mods.ini");
        let current = std::fs::read_to_string(&ini_path).ok();
        match (&b.after, &b.ini) {
            (Some(after), before) => {
                if let Some(cur) = current {
                    let back = revert_keys(&cur, before.as_deref().unwrap_or(""), after);
                    if before.is_none() && mods_entries(&back).is_empty() {
                        // Play created the file and nothing else lives in it.
                        std::fs::remove_file(&ini_path)?;
                    } else if back != cur {
                        write_atomic(&ini_path, &back)?;
                    }
                }
            }
            // Backup of an older editor: put the whole text back.
            (None, Some(before)) => write_atomic(&ini_path, before)?,
            (None, None) => {
                let _ = std::fs::remove_file(&ini_path);
            }
        }
    }
    b.restored = true;
    let zip = zip_path(&mods, &b.id);
    if zip.exists() && std::fs::remove_file(&zip).is_err() {
        write_atomic(&file, &serde_json::to_string(&b).unwrap_or_default())?;
        return Ok(Restored::Pending);
    }
    std::fs::remove_file(file)?;
    Ok(Restored::Done)
}

/// Restore, and keep trying (in the background) while the zip is still locked by the closing game.
pub fn restore_with_retry(config_dir: PathBuf) {
    if let Ok(Restored::Pending) = restore(&config_dir) {
        std::thread::spawn(move || {
            for _ in 0..90 {
                std::thread::sleep(std::time::Duration::from_secs(2));
                if !matches!(restore(&config_dir), Ok(Restored::Pending)) {
                    return;
                }
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("drakan-playpack-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn backup(mods: &Path, before: Option<&str>, after: &str) -> Backup {
        Backup {
            mods: mods.to_string_lossy().to_string(),
            id: "editor-play-ootf".into(),
            ini: before.map(str::to_string),
            after: Some(after.to_string()),
            restored: false,
        }
    }

    const BEFORE: &str = "[mods]\nootf-levels = on\nother = on\n";
    const AFTER: &str = "[mods]\nootf-levels = off\nother = on\neditor-play-ootf = on\n";

    #[test]
    fn restore_reverts_only_the_keys_play_changed_and_removes_only_the_play_pack() {
        let cfg = tmp("cfg");
        let mods = tmp("mods");
        std::fs::write(mods.join("mods.ini"), AFTER).unwrap();
        std::fs::write(mods.join("editor-play-ootf.zip"), b"play").unwrap();
        std::fs::write(mods.join("ootf-levels.zip"), b"players").unwrap();
        save_backup(&cfg, &backup(&mods, Some(BEFORE), AFTER)).unwrap();
        assert_eq!(restore(&cfg).unwrap(), Restored::Done);
        assert_eq!(std::fs::read_to_string(mods.join("mods.ini")).unwrap(), BEFORE);
        assert!(!mods.join("editor-play-ootf.zip").exists());
        assert!(mods.join("ootf-levels.zip").exists());
        assert!(!backup_path(&cfg).exists());
        assert_eq!(restore(&cfg).unwrap(), Restored::Nothing);
    }

    #[test]
    fn a_change_made_while_the_game_ran_survives_the_restore() {
        let cfg = tmp("cfg-merge");
        let mods = tmp("mods-merge");
        // The player switched `other` off in the game's menu and a new pack was installed meanwhile.
        let now = "[mods]\nootf-levels = off\nother = off\neditor-play-ootf = on\nnewpack = on\n";
        std::fs::write(mods.join("mods.ini"), now).unwrap();
        save_backup(&cfg, &backup(&mods, Some(BEFORE), AFTER)).unwrap();
        restore(&cfg).unwrap();
        let got = std::fs::read_to_string(mods.join("mods.ini")).unwrap();
        assert_eq!(got, "[mods]\nootf-levels = on\nother = off\nnewpack = on\n");
    }

    #[test]
    fn a_key_the_player_flipped_back_themselves_is_left_alone() {
        let cur = "[mods]\nootf-levels = on\nother = on\n";
        // Play switched ootf-levels off; the player already turned it on again.
        assert_eq!(revert_keys(cur, BEFORE, AFTER), cur);
    }

    #[test]
    fn a_full_revert_returns_the_original_text_exactly() {
        let before = "[mods]
ootf-levels = on

";
        let after = "[mods]
ootf-levels = off
editor-play-ootf = on
";
        assert_eq!(revert_keys(after, before, after), before);
    }

    #[test]
    fn crlf_files_stay_crlf() {
        let b = BEFORE.replace('\n', "\r\n");
        let a = AFTER.replace('\n', "\r\n");
        assert_eq!(revert_keys(&a, &b, &a), b);
        let edited = set_key(&a, "ootf-levels", Some("on"));
        assert!(edited.contains("ootf-levels = on\r\n"));
        assert!(!edited.replace("\r\n", "").contains('\n'));
    }

    #[test]
    fn no_ini_before_play_means_no_ini_after() {
        let cfg = tmp("cfg-none");
        let mods = tmp("mods-none");
        let after = "[mods]\neditor-play-ootf = on\n";
        std::fs::write(mods.join("mods.ini"), after).unwrap();
        save_backup(&cfg, &backup(&mods, None, after)).unwrap();
        assert_eq!(restore(&cfg).unwrap(), Restored::Done);
        assert!(!mods.join("mods.ini").exists());
    }

    #[test]
    fn a_file_play_created_but_the_player_added_to_is_kept() {
        let cfg = tmp("cfg-none2");
        let mods = tmp("mods-none2");
        let after = "[mods]\neditor-play-ootf = on\n";
        std::fs::write(mods.join("mods.ini"), "[mods]\neditor-play-ootf = on\nmine = on\n").unwrap();
        save_backup(&cfg, &backup(&mods, None, after)).unwrap();
        restore(&cfg).unwrap();
        assert_eq!(std::fs::read_to_string(mods.join("mods.ini")).unwrap(), "[mods]\nmine = on\n");
    }

    #[test]
    fn a_second_session_keeps_the_first_before_text() {
        let cfg = tmp("cfg2");
        let mods = tmp("mods2");
        save_backup(&cfg, &backup(&mods, Some("original"), "a")).unwrap();
        save_backup(&cfg, &backup(&mods, Some("already modified"), "b")).unwrap();
        let kept = read_backup(&cfg).unwrap();
        assert_eq!(kept.ini.as_deref(), Some("original"));
        assert_eq!(kept.after.as_deref(), Some("b"));
    }

    #[test]
    fn a_backup_of_another_mods_folder_is_restored_there_first() {
        let cfg = tmp("cfg-stale");
        let old = tmp("mods-old");
        let new = tmp("mods-new");
        std::fs::write(old.join("mods.ini"), AFTER).unwrap();
        std::fs::write(old.join("editor-play-ootf.zip"), b"x").unwrap();
        save_backup(&cfg, &backup(&old, Some(BEFORE), AFTER)).unwrap();
        save_backup(&cfg, &backup(&new, Some("[mods]\n"), "[mods]\neditor-play-ootf = on\n")).unwrap();
        assert_eq!(std::fs::read_to_string(old.join("mods.ini")).unwrap(), BEFORE);
        assert!(!old.join("editor-play-ootf.zip").exists());
        assert!(same_dir(&read_backup(&cfg).unwrap().mods, &new.to_string_lossy()));
    }

    #[test]
    fn an_old_backup_without_the_after_text_restores_the_whole_file() {
        let cfg = tmp("cfg-legacy");
        let mods = tmp("mods-legacy");
        std::fs::write(mods.join("mods.ini"), AFTER).unwrap();
        let json = format!(
            "{{\"mods\":{},\"id\":\"editor-play-ootf\",\"ini\":{}}}",
            serde_json::to_string(&mods.to_string_lossy()).unwrap(),
            serde_json::to_string(BEFORE).unwrap()
        );
        std::fs::write(backup_path(&cfg), json).unwrap();
        assert_eq!(restore(&cfg).unwrap(), Restored::Done);
        assert_eq!(std::fs::read_to_string(mods.join("mods.ini")).unwrap(), BEFORE);
    }

    #[cfg(windows)]
    #[test]
    fn a_locked_zip_keeps_a_pending_backup_that_finishes_later() {
        let cfg = tmp("cfg-lock");
        let mods = tmp("mods-lock");
        std::fs::write(mods.join("mods.ini"), AFTER).unwrap();
        let zip = mods.join("editor-play-ootf.zip");
        std::fs::write(&zip, b"x").unwrap();
        save_backup(&cfg, &backup(&mods, Some(BEFORE), AFTER)).unwrap();
        let held = open_exclusive(&zip).unwrap();
        assert_eq!(restore(&cfg).unwrap(), Restored::Pending);
        // The ini is back already and a later retry does not touch it again.
        assert_eq!(std::fs::read_to_string(mods.join("mods.ini")).unwrap(), BEFORE);
        std::fs::write(mods.join("mods.ini"), "[mods]\nchanged = on\n").unwrap();
        drop(held);
        assert_eq!(restore(&cfg).unwrap(), Restored::Done);
        assert_eq!(std::fs::read_to_string(mods.join("mods.ini")).unwrap(), "[mods]\nchanged = on\n");
        assert!(!zip.exists());
        assert!(!backup_path(&cfg).exists());
    }

    #[cfg(windows)]
    #[test]
    fn a_held_lock_is_seen_by_another_opener() {
        let cfg = tmp("cfg-own");
        let f = open_exclusive(&lock_path(&cfg)).unwrap();
        assert!(open_exclusive(&lock_path(&cfg)).is_err());
        drop(f);
        assert!(open_exclusive(&lock_path(&cfg)).is_ok());
    }

    #[cfg(windows)]
    #[test]
    fn a_running_game_blocks_a_second_window_until_it_ends() {
        let cfg = tmp("cfg-game");
        let gen = begin_game_session(&cfg).unwrap();
        // this window owns it: not "elsewhere"; another window (a separate handle) cannot take it
        assert!(!game_session_elsewhere(&cfg));
        let other = open_exclusive(&game_lock_path(&cfg)).unwrap_err();
        assert!(is_sharing_violation(&other));
        end_game_session(gen + 1); // a stale watcher of an older session changes nothing
        assert!(open_exclusive(&game_lock_path(&cfg)).is_err());
        end_game_session(gen);
        assert!(!game_session_elsewhere(&cfg));
        assert!(open_exclusive(&game_lock_path(&cfg)).is_ok());
    }

    #[test]
    fn atomic_write_replaces_the_file() {
        let d = tmp("atomic");
        let f = d.join("mods.ini");
        write_atomic(&f, "one").unwrap();
        write_atomic(&f, "two").unwrap();
        assert_eq!(std::fs::read_to_string(&f).unwrap(), "two");
        assert_eq!(std::fs::read_dir(&d).unwrap().count(), 1);
    }
}

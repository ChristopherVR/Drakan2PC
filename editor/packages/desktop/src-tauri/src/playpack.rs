//! The pack the editor installs for one Play session (the converted Order of the Flame levels). It lives under its own id
//! (`editor-play-ootf`), never touches a pack the player installed, and is taken out again, with `mods.ini` as it was
//! before, when the game closes (or at the next start of the editor if the editor itself was closed first).

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

/// What to put back: the `mods.ini` text from before the Play session and the pack file to delete.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq)]
pub struct Backup {
    pub mods: String,
    pub id: String,
    pub ini: String,
}

pub fn backup_path(config_dir: &Path) -> PathBuf {
    config_dir.join("play-pack-backup.json")
}

pub fn zip_path(mods: &Path, id: &str) -> PathBuf {
    mods.join(format!("{id}.zip"))
}

/// Remember the state before a Play session. An existing backup is kept: it holds the player's real `mods.ini`, while the
/// file on disk may already be the one of an earlier session that never got restored.
pub fn save_backup(config_dir: &Path, backup: &Backup) -> std::io::Result<()> {
    let file = backup_path(config_dir);
    if file.exists() {
        return Ok(());
    }
    std::fs::create_dir_all(config_dir)?;
    std::fs::write(file, serde_json::to_string(backup).unwrap_or_default())
}

/// Put `mods.ini` back and delete the Play pack. Returns whether there was anything to restore.
pub fn restore(config_dir: &Path) -> std::io::Result<bool> {
    let file = backup_path(config_dir);
    let Ok(text) = std::fs::read_to_string(&file) else {
        return Ok(false);
    };
    let Ok(b) = serde_json::from_str::<Backup>(&text) else {
        std::fs::remove_file(&file)?;
        return Ok(false);
    };
    let mods = PathBuf::from(&b.mods);
    std::fs::write(mods.join("mods.ini"), &b.ini)?;
    let zip = zip_path(&mods, &b.id);
    if zip.exists() {
        std::fs::remove_file(zip)?;
    }
    std::fs::remove_file(file)?;
    Ok(true)
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

    #[test]
    fn restore_puts_the_ini_back_and_removes_only_the_play_pack() {
        let cfg = tmp("cfg");
        let mods = tmp("mods");
        std::fs::write(mods.join("mods.ini"), "[mods]\nootf-levels = off\neditor-play-ootf = on\n").unwrap();
        std::fs::write(mods.join("editor-play-ootf.zip"), b"play").unwrap();
        std::fs::write(mods.join("ootf-levels.zip"), b"players").unwrap();
        let b = Backup {
            mods: mods.to_string_lossy().to_string(),
            id: "editor-play-ootf".into(),
            ini: "[mods]\nootf-levels = on\n".into(),
        };
        save_backup(&cfg, &b).unwrap();
        assert!(restore(&cfg).unwrap());
        assert_eq!(std::fs::read_to_string(mods.join("mods.ini")).unwrap(), b.ini);
        assert!(!mods.join("editor-play-ootf.zip").exists());
        assert!(mods.join("ootf-levels.zip").exists());
        assert!(!restore(&cfg).unwrap());
    }

    #[test]
    fn a_second_session_keeps_the_first_backup() {
        let cfg = tmp("cfg2");
        let mut b = Backup { mods: "m".into(), id: "x".into(), ini: "original".into() };
        save_backup(&cfg, &b).unwrap();
        b.ini = "already modified".into();
        save_backup(&cfg, &b).unwrap();
        let kept: Backup = serde_json::from_str(&std::fs::read_to_string(backup_path(&cfg)).unwrap()).unwrap();
        assert_eq!(kept.ini, "original");
    }
}

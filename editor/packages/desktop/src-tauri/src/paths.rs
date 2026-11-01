//! Pure validation helpers for everything the webview can ask the shell to write. Kept free of Tauri so they are unit tested.

use std::path::{Path, PathBuf};

/// Mod pack ids become folder names in `mods/` and keys in `mods.ini`.
pub fn valid_mod_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 64
        && !id.starts_with('.')
        && id
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-'))
}

/// A folder name for a new project: the name with characters Windows forbids replaced, never empty or dotted.
pub fn folder_name(name: &str) -> String {
    let s: String = name
        .trim()
        .chars()
        .map(|c| if c.is_control() || "\\/:*?\"<>|".contains(c) { '-' } else { c })
        .collect();
    let s: String = s.trim_matches(|c| c == '.' || c == ' ' || c == '-').chars().take(64).collect();
    if s.is_empty() {
        "My mod".to_string()
    } else {
        s
    }
}

/// A path inside a pack: relative, `/` separated, no `.` / `..`, no drive or stream syntax.
pub fn valid_pack_path(p: &str) -> bool {
    if p.is_empty() || p.len() > 240 || p.starts_with('/') {
        return false;
    }
    let parts: Vec<&str> = p.split('/').collect();
    if parts.len() > 8 {
        return false;
    }
    parts.iter().all(|c| {
        !c.is_empty()
            && *c != "."
            && *c != ".."
            && !c.ends_with('.')
            && !c.ends_with(' ')
            && !c.chars().any(|ch| ch.is_control() || "\\:*?\"<>|".contains(ch))
    })
}

/// Join a validated pack path under `base`.
pub fn join_pack_path(base: &Path, rel: &str) -> Option<PathBuf> {
    if !valid_pack_path(rel) {
        return None;
    }
    let mut out = base.to_path_buf();
    for c in rel.split('/') {
        out.push(c);
    }
    Some(out)
}

/// First free `NAME.bak`, `NAME.bak.1`, ... given the names already in the folder (mirrors `nextBackupName`).
pub fn next_backup_name(existing: &[String], name: &str) -> String {
    let has = |c: &str| existing.iter().any(|e| e.eq_ignore_ascii_case(c));
    let mut candidate = format!("{name}.bak");
    let mut n = 1;
    while has(&candidate) {
        candidate = format!("{name}.bak.{n}");
        n += 1;
    }
    candidate
}

/// Environment variables the editor may set on the game. Anything else is dropped.
pub fn allowed_env(key: &str) -> bool {
    matches!(
        key,
        "DRAKAN_START_LEVEL"
            | "DRAKAN_START_LEVEL_FROM"
            | "DRAKAN_GAME"
            | "DRAKAN_SKIP_MEMCARD_WARNING"
            | "DRAKAN_AUTOPRESS"
            | "DRAKAN_AUTOPRESS_UNTIL"
            | "DRAKAN_INPUT_AUTOSTART"
            | "DRAKAN_NET_HOST"
            | "DRAKAN_NET_CONNECT"
            | "DRAKAN_NET_MODE"
            | "DRAKAN_NET_NAME"
            | "DRAKAN_GHOST"
            | "DRAKAN_MP_MAP"
            | "DRAKAN_MP_MAPS"
            | "DRAKAN_PLAYTEST_POS"
            | "DRAKAN_PLAYTEST_GOD"
    )
}

/// `DRAKAN_PLAYTEST_CARD=1` (not passed to the game): the shell gives the game a fresh, empty memory card instead of
/// the player's, so the title's Cross is New Game (on the player's card it was Continue, which loaded their latest save)
/// and a play test never reads or writes the player's saves. The path is chosen here, never by the web view.
pub const PLAYTEST_CARD: &str = "DRAKAN_PLAYTEST_CARD";

/// The throw-away card root for instance `n` (0 = the main game, 1.. = extra instances), as `DRAKAN_MEMCARD_DIR` wants
/// it (`.../mc0`).
pub fn playtest_card_dir(base: &Path, n: usize) -> PathBuf {
    base.join("drakan-editor-playtest").join(format!("card{n}")).join("mc0")
}

pub fn valid_env_value(v: &str) -> bool {
    v.len() <= 260 && !v.chars().any(|c| c == '\0' || c == '\r' || c == '\n')
}

pub fn has_ext(p: &Path, ext: &str) -> bool {
    p.extension()
        .and_then(|e| e.to_str())
        .is_some_and(|e| e.eq_ignore_ascii_case(ext))
}

/// The game executable inside a play folder (`drakan.exe`, any case).
pub fn find_game_exe(dir: &Path) -> Option<PathBuf> {
    let rd = std::fs::read_dir(dir).ok()?;
    rd.flatten().map(|e| e.path()).find(|p| {
        p.is_file()
            && p.file_name()
                .and_then(|n| n.to_str())
                .is_some_and(|n| n.eq_ignore_ascii_case("drakan.exe"))
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mod_ids() {
        assert!(valid_mod_id("editor.level1na-edit"));
        assert!(!valid_mod_id(""));
        assert!(!valid_mod_id(".."));
        assert!(!valid_mod_id("a/b"));
        assert!(!valid_mod_id("a b"));
        assert!(!valid_mod_id(".hidden"));
        assert!(!valid_mod_id(&"x".repeat(65)));
    }

    #[test]
    fn pack_paths() {
        assert!(valid_pack_path("overrides/LEVEL1NA.LVL/0020_0_0.bin"));
        assert!(valid_pack_path("mod.toml"));
        for bad in ["", "/etc/x", "../x", "a/../b", "a//b", "C:/x", "a\\b", "a/b:stream", "a/./b", "x.", "a/b "] {
            assert!(!valid_pack_path(bad), "{bad}");
        }
        assert!(!valid_pack_path("a/b/c/d/e/f/g/h/i"));
    }

    #[test]
    fn join_stays_inside() {
        let base = Path::new("mods").join("id");
        assert_eq!(join_pack_path(&base, "a/b.bin").unwrap(), base.join("a").join("b.bin"));
        assert!(join_pack_path(&base, "../b.bin").is_none());
    }

    #[test]
    fn project_folder_names() {
        assert_eq!(folder_name("My Map: v2?"), "My Map- v2");
        assert_eq!(folder_name(".."), "My mod");
        assert_eq!(folder_name("  "), "My mod");
        assert_eq!(folder_name("a/b"), "a-b");
    }

    #[test]
    fn backups() {
        let have = vec!["A.rsg".to_string(), "A.rsg.bak".to_string(), "A.rsg.bak.1".to_string()];
        assert_eq!(next_backup_name(&have, "A.rsg"), "A.rsg.bak.2");
        assert_eq!(next_backup_name(&[], "A.rsg"), "A.rsg.bak");
    }

    #[test]
    fn env_allow_list() {
        assert!(allowed_env("DRAKAN_START_LEVEL"));
        assert!(!allowed_env("PATH"));
        assert!(!allowed_env("DRAKAN_MODS"));
        assert!(!allowed_env("DRAKAN_MEMCARD_DIR"));
        assert!(!allowed_env(PLAYTEST_CARD));
        let d = playtest_card_dir(Path::new("t"), 2);
        assert!(d.ends_with("drakan-editor-playtest/card2/mc0"));
        assert!(!valid_env_value("a\nb"));
    }
}

//! Remembered choices (recent discs, game and mods folders), persisted as JSON in the app config folder.

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

const MAX_RECENT: usize = 8;

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
#[serde(rename_all = "camelCase", default)]
pub struct Settings {
    pub recent_isos: Vec<String>,
    /// Project folders (each holds `project.drakan.json`), newest first.
    pub recent_projects: Vec<String>,
    pub game_dir: Option<String>,
    pub mods_dir: Option<String>,
    pub save_dir: Option<String>,
    pub iso_dir: Option<String>,
}

impl Settings {
    pub fn load(file: &Path) -> Settings {
        std::fs::read_to_string(file)
            .ok()
            .and_then(|t| serde_json::from_str(&t).ok())
            .unwrap_or_default()
    }

    pub fn save(&self, file: &Path) -> Result<(), String> {
        if let Some(dir) = file.parent() {
            std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
        }
        let text = serde_json::to_string_pretty(self).map_err(|e| e.to_string())?;
        std::fs::write(file, text).map_err(|e| e.to_string())
    }

    /// Put `path` first in the recent list (case-insensitive de-dup) and remember its folder.
    pub fn push_iso(&mut self, path: &Path) {
        let s = path.to_string_lossy().to_string();
        self.recent_isos.retain(|p| !p.eq_ignore_ascii_case(&s));
        self.recent_isos.insert(0, s);
        self.recent_isos.truncate(MAX_RECENT);
        self.iso_dir = path.parent().map(|p| p.to_string_lossy().to_string());
    }

    /// Put a project folder first in the recent list (case-insensitive de-dup).
    pub fn push_project(&mut self, dir: &Path) {
        let s = dir.to_string_lossy().to_string();
        self.recent_projects.retain(|p| !p.eq_ignore_ascii_case(&s));
        self.recent_projects.insert(0, s);
        self.recent_projects.truncate(MAX_RECENT);
    }

    pub fn mods_folder(&self) -> Option<PathBuf> {
        self.mods_dir
            .as_ref()
            .map(PathBuf::from)
            .or_else(|| self.game_dir.as_ref().map(|g| PathBuf::from(g).join("mods")))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn recent_order_and_dedup() {
        let mut s = Settings::default();
        for i in 0..10 {
            s.push_iso(Path::new(&format!("C:/iso/{i}.iso")));
        }
        s.push_iso(Path::new("c:/ISO/5.iso"));
        assert_eq!(s.recent_isos.len(), MAX_RECENT);
        assert_eq!(s.recent_isos[0], "c:/ISO/5.iso");
        assert_eq!(
            s.recent_isos.iter().filter(|p| p.to_lowercase().ends_with("5.iso")).count(),
            1
        );
    }

    #[test]
    fn recent_projects_dedup() {
        let mut s = Settings::default();
        s.push_project(Path::new("C:/p/a"));
        s.push_project(Path::new("C:/p/b"));
        s.push_project(Path::new("c:/P/A"));
        assert_eq!(s.recent_projects, vec!["c:/P/A".to_string(), "C:/p/b".to_string()]);
    }

    #[test]
    fn mods_default_is_inside_game_dir() {
        let mut s = Settings::default();
        assert!(s.mods_folder().is_none());
        s.game_dir = Some("D:/Play".into());
        assert_eq!(s.mods_folder().unwrap(), PathBuf::from("D:/Play").join("mods"));
        s.mods_dir = Some("E:/m".into());
        assert_eq!(s.mods_folder().unwrap(), PathBuf::from("E:/m"));
    }

    #[test]
    fn round_trip() {
        let dir = std::env::temp_dir().join(format!("drakan-editor-test-{}", std::process::id()));
        let file = dir.join("settings.json");
        let mut s = Settings::default();
        s.push_iso(Path::new("C:/a.iso"));
        s.game_dir = Some("C:/g".into());
        s.save(&file).unwrap();
        let back = Settings::load(&file);
        assert_eq!(back.recent_isos, s.recent_isos);
        assert_eq!(back.game_dir, s.game_dir);
        let _ = std::fs::remove_dir_all(dir);
    }
}

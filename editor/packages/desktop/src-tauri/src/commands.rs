//! The commands the webview may call. Every path that is read or written is either chosen by the user in a native dialog
//! during this session, remembered from an earlier one (`settings.json`), or derived from the chosen game folder.

use crate::paths::{self, allowed_env, valid_env_value, valid_mod_id};
use crate::store::Settings;
use crate::AppState;
use base64::Engine;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use tauri::ipc::{InvokeBody, Request, Response};
use tauri::{AppHandle, Manager, State};
use tauri_plugin_dialog::DialogExt;

type Res<T> = Result<T, String>;

fn err<E: std::fmt::Display>(e: E) -> String {
    e.to_string()
}

fn modified_ms(meta: &std::fs::Metadata) -> f64 {
    meta.modified()
        .ok()
        .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
        .map(|d| d.as_millis() as f64)
        .unwrap_or(0.0)
}

fn header_text(req: &Request<'_>, name: &str) -> Res<String> {
    let v = req
        .headers()
        .get(name)
        .ok_or_else(|| format!("missing header {name}"))?;
    let bytes = base64::engine::general_purpose::STANDARD
        .decode(v.as_bytes())
        .map_err(err)?;
    String::from_utf8(bytes).map_err(err)
}

fn raw_body<'a>(req: &'a Request<'_>) -> Res<&'a [u8]> {
    match req.body() {
        InvokeBody::Raw(b) => Ok(b.as_slice()),
        _ => Err("expected a binary body".into()),
    }
}

// ---- settings ----------------------------------------------------------------------------------------------------------

#[tauri::command]
pub fn settings_get(state: State<'_, AppState>) -> Settings {
    state.inner.lock().unwrap().settings.clone()
}

/// Forget the recent discs (the folders stay).
#[tauri::command]
pub fn settings_forget(app: AppHandle, state: State<'_, AppState>) -> Res<Settings> {
    let out = {
        let mut g = state.inner.lock().unwrap();
        g.settings.recent_isos.clear();
        g.settings.save(&state.settings_file).map_err(err)?;
        g.settings.clone()
    };
    crate::menu::refresh(&app);
    Ok(out)
}

// ---- disc image --------------------------------------------------------------------------------------------------------

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IsoInfo {
    pub path: String,
    pub name: String,
    pub size: f64,
}

fn grant_iso(app: &AppHandle, state: &AppState, path: &Path) -> Res<IsoInfo> {
    let meta = std::fs::metadata(path).map_err(|e| format!("{}: {e}", path.display()))?;
    if !meta.is_file() {
        return Err(format!("{} is not a file", path.display()));
    }
    // The webview reads the image through the asset protocol with range requests, so a multi-GB disc is never loaded
    // whole. Only this one file is added to the protocol's scope.
    app.asset_protocol_scope().allow_file(path).map_err(err)?;
    {
        let mut g = state.inner.lock().unwrap();
        g.settings.push_iso(path);
        g.settings.save(&state.settings_file).map_err(err)?;
    }
    crate::menu::refresh(app);
    Ok(IsoInfo {
        path: path.to_string_lossy().to_string(),
        name: path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default(),
        size: meta.len() as f64,
    })
}

#[tauri::command]
pub async fn iso_pick(app: AppHandle, state: State<'_, AppState>) -> Res<Option<IsoInfo>> {
    let start = state.inner.lock().unwrap().settings.iso_dir.clone();
    let mut d = app
        .dialog()
        .file()
        .set_title("Open your Drakan disc image")
        .add_filter("Disc image", &["iso", "bin", "img"])
        .add_filter("All files", &["*"]);
    if let Some(s) = start.filter(|s| Path::new(s).is_dir()) {
        d = d.set_directory(s);
    }
    let Some(p) = d.blocking_pick_file() else {
        return Ok(None);
    };
    let path = p.into_path().map_err(err)?;
    grant_iso(&app, &state, &path).map(Some)
}

/// Re-open a disc from the recent list (the only way to open a path without a dialog).
#[tauri::command]
pub async fn iso_open(app: AppHandle, state: State<'_, AppState>, path: String) -> Res<IsoInfo> {
    let known = state
        .inner
        .lock()
        .unwrap()
        .settings
        .recent_isos
        .iter()
        .any(|p| p.eq_ignore_ascii_case(&path));
    if !known {
        return Err("That disc is not in the recent list; use Open ISO.".into());
    }
    grant_iso(&app, &state, Path::new(&path))
}

// ---- folders -----------------------------------------------------------------------------------------------------------

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FolderInfo {
    pub path: String,
    pub name: String,
    /// For the game folder: whether it holds `drakan.exe`.
    pub has_game: bool,
}

/// Choose and remember the game (play) folder (`which` = "game") or the mods folder ("mods").
#[tauri::command]
pub async fn folder_pick(app: AppHandle, state: State<'_, AppState>, which: String) -> Res<Option<FolderInfo>> {
    let (title, start) = {
        let g = state.inner.lock().unwrap();
        match which.as_str() {
            "game" => ("Choose the play folder (the one with drakan.exe)", g.settings.game_dir.clone()),
            "mods" => ("Choose the mods folder", g.settings.mods_folder().map(|p| p.to_string_lossy().to_string())),
            _ => return Err(format!("unknown folder kind '{which}'")),
        }
    };
    let mut d = app.dialog().file().set_title(title);
    if let Some(s) = start.filter(|s| Path::new(s).is_dir()) {
        d = d.set_directory(s);
    }
    let Some(p) = d.blocking_pick_folder() else {
        return Ok(None);
    };
    let dir = p.into_path().map_err(err)?;
    let has_game = paths::find_game_exe(&dir).is_some();
    if which == "game" && !has_game {
        return Err(format!(
            "{} has no drakan.exe. Pick the play folder (for example dist/DrakanPC).",
            dir.display()
        ));
    }
    let text = dir.to_string_lossy().to_string();
    {
        let mut g = state.inner.lock().unwrap();
        if which == "game" {
            g.settings.game_dir = Some(text.clone());
        } else {
            g.settings.mods_dir = Some(text.clone());
        }
        g.settings.save(&state.settings_file).map_err(err)?;
    }
    Ok(Some(FolderInfo {
        name: dir.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default(),
        path: text,
        has_game,
    }))
}

// ---- saves -------------------------------------------------------------------------------------------------------------

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SaveFile {
    pub path: String,
    pub name: String,
    pub folder: String,
    pub size: f64,
    pub modified: f64,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SaveFolder {
    pub root: String,
    pub files: Vec<SaveFile>,
}

fn save_file_info(path: &Path, folder: &str) -> Res<SaveFile> {
    let meta = std::fs::metadata(path).map_err(err)?;
    Ok(SaveFile {
        path: path.to_string_lossy().to_string(),
        name: path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default(),
        folder: folder.to_string(),
        size: meta.len() as f64,
        modified: modified_ms(&meta),
    })
}

fn grant_save(state: &AppState, path: &Path) {
    let mut g = state.inner.lock().unwrap();
    g.granted_saves.insert(path.to_string_lossy().to_lowercase());
    if let Some(dir) = path.parent() {
        g.settings.save_dir = Some(dir.to_string_lossy().to_string());
    }
}

fn is_granted_save(state: &AppState, path: &str) -> Res<PathBuf> {
    let p = PathBuf::from(path);
    if !paths::has_ext(&p, "rsg") {
        return Err("Only .rsg save files can be read or written.".into());
    }
    if !state.inner.lock().unwrap().granted_saves.contains(&path.to_lowercase()) {
        return Err("That save was not opened in this session; use Open Save.".into());
    }
    Ok(p)
}

fn dialog_start(state: &AppState) -> Option<String> {
    state
        .inner
        .lock()
        .unwrap()
        .settings
        .save_dir
        .clone()
        .filter(|s| Path::new(s).is_dir())
}

#[tauri::command]
pub async fn save_pick_file(app: AppHandle, state: State<'_, AppState>) -> Res<Option<SaveFile>> {
    let mut d = app
        .dialog()
        .file()
        .set_title("Open a Drakan save")
        .add_filter("Save file", &["rsg"]);
    if let Some(s) = dialog_start(&state) {
        d = d.set_directory(s);
    }
    let Some(p) = d.blocking_pick_file() else {
        return Ok(None);
    };
    let path = p.into_path().map_err(err)?;
    if !paths::has_ext(&path, "rsg") {
        return Err("Pick a .rsg save file.".into());
    }
    grant_save(&state, &path);
    let _ = persist(&state);
    save_file_info(&path, "").map(Some)
}

fn walk_saves(dir: &Path, folder: &str, depth: u32, out: &mut Vec<PathBuf>, names: &mut Vec<String>) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    for e in rd.flatten() {
        let p = e.path();
        let name = e.file_name().to_string_lossy().to_string();
        if p.is_file() {
            if paths::has_ext(&p, "rsg") {
                out.push(p);
                names.push(folder.to_string());
            }
        } else if p.is_dir() && depth > 0 && !name.starts_with('.') {
            walk_saves(&p, &name, depth - 1, out, names);
        }
    }
}

/// Pick a memory card folder (the card root, `mc0`, or one slot) and list its `.rsg` files up to four levels down.
#[tauri::command]
pub async fn save_pick_folder(app: AppHandle, state: State<'_, AppState>) -> Res<Option<SaveFolder>> {
    let mut d = app.dialog().file().set_title("Open a memory card folder");
    if let Some(s) = dialog_start(&state) {
        d = d.set_directory(s);
    }
    let Some(p) = d.blocking_pick_folder() else {
        return Ok(None);
    };
    let dir = p.into_path().map_err(err)?;
    let root = dir.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default();
    let (mut found, mut folders) = (Vec::new(), Vec::new());
    walk_saves(&dir, &root, 4, &mut found, &mut folders);
    let mut files = Vec::new();
    for (p, folder) in found.iter().zip(folders.iter()) {
        grant_save(&state, p);
        files.push(save_file_info(p, folder)?);
    }
    state.inner.lock().unwrap().settings.save_dir = Some(dir.to_string_lossy().to_string());
    let _ = persist(&state);
    Ok(Some(SaveFolder { root, files }))
}

fn persist(state: &AppState) -> Res<()> {
    state.inner.lock().unwrap().settings.save(&state.settings_file)
}

#[tauri::command]
pub fn save_read(state: State<'_, AppState>, path: String) -> Res<Response> {
    let p = is_granted_save(&state, &path)?;
    Ok(Response::new(std::fs::read(p).map_err(err)?))
}

#[tauri::command]
pub fn save_stat(state: State<'_, AppState>, path: String) -> Res<SaveFile> {
    let p = is_granted_save(&state, &path)?;
    save_file_info(&p, "")
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WriteResult {
    pub backup: String,
    pub size: f64,
    pub modified: f64,
}

/// Overwrite a save with the raw request body, first copying the old file to `NAME.bak` (then `.bak.1`, ...) beside it.
/// Nothing is written when the backup fails; the new bytes go to a temp file that then replaces the save.
#[tauri::command]
pub fn save_write(state: State<'_, AppState>, request: Request<'_>) -> Res<WriteResult> {
    let path = header_text(&request, "x-path")?;
    let p = is_granted_save(&state, &path)?;
    let bytes = raw_body(&request)?;
    let dir = p.parent().ok_or("save has no folder")?;
    let name = p.file_name().ok_or("save has no name")?.to_string_lossy().to_string();
    let existing: Vec<String> = std::fs::read_dir(dir)
        .map_err(err)?
        .flatten()
        .map(|e| e.file_name().to_string_lossy().to_string())
        .collect();
    let backup = paths::next_backup_name(&existing, &name);
    std::fs::copy(&p, dir.join(&backup)).map_err(|e| format!("backup failed, nothing written: {e}"))?;
    let tmp = dir.join(format!("{name}.tmp-editor"));
    std::fs::write(&tmp, bytes).map_err(err)?;
    std::fs::rename(&tmp, &p).map_err(|e| {
        let _ = std::fs::remove_file(&tmp);
        err(e)
    })?;
    let meta = std::fs::metadata(&p).map_err(err)?;
    Ok(WriteResult {
        backup,
        size: meta.len() as f64,
        modified: modified_ms(&meta),
    })
}

/// Export: show a save dialog (suggested name from the `x-name` header, extension filter from `x-ext`) and write the
/// body where the user says. Returns the chosen path, or null when cancelled.
#[tauri::command]
pub async fn file_save_as(app: AppHandle, state: State<'_, AppState>, request: Request<'_>) -> Res<Option<String>> {
    let name = header_text(&request, "x-name")?;
    let ext = header_text(&request, "x-ext").unwrap_or_default();
    let bytes = raw_body(&request)?.to_vec();
    let safe_name: String = name
        .chars()
        .map(|c| if c.is_control() || "\\/:*?\"<>|".contains(c) { '_' } else { c })
        .collect();
    let mut d = app.dialog().file().set_title("Export").set_file_name(&safe_name);
    if !ext.is_empty() && ext.chars().all(|c| c.is_ascii_alphanumeric()) {
        d = d.add_filter(ext.to_uppercase(), &[ext.as_str()]);
    }
    if let Some(s) = dialog_start(&state) {
        d = d.set_directory(s);
    }
    let Some(p) = d.blocking_save_file() else {
        return Ok(None);
    };
    let path = p.into_path().map_err(err)?;
    std::fs::write(&path, bytes).map_err(err)?;
    Ok(Some(path.to_string_lossy().to_string()))
}

// ---- mods --------------------------------------------------------------------------------------------------------------

fn mods_folder(state: &AppState) -> Res<PathBuf> {
    state
        .inner
        .lock()
        .unwrap()
        .settings
        .mods_folder()
        .ok_or_else(|| "No game folder is set. Choose the play folder first.".to_string())
}

/// Current `mods.ini` text ("" when there is none yet).
#[tauri::command]
pub fn mods_read_ini(state: State<'_, AppState>) -> Res<String> {
    let dir = mods_folder(&state)?;
    match std::fs::read_to_string(dir.join("mods.ini")) {
        Ok(t) => Ok(t),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(String::new()),
        Err(e) => Err(err(e)),
    }
}

#[derive(Deserialize)]
pub struct PackFile {
    pub path: String,
    /// Base64 of the file bytes.
    pub data: String,
}

/// Write a pack as `<mods>/<id>/...` and replace `<mods>/mods.ini` with `ini` (computed by the editor's own
/// `updateModsIni`, so comments and order survive). Returns the pack folder.
#[tauri::command]
pub fn mods_write_pack(state: State<'_, AppState>, id: String, files: Vec<PackFile>, ini: String) -> Res<String> {
    if !valid_mod_id(&id) {
        return Err(format!("'{id}' is not a valid mod id (letters, digits, . _ - only)."));
    }
    let mods = mods_folder(&state)?;
    let pack = mods.join(&id);
    let mut staged = Vec::new();
    for f in &files {
        let target = paths::join_pack_path(&pack, &f.path).ok_or_else(|| format!("bad pack path '{}'", f.path))?;
        let data = base64::engine::general_purpose::STANDARD.decode(f.data.as_bytes()).map_err(err)?;
        staged.push((target, data));
    }
    for (target, data) in staged {
        if let Some(dir) = target.parent() {
            std::fs::create_dir_all(dir).map_err(err)?;
        }
        std::fs::write(&target, data).map_err(|e| format!("{}: {e}", target.display()))?;
    }
    std::fs::write(mods.join("mods.ini"), ini).map_err(err)?;
    Ok(pack.to_string_lossy().to_string())
}

// ---- the game ----------------------------------------------------------------------------------------------------------

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LaunchInfo {
    pub pid: u32,
    pub exe: String,
    pub mods: String,
}

/// Start the game from the remembered play folder. Only the documented `DRAKAN_*` debugging variables can be set.
#[tauri::command]
pub fn game_launch(
    state: State<'_, AppState>,
    env: HashMap<String, String>,
    extra: Option<bool>,
) -> Res<LaunchInfo> {
    let extra = extra.unwrap_or(false);
    let (dir, mods, default_mods) = {
        let g = state.inner.lock().unwrap();
        let dir = g.settings.game_dir.clone().ok_or("No game folder is set. Choose the play folder first.")?;
        let mods = g.settings.mods_folder();
        (PathBuf::from(dir), mods, g.settings.game_dir.is_some() && g.settings.mods_dir.is_none())
    };
    let exe = paths::find_game_exe(&dir).ok_or_else(|| format!("{} has no drakan.exe", dir.display()))?;
    {
        let mut g = state.inner.lock().unwrap();
        let running = match g.child.as_mut() {
            Some(child) => child.try_wait().map_err(err)?.is_none(),
            None => false,
        };
        if extra {
            // A second instance (multiplayer test) only makes sense beside a running first one.
            if !running {
                return Err("Start the game first; an extra instance joins the running one.".into());
            }
            g.extras.retain_mut(|c| matches!(c.try_wait(), Ok(None)));
            if g.extras.len() >= 3 {
                return Err("Too many extra instances are running.".into());
            }
        } else {
            if running {
                return Err("The game is already running.".into());
            }
            g.child = None;
        }
    }
    let mut cmd = std::process::Command::new(&exe);
    cmd.current_dir(&dir)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    for (k, v) in &env {
        if allowed_env(k) && valid_env_value(v) {
            cmd.env(k, v);
        }
    }
    if let (Some(m), false) = (&mods, default_mods) {
        cmd.env("DRAKAN_MODS", m);
    }
    let child = cmd.spawn().map_err(|e| format!("could not start {}: {e}", exe.display()))?;
    let pid = child.id();
    if extra {
        state.inner.lock().unwrap().extras.push(child);
    } else {
        state.inner.lock().unwrap().child = Some(child);
    }
    Ok(LaunchInfo {
        pid,
        exe: exe.to_string_lossy().to_string(),
        mods: mods.map(|m| m.to_string_lossy().to_string()).unwrap_or_default(),
    })
}

#[tauri::command]
pub fn game_running(state: State<'_, AppState>) -> bool {
    let mut g = state.inner.lock().unwrap();
    match g.child.as_mut() {
        Some(c) => matches!(c.try_wait(), Ok(None)),
        None => false,
    }
}

/// Used by `lib.rs` to find the settings file.
pub fn settings_path(app: &AppHandle) -> PathBuf {
    app.path()
        .app_config_dir()
        .unwrap_or_else(|_| std::env::temp_dir().join("drakan-editor"))
        .join("settings.json")
}

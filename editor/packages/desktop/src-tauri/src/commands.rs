//! The commands the webview may call. Every path that is read or written is either chosen by the user in a native dialog
//! during this session, remembered from an earlier one (`settings.json`), or derived from the chosen game folder.

use crate::playpack;
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
        g.settings.recent_projects.clear();
        g.settings.save(&state.settings_file).map_err(err)?;
        g.settings.clone()
    };
    crate::menu::refresh(&app);
    Ok(out)
}

/// Remove a project folder that is gone from the recent list; returns the new settings.
#[tauri::command]
pub fn recent_project_forget(app: AppHandle, state: State<'_, AppState>, dir: String) -> Res<Settings> {
    let out = {
        let mut g = state.inner.lock().unwrap();
        if g.settings.forget_project(&dir) {
            g.settings.save(&state.settings_file).map_err(err)?;
        }
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
            "game" => ("Choose the game folder (the one with drakan.exe)", g.settings.game_dir.clone()),
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
            "{} has no drakan.exe. Choose the game folder.",
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
        .ok_or_else(|| "No game folder is set. Choose the game folder first.".to_string())
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

/// The page has written its unsaved work; the window may close now.
#[tauri::command]
pub fn close_ack(state: State<'_, AppState>) {
    state.close_ack.store(true, std::sync::atomic::Ordering::SeqCst);
}

/// Size in bytes of `<mods>/<id>.zip`, or null when it is not there (the editor skips a rewrite of the same pack).
#[tauri::command]
pub fn mods_zip_size(state: State<'_, AppState>, id: String) -> Res<Option<f64>> {
    if !valid_mod_id(&id) {
        return Err(format!("'{id}' is not a valid mod id (letters, digits, . _ - only)."));
    }
    let file = mods_folder(&state)?.join(format!("{id}.zip"));
    Ok(std::fs::metadata(file).ok().map(|m| m.len() as f64))
}

/// Write a finished pack zip as `<mods>/<id>.zip` (raw body) and replace `mods.ini` with the text of header `x-ini`
/// (base64). Used for the converted Order of the Flame pack, which is thousands of files: one zip, one transfer.
#[tauri::command]
pub fn mods_write_zip(state: State<'_, AppState>, request: Request<'_>) -> Res<String> {
    let id = header_text(&request, "x-id")?;
    if !valid_mod_id(&id) {
        return Err(format!("'{id}' is not a valid mod id (letters, digits, . _ - only)."));
    }
    let ini = header_text(&request, "x-ini")?;
    let mods = mods_folder(&state)?;
    std::fs::create_dir_all(&mods).map_err(err)?;
    // A Play pack is temporary: remember `mods.ini` as it is now so it can be put back when the game closes.
    if header_text(&request, "x-restore").is_ok_and(|v| v == "1") {
        let before = std::fs::read_to_string(mods.join("mods.ini")).unwrap_or_default();
        let dir = state.settings_file.parent().map(Path::to_path_buf).unwrap_or_default();
        playpack::save_backup(
            &dir,
            &playpack::Backup { mods: mods.to_string_lossy().to_string(), id: id.clone(), ini: before },
        )
        .map_err(err)?;
    }
    let target = mods.join(format!("{id}.zip"));
    let tmp = mods.join(format!("{id}.zip.part"));
    std::fs::write(&tmp, raw_body(&request)?).map_err(|e| format!("{}: {e}", tmp.display()))?;
    std::fs::rename(&tmp, &target).map_err(|e| format!("{}: {e}", target.display()))?;
    std::fs::write(mods.join("mods.ini"), ini).map_err(err)?;
    Ok(target.to_string_lossy().to_string())
}

// ---- projects ----------------------------------------------------------------------------------------------------------

/// The file that makes a folder a project.
const PROJECT_FILE: &str = "project.drakan.json";

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ProjectInfo {
    pub dir: String,
    pub name: String,
    /// Contents of the project file; null for a project that was just created.
    pub text: Option<String>,
}

fn project_info(dir: &Path, text: Option<String>) -> ProjectInfo {
    ProjectInfo {
        dir: dir.to_string_lossy().to_string(),
        name: dir.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default(),
        text,
    }
}

fn grant_project(app: &AppHandle, state: &AppState, dir: &Path) -> Res<()> {
    {
        let mut g = state.inner.lock().unwrap();
        g.granted_projects.insert(dir.to_string_lossy().to_lowercase());
        g.settings.push_project(dir);
        g.settings.save(&state.settings_file).map_err(err)?;
    }
    crate::menu::refresh(app);
    Ok(())
}

fn granted_project(state: &AppState, dir: &str) -> Res<PathBuf> {
    let key = dir.to_lowercase();
    let g = state.inner.lock().unwrap();
    if g.granted_projects.contains(&key) {
        Ok(PathBuf::from(dir))
    } else {
        Err("That project folder was not opened in this session; open it from File > Open Project.".into())
    }
}

fn read_project(dir: &Path) -> Res<String> {
    let f = dir.join(PROJECT_FILE);
    std::fs::read_to_string(&f).map_err(|e| {
        if e.kind() == std::io::ErrorKind::NotFound {
            format!("{} has no {PROJECT_FILE}; it is not a project folder.", dir.display())
        } else {
            format!("{}: {e}", f.display())
        }
    })
}

/// Ask where to put a new project and create its folder (`<chosen>/<name>`). The caller then writes the project file.
#[tauri::command]
pub async fn project_new(app: AppHandle, state: State<'_, AppState>, name: String) -> Res<Option<ProjectInfo>> {
    let folder = paths::folder_name(&name);
    let start = state
        .inner
        .lock()
        .unwrap()
        .settings
        .recent_projects
        .first()
        .and_then(|p| Path::new(p).parent().map(|p| p.to_path_buf()));
    let mut d = app.dialog().file().set_title("Choose where to create the project folder");
    if let Some(s) = start.filter(|s| s.is_dir()) {
        d = d.set_directory(s);
    }
    let Some(p) = d.blocking_pick_folder() else {
        return Ok(None);
    };
    let dir = p.into_path().map_err(err)?.join(folder);
    paths::check_new_project_dir(&dir, PROJECT_FILE)?;
    std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    grant_project(&app, &state, &dir)?;
    Ok(Some(project_info(&dir, None)))
}

/// Choose a project folder and read its project file.
#[tauri::command]
pub async fn project_open(app: AppHandle, state: State<'_, AppState>) -> Res<Option<ProjectInfo>> {
    let start = state.inner.lock().unwrap().settings.recent_projects.first().cloned();
    let mut d = app.dialog().file().set_title("Open a project folder (the one with project.drakan.json)");
    if let Some(s) = start.map(PathBuf::from).and_then(|s| s.parent().map(|p| p.to_path_buf())).filter(|s| s.is_dir()) {
        d = d.set_directory(s);
    }
    let Some(p) = d.blocking_pick_folder() else {
        return Ok(None);
    };
    let dir = p.into_path().map_err(err)?;
    let text = read_project(&dir)?;
    grant_project(&app, &state, &dir)?;
    Ok(Some(project_info(&dir, Some(text))))
}

/// Re-open a project from the recent list.
#[tauri::command]
pub async fn project_open_path(app: AppHandle, state: State<'_, AppState>, path: String) -> Res<ProjectInfo> {
    let known = state
        .inner
        .lock()
        .unwrap()
        .settings
        .recent_projects
        .iter()
        .any(|p| p.eq_ignore_ascii_case(&path));
    if !known {
        return Err("That project is not in the recent list.".into());
    }
    let dir = PathBuf::from(&path);
    let text = read_project(&dir)?;
    grant_project(&app, &state, &dir)?;
    Ok(project_info(&dir, Some(text)))
}

/// Save the project file in place (the request body is its text). The previous version is kept as `.bak`, and the new one
/// goes to a temp file that then replaces it, so a crash never leaves half a file.
#[tauri::command]
pub fn project_write(state: State<'_, AppState>, request: Request<'_>) -> Res<f64> {
    let dir = granted_project(&state, &header_text(&request, "x-dir")?)?;
    let bytes = raw_body(&request)?;
    let file = dir.join(PROJECT_FILE);
    if file.exists() {
        let _ = std::fs::copy(&file, dir.join(format!("{PROJECT_FILE}.bak")));
    }
    let tmp = dir.join(format!("{PROJECT_FILE}.tmp-editor"));
    std::fs::write(&tmp, bytes).map_err(err)?;
    std::fs::rename(&tmp, &file).map_err(|e| {
        let _ = std::fs::remove_file(&tmp);
        err(e)
    })?;
    let meta = std::fs::metadata(&file).map_err(err)?;
    Ok(modified_ms(&meta))
}

/// Write one file inside the project folder (`x-rel` is a validated relative path such as `mod/my-mod.zip`).
/// Returns the full path written.
#[tauri::command]
pub fn project_file_write(state: State<'_, AppState>, request: Request<'_>) -> Res<String> {
    let dir = granted_project(&state, &header_text(&request, "x-dir")?)?;
    let rel = header_text(&request, "x-rel")?;
    let target = paths::join_pack_path(&dir, &rel).ok_or_else(|| format!("bad project path '{rel}'"))?;
    if rel.eq_ignore_ascii_case(PROJECT_FILE) {
        return Err("The project file is written by Save.".into());
    }
    if let Some(parent) = target.parent() {
        std::fs::create_dir_all(parent).map_err(err)?;
    }
    std::fs::write(&target, raw_body(&request)?).map_err(|e| format!("{}: {e}", target.display()))?;
    Ok(target.to_string_lossy().to_string())
}

// ---- show in folder ----------------------------------------------------------------------------------------------------

/// The folders the shell may show: the project folders opened this session, the game folder and the mods folder.
fn reveal_roots(state: &AppState) -> Vec<PathBuf> {
    let g = state.inner.lock().unwrap();
    let mut roots: Vec<PathBuf> = g.granted_projects.iter().map(PathBuf::from).collect();
    roots.extend(g.settings.game_dir.as_ref().map(PathBuf::from));
    roots.extend(g.settings.mods_folder());
    roots
}

/// Show a folder in Explorer (Finder, the desktop's file manager). Only a folder the app already knows, or one inside
/// it, is accepted; anything else from the web view is refused.
#[tauri::command]
pub fn folder_reveal(state: State<'_, AppState>, path: String) -> Res<()> {
    let dir = paths::reveal_target(Path::new(&path), &reveal_roots(&state))
        .ok_or_else(|| "Only the project, game or mods folder can be shown.".to_string())?;
    #[cfg(target_os = "windows")]
    let program = "explorer";
    #[cfg(target_os = "macos")]
    let program = "open";
    #[cfg(not(any(target_os = "windows", target_os = "macos")))]
    let program = "xdg-open";
    // Not waited on: Explorer stays a separate process and its exit code means nothing.
    std::process::Command::new(program)
        .arg(&dir)
        .spawn()
        .map(|_| ())
        .map_err(|e| format!("{program}: {e}"))
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
    app: AppHandle,
    state: State<'_, AppState>,
    env: HashMap<String, String>,
    extra: Option<bool>,
) -> Res<LaunchInfo> {
    let extra = extra.unwrap_or(false);
    let (dir, mods, default_mods) = {
        let g = state.inner.lock().unwrap();
        let dir = g.settings.game_dir.clone().ok_or("No game folder is set. Choose the game folder first.")?;
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
    if env.get(paths::PLAYTEST_CARD).is_some_and(|v| v == "1") {
        let n = if extra { state.inner.lock().unwrap().extras.len() + 1 } else { 0 };
        let card = paths::playtest_card_dir(&std::env::temp_dir(), n);
        // The whole card folder: the Order of the Flame saves sit beside mc0 (mc0_ootf).
        if let Some(root) = card.parent().filter(|r| r.exists()) {
            std::fs::remove_dir_all(root).map_err(|e| format!("could not clear the test memory card: {e}"))?;
        }
        std::fs::create_dir_all(&card).map_err(|e| format!("could not make the test memory card: {e}"))?;
        cmd.env("DRAKAN_MEMCARD_DIR", &card);
    }
    if let (Some(m), false) = (&mods, default_mods) {
        cmd.env("DRAKAN_MODS", m);
    }
    let child = cmd.spawn().map_err(|e| format!("could not start {}: {e}", exe.display()))?;
    let pid = child.id();
    if !extra && playpack::backup_path(&config_dir(&state)).exists() {
        watch_play_pack(app.clone());
    }
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

fn config_dir(state: &AppState) -> PathBuf {
    state.settings_file.parent().map(Path::to_path_buf).unwrap_or_default()
}

/// When the game and its extra instances have closed, take the Play pack out and restore `mods.ini`.
fn watch_play_pack(app: AppHandle) {
    std::thread::spawn(move || loop {
        std::thread::sleep(std::time::Duration::from_secs(2));
        let state = app.state::<AppState>();
        let mut g = state.inner.lock().unwrap();
        let alive = |c: &mut std::process::Child| matches!(c.try_wait(), Ok(None));
        let main = g.child.as_mut().is_some_and(alive);
        g.extras.retain_mut(alive);
        if main || !g.extras.is_empty() {
            continue;
        }
        drop(g);
        let _ = playpack::restore(&config_dir(&state));
        return;
    });
}

/// Called when the editor starts: a Play pack left over from a session the editor did not outlive.
pub fn restore_play_pack_at_start(settings_file: &Path) {
    if let Some(dir) = settings_file.parent() {
        let _ = playpack::restore(dir);
    }
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

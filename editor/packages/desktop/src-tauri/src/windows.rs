//! The two applications of the desktop shell: the level/mod editor (`main`, index.html) and the save game editor
//! (`save`, save.html). Each opens the other in its own window; an action (`file`, `folder`) is passed to the save
//! editor as a `save-action` event, or in the URL hash when its window is created.

use tauri::{AppHandle, Emitter, Manager, WebviewUrl, WebviewWindowBuilder};

pub fn open(app: &AppHandle, which: &str, action: Option<&str>) -> Result<(), String> {
    let (label, page) = match which {
        "save" => ("save", "save.html"),
        "main" => ("main", "index.html"),
        _ => return Err(format!("unknown window {which}")),
    };
    if let Some(w) = app.get_webview_window(label) {
        let _ = w.unminimize();
        let _ = w.show();
        let _ = w.set_focus();
        if let Some(a) = action {
            let _ = app.emit_to(label, "save-action", a);
        }
        return Ok(());
    }
    // The window is declared in tauri.conf.json (`create: false`); an action rides in the URL hash.
    let mut conf = app
        .config()
        .app
        .windows
        .iter()
        .find(|w| w.label == label)
        .cloned()
        .ok_or_else(|| format!("no window {label} in tauri.conf.json"))?;
    conf.url = WebviewUrl::App(
        match action {
            Some(a) => format!("{page}#open={a}"),
            None => page.to_string(),
        }
        .into(),
    );
    let w = WebviewWindowBuilder::from_config(app, &conf)
        .map_err(|e| e.to_string())?
        .build()
        .map_err(|e| e.to_string())?;
    // The save editor has its own toolbar; the level editor's menu bar is not its menu.
    if label == "save" {
        let _ = w.remove_menu();
    }
    Ok(())
}

#[tauri::command]
pub fn window_open(app: AppHandle, which: String, action: Option<String>) -> Result<(), String> {
    open(&app, &which, action.as_deref())
}

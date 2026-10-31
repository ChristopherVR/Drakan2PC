//! Native menu bar. Items either act in the shell (`view-*`, `about`, `quit`, `recent-clear`) or are forwarded to the
//! webview as a `menu-action` event carrying the item id, which `packages/app/src/desktop.ts` maps to editor actions.

use crate::AppState;
use tauri::menu::{Menu, MenuBuilder, MenuItemBuilder, SubmenuBuilder};
use tauri::{AppHandle, Emitter, Manager, Wry};
use tauri_plugin_dialog::{DialogExt, MessageDialogKind};

fn item(app: &AppHandle, id: &str, text: &str, accel: Option<&str>) -> tauri::Result<tauri::menu::MenuItem<Wry>> {
    let mut b = MenuItemBuilder::with_id(id, text);
    if let Some(a) = accel {
        b = b.accelerator(a);
    }
    b.build(app)
}

pub fn build(app: &AppHandle) -> tauri::Result<Menu<Wry>> {
    let (recents, recent_projects) = app
        .try_state::<AppState>()
        .map(|s| {
            let g = s.inner.lock().unwrap();
            (g.settings.recent_isos.clone(), g.settings.recent_projects.clone())
        })
        .unwrap_or_default();

    let mut recent_proj = SubmenuBuilder::new(app, "Open Recent P&roject");
    if recent_projects.is_empty() {
        recent_proj =
            recent_proj.item(&MenuItemBuilder::with_id("recent-project-none", "(none yet)").enabled(false).build(app)?);
    }
    for (i, p) in recent_projects.iter().enumerate() {
        recent_proj = recent_proj.item(&item(app, &format!("recent-project:{i}"), p, None)?);
    }
    let recent_proj = recent_proj.build()?;

    let mut recent = SubmenuBuilder::new(app, "Open &Recent");
    if recents.is_empty() {
        recent = recent.item(&MenuItemBuilder::with_id("recent-none", "(none yet)").enabled(false).build(app)?);
    }
    for (i, p) in recents.iter().enumerate() {
        recent = recent.item(&item(app, &format!("recent:{i}"), p, None)?);
    }
    let recent = recent
        .separator()
        .item(&item(app, "recent-clear", "Clear recent lists", None)?)
        .build()?;

    let file = SubmenuBuilder::new(app, "&File")
        .item(&item(app, "project-new", "&New Project...", Some("CmdOrCtrl+Shift+N"))?)
        .item(&item(app, "project-open", "Open &Project...", Some("CmdOrCtrl+Shift+O"))?)
        .item(&recent_proj)
        .separator()
        .item(&item(app, "open-iso", "Open &ISO...", Some("CmdOrCtrl+O"))?)
        .item(&recent)
        .separator()
        .item(&item(app, "open-save", "Open &Save File...", None)?)
        .item(&item(app, "open-save-folder", "Open Memory &Card Folder...", None)?)
        .separator()
        .item(&item(app, "save-document", "&Save\tCtrl+S", None)?)
        .item(&item(app, "export-mod", "&Export...\tCtrl+E", None)?)
        .separator()
        .item(&item(app, "play", "&Play in Game", Some("F5"))?)
        .item(&item(app, "game-folder", "Game (Play) Folder...", None)?)
        .item(&item(app, "mods-folder", "Mods Folder...", None)?)
        .separator()
        .item(&item(app, "quit", "E&xit", None)?)
        .build()?;

    let edit = SubmenuBuilder::new(app, "&Edit")
        .item(&item(app, "edit-undo", "&Undo\tCtrl+Z", None)?)
        .item(&item(app, "edit-redo", "&Redo\tCtrl+Y", None)?)
        .separator()
        .item(&item(app, "edit-cut", "Cu&t\tCtrl+X", None)?)
        .item(&item(app, "edit-copy", "&Copy\tCtrl+C", None)?)
        .item(&item(app, "edit-paste", "&Paste\tCtrl+V", None)?)
        .separator()
        .item(&item(app, "edit-select-all", "Select &All\tCtrl+A", None)?)
        .item(&item(app, "edit-duplicate", "&Duplicate\tCtrl+D", None)?)
        .item(&item(app, "edit-delete", "De&lete\tDel", None)?)
        .build()?;

    let mut view = SubmenuBuilder::new(app, "&View")
        .item(&item(app, "tab-inspect", "&Files", None)?)
        .item(&item(app, "tab-level", "&Level Editor", None)?)
        .item(&item(app, "tab-save", "&Save Editor", None)?)
        .separator()
        .item(&item(app, "theme", "Toggle &Theme\tCtrl+Shift+L", None)?)
        .item(&item(app, "view-fullscreen", "&Full Screen", Some("F11"))?)
        .item(&item(app, "view-reload", "&Reload", Some("CmdOrCtrl+R"))?);
    if cfg!(debug_assertions) {
        view = view.item(&item(app, "view-devtools", "Developer &Tools", Some("F12"))?);
    }
    let view = view.build()?;

    let help = SubmenuBuilder::new(app, "&Help")
        .item(&item(app, "help", "&Keyboard Shortcuts\tF1", None)?)
        .item(&item(app, "about", "&About Drakan Editor", None)?)
        .build()?;

    MenuBuilder::new(app).items(&[&file, &edit, &view, &help]).build()
}

/// Rebuild the bar (the recent list changed).
pub fn refresh(app: &AppHandle) {
    if let Ok(m) = build(app) {
        let _ = app.set_menu(m);
    }
}

pub fn on_event(app: &AppHandle, id: &str) {
    let window = app.get_webview_window("main");
    match id {
        "quit" => app.exit(0),
        "view-fullscreen" => {
            if let Some(w) = window {
                let on = w.is_fullscreen().unwrap_or(false);
                let _ = w.set_fullscreen(!on);
            }
        }
        "view-reload" => {
            if let Some(w) = window {
                let _ = w.eval("location.reload()");
            }
        }
        #[cfg(debug_assertions)]
        "view-devtools" => {
            if let Some(w) = window {
                w.open_devtools();
            }
        }
        "recent-clear" => {
            if let Some(state) = app.try_state::<AppState>() {
                let mut g = state.inner.lock().unwrap();
                g.settings.recent_isos.clear();
                g.settings.recent_projects.clear();
                let _ = g.settings.save(&state.settings_file);
            }
            refresh(app);
        }
        "about" => {
            app.dialog()
                .message(format!(
                    "Drakan Editor {}\n\nLevel, save and mod editor for Drakan: The Ancients' Gates.\nReads your own disc image locally; nothing is uploaded and no game data is bundled.",
                    env!("CARGO_PKG_VERSION")
                ))
                .title("About Drakan Editor")
                .kind(MessageDialogKind::Info)
                .show(|_| {});
        }
        other => {
            let _ = app.emit("menu-action", other);
        }
    }
}

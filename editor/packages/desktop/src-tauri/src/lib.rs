//! Tauri shell around the editor web app (`editor/packages/app`). The shell adds native file access with a small, named
//! command set; the web app keeps working in a plain browser without it.

mod closing;
mod commands;
mod menu;
mod paths;
mod playpack;
mod store;
mod windows;

use std::collections::HashSet;
use std::path::PathBuf;
use std::process::Child;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use tauri::{Emitter, Manager};

pub struct Inner {
    pub settings: store::Settings,
    /// Lower-cased paths of saves the user picked or that were listed from a folder they picked this session.
    pub granted_saves: HashSet<String>,
    /// Lower-cased project folders the user created or opened (dialog or recent list) this session.
    pub granted_projects: HashSet<String>,
    pub child: Option<Child>,
    /// Extra instances started beside `child` (multiplayer tests).
    pub extras: Vec<Child>,
}

pub struct AppState {
    pub inner: Mutex<Inner>,
    pub settings_file: PathBuf,
    /// The main window has been asked to close once (the next request goes through).
    pub closing: AtomicBool,
    /// The page wrote its unsaved work and said so (`close_ack`).
    pub close_ack: AtomicBool,
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_window_state::Builder::default().build())
        .setup(|app| {
            let file = commands::settings_path(app.handle());
            commands::restore_play_pack_at_start(&file);
            let settings = store::Settings::load(&file);
            app.manage(AppState {
                inner: Mutex::new(Inner {
                    settings,
                    granted_saves: HashSet::new(),
                    granted_projects: HashSet::new(),
                    child: None,
                    extras: Vec::new(),
                }),
                settings_file: file,
                closing: AtomicBool::new(false),
                close_ack: AtomicBool::new(false),
            });
            // The standalone save editor (tauri.save.conf.json) has no level editor window and no menu bar.
            if app.config().app.windows.iter().any(|w| w.label == "main" && w.create) {
                app.set_menu(menu::build(app.handle())?)?;
            }
            Ok(())
        })
        .on_window_event(|window, event| {
            // File > Exit, Alt+F4 and the window's close button all arrive here: let the page save first.
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                if window.label() != "main" {
                    return;
                }
                let state = window.state::<AppState>();
                if state.closing.swap(true, Ordering::SeqCst) {
                    return;
                }
                api.prevent_close();
                state.close_ack.store(false, Ordering::SeqCst);
                let _ = window.emit("app-close-requested", ());
                let w = window.clone();
                std::thread::spawn(move || {
                    let state = w.state::<AppState>();
                    closing::wait_for_ack(&state.close_ack, closing::ACK_WAIT);
                    let _ = w.close();
                });
            }
        })
        .on_menu_event(|app, event| menu::on_event(app, event.id().as_ref()))
        .invoke_handler(tauri::generate_handler![
            commands::settings_get,
            commands::settings_forget,
            commands::recent_project_forget,
            commands::iso_pick,
            commands::iso_open,
            commands::folder_pick,
            commands::save_pick_file,
            commands::save_pick_folder,
            commands::save_read,
            commands::save_stat,
            commands::save_write,
            commands::file_save_as,
            commands::mods_read_ini,
            commands::mods_write_pack,
            commands::close_ack,
            commands::mods_zip_size,
            commands::mods_write_zip,
            commands::game_launch,
            commands::game_running,
            commands::project_new,
            commands::project_open,
            commands::project_open_path,
            commands::project_write,
            commands::project_file_write,
            commands::folder_reveal,
            windows::window_open,
        ])
        .run(tauri::generate_context!())
        .expect("error while running the Drakan editor");
}

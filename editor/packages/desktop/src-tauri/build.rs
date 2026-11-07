fn main() {
    // Only these commands exist for the webview; capabilities/default.json grants each one by name.
    const COMMANDS: &[&str] = &[
        "settings_get",
        "settings_forget",
        "iso_pick",
        "iso_open",
        "folder_pick",
        "save_pick_file",
        "save_pick_folder",
        "save_read",
        "save_stat",
        "save_write",
        "file_save_as",
        "mods_read_ini",
        "mods_write_pack",
        "mods_zip_size",
        "mods_write_zip",
        "game_launch",
        "game_running",
        "project_new",
        "project_open",
        "project_open_path",
        "project_write",
        "project_file_write",
        "folder_reveal",
        "window_open",
    ];
    tauri_build::try_build(
        tauri_build::Attributes::new()
            .app_manifest(tauri_build::AppManifest::new().commands(COMMANDS)),
    )
    .expect("failed to run tauri-build");
}

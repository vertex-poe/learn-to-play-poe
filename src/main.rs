#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use learn_to_play_poe1::config::{load_config, save_config, AppConfig};
use learn_to_play_poe1::tracker::{create_tracker, WindowTracker};
use tray_icon::{Icon, TrayIconBuilder, TrayIcon};
use tray_icon::menu::{Menu, MenuItem, MenuEvent};
use eframe::egui;
use std::process::{Command, Child, Stdio};
use std::io::Write;
use std::time::Instant;
use std::sync::{Arc, Mutex};

struct AppState {
    show_main_window: bool,
    show_settings: bool,
    exit_requested: bool,
}

struct MainApp {
    config: AppConfig,
    _tray_icon: Option<TrayIcon>,
    overlay_process: Option<Child>,
    show_main_window: bool,
    show_settings: bool,
    tracker: Box<dyn WindowTracker>,
    last_tracker_check: Instant,
    overlay_exe: std::path::PathBuf,
    was_visible: Option<bool>,
    last_window_pos: Option<egui::Pos2>,
    state: Arc<Mutex<AppState>>,
}

impl MainApp {
    fn new(cc: &eframe::CreationContext<'_>, config: AppConfig) -> Self {
        let tray_menu = Menu::new();
        let settings_i = MenuItem::new("Settings", true, None);
        let settings_id = settings_i.id().clone();
        let quit_i = MenuItem::new("Exit", true, None);
        let quit_id = quit_i.id().clone();
        
        tray_menu.append(&settings_i).unwrap();
        tray_menu.append(&quit_i).unwrap();

        let icon_bytes = include_bytes!("../assets/logo/vertex-icon.png");
        let image = image::load_from_memory(icon_bytes).unwrap().into_rgba8();
        let (width, height) = image.dimensions();
        let icon_rgba = image.into_raw();
        let icon = Icon::from_rgba(icon_rgba, width, height).unwrap();

        let tray_icon = TrayIconBuilder::new()
            .with_menu(Box::new(tray_menu))
            .with_tooltip("Learn to Play: Path of Exile 1")
            .with_icon(icon)
            .build()
            .unwrap();

        let mut overlay_exe = std::env::current_exe().unwrap_or_else(|_| std::path::PathBuf::from("."));
        overlay_exe.pop(); 
        
        #[cfg(windows)]
        overlay_exe.push("l2p-poe1-overlay.exe");
        #[cfg(not(windows))]
        overlay_exe.push("l2p-poe1-overlay");

        let state = Arc::new(Mutex::new(AppState {
            show_main_window: false,
            show_settings: false,
            exit_requested: false,
        }));

        let state_clone = state.clone();
        let ctx_clone = cc.egui_ctx.clone();

        std::thread::spawn(move || {
            let menu_channel = tray_icon::menu::MenuEvent::receiver();
            let tray_channel = tray_icon::TrayIconEvent::receiver();
            println!("Settings ID: {:?}, Quit ID: {:?}", settings_id, quit_id);
            loop {
                let mut changed = false;
                while let Ok(event) = menu_channel.try_recv() {
                    println!("Menu event: {:?}", event.id);
                    let mut st = state_clone.lock().unwrap();
                    if event.id == quit_id {
                        println!("Matched Quit ID!");
                        st.exit_requested = true;
                        changed = true;
                    } else if event.id == settings_id {
                        println!("Matched Settings ID!");
                        st.show_main_window = true;
                        st.show_settings = true;
                        changed = true;
                    }
                }
                while let Ok(event) = tray_channel.try_recv() {
                    if let tray_icon::TrayIconEvent::Click { button: tray_icon::MouseButton::Left, .. } = event {
                        let mut st = state_clone.lock().unwrap();
                        st.show_main_window = !st.show_main_window;
                        if !st.show_main_window {
                            st.show_settings = false;
                        }
                        changed = true;
                    }
                }
                if changed {
                    let st = state_clone.lock().unwrap();
                    let is_visible = st.show_main_window || st.show_settings;
                    
                    if st.exit_requested {
                        // Wake up eframe so update() can process the exit
                        ctx_clone.send_viewport_cmd(egui::ViewportCommand::Visible(true));
                    } else if is_visible {
                        ctx_clone.send_viewport_cmd(egui::ViewportCommand::OuterPosition(egui::pos2(200.0, 200.0)));
                        ctx_clone.send_viewport_cmd(egui::ViewportCommand::Visible(true));
                        ctx_clone.send_viewport_cmd(egui::ViewportCommand::Focus);
                    } else {
                        ctx_clone.send_viewport_cmd(egui::ViewportCommand::Visible(false));
                    }
                    ctx_clone.request_repaint();
                }
                std::thread::sleep(std::time::Duration::from_millis(50));
            }
        });

        Self {
            config,
            _tray_icon: Some(tray_icon),
            overlay_process: None,
            show_main_window: false,
            show_settings: false,
            tracker: create_tracker(),
            last_tracker_check: Instant::now() - std::time::Duration::from_secs(10), // force immediate check
            overlay_exe,
            was_visible: None,
            last_window_pos: None,
            state,
        }
    }

    fn sync_config(&mut self) {
        save_config(&self.config);
        let msg = serde_json::to_string(&self.config).unwrap();
        if let Some(child) = self.overlay_process.as_mut() {
            if let Some(stdin) = child.stdin.as_mut() {
                let _ = writeln!(stdin, "{}", msg);
            }
        }
    }
}

impl eframe::App for MainApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        let mut st = self.state.lock().unwrap();
        if st.exit_requested {
            self._tray_icon.take(); // force drop tray icon
            std::process::exit(0);
        }
        self.show_main_window = st.show_main_window;
        self.show_settings = st.show_settings;
        drop(st);

        let is_visible = self.show_main_window || self.show_settings;

        if is_visible {
            if let Some(rect) = ctx.input(|i| i.viewport().outer_rect) {
                if rect.min.x > -5000.0 {
                    self.last_window_pos = Some(rect.min);
                }
            }
        }

        // ALWAYS draw CentralPanel so the window is never black if it's visible
        egui::CentralPanel::default().show(ctx, |ui| {
            egui::menu::bar(ui, |ui| {
                ui.menu_button("File", |ui| {
                    if ui.button("Settings").clicked() {
                        self.show_settings = true;
                        ui.close_menu();
                    }
                });
            });
            ui.label("Hello world");
        });

        if self.show_settings {
            let mut close_settings = false;
            let mut config_changed = false;
            
            egui::Window::new("Settings")
                .open(&mut self.show_settings)
                .show(ctx, |ui| {
                    if ui.checkbox(&mut self.config.use_game_overlay, "Use game overlay").changed() {
                        config_changed = true;
                    }
                    ui.horizontal(|ui| {
                        ui.label("Windows Executable:");
                        if ui.text_edit_singleline(&mut self.config.windows_executable_name).changed() {
                            config_changed = true;
                        }
                    });
                    ui.horizontal(|ui| {
                        ui.label("Linux Executable:");
                        if ui.text_edit_singleline(&mut self.config.linux_executable_name).changed() {
                            config_changed = true;
                        }
                    });
                });
                
            if !self.show_settings {
                close_settings = true;
            }
            if config_changed || close_settings {
                self.sync_config();
                // Force an immediate tracker check if settings changed (e.g. toggled off)
                self.last_tracker_check = Instant::now() - std::time::Duration::from_secs(10);
            }
        }

        // Write state back
        let mut st = self.state.lock().unwrap();
        st.show_main_window = self.show_main_window;
        st.show_settings = self.show_settings;
        drop(st);

        // Check game window every 1 second
        if self.last_tracker_check.elapsed() > std::time::Duration::from_secs(1) {
            self.last_tracker_check = Instant::now();
            let executable_name = if cfg!(windows) {
                &self.config.windows_executable_name
            } else {
                &self.config.linux_executable_name
            };

            let _game_found = self.tracker.get_window_rect(executable_name).is_some();

            // Overlay launching temporarily disabled while focusing on the regular GUI
            /*
            if _game_found && self.config.use_game_overlay {
                if self.overlay_process.is_none() {
                    let mut child = Command::new(&self.overlay_exe)
                        .stdin(Stdio::piped())
                        .spawn()
                        .unwrap_or_else(|e| panic!("Failed to start overlay process at {:?}: {}", self.overlay_exe, e));
                    
                    let msg = serde_json::to_string(&self.config).unwrap();
                    if let Some(stdin) = child.stdin.as_mut() {
                        let _ = writeln!(stdin, "{}", msg);
                    }
                    self.overlay_process = Some(child);
                }
            } else {
                if let Some(mut child) = self.overlay_process.take() {
                    let _ = child.kill();
                    let _ = child.wait();
                }
            }
            */
        }

        // We do NOT need to ctx.request_repaint() constantly anymore!
        // The background thread will wake us up when a tray event happens.
        // This makes the app consume 0% CPU when minimized!
    }
}

impl Drop for MainApp {
    fn drop(&mut self) {
        if let Some(mut child) = self.overlay_process.take() {
            let _ = child.kill();
        }
    }
}

fn main() -> Result<(), eframe::Error> {
    let config = load_config();
    
    let icon_bytes = include_bytes!("../assets/logo/vertex-icon.png");
    let image = image::load_from_memory(icon_bytes).unwrap().into_rgba8();
    let (width, height) = image.dimensions();
    let icon_data = egui::IconData {
        rgba: image.into_raw(),
        width,
        height,
    };

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_visible(false)
            .with_taskbar(false) // Hide from taskbar for tray app
            .with_title("Learn to Play: Path of Exile 1")
            .with_icon(Arc::new(icon_data)),
        run_and_return: false,
        ..Default::default()
    };

    eframe::run_native(
        "Learn to Play: Path of Exile 1",
        options,
        Box::new(|cc| Ok(Box::new(MainApp::new(cc, config)))),
    )
}

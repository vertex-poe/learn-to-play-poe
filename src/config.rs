use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

#[derive(Debug, Deserialize, Serialize, Clone)]
#[serde(default)]
pub struct AppConfig {
    pub windows_executable_name: String,
    pub linux_executable_name: String,
    pub use_game_overlay: bool,
    pub auto_start_on_boot: bool,
    pub start_minimized: bool,
    pub minimize_to_tray: bool,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            windows_executable_name: "PathOfExile.exe".to_string(),
            linux_executable_name: "PathOfExile".to_string(),
            use_game_overlay: true,
            auto_start_on_boot: false,
            start_minimized: false,
            minimize_to_tray: true,
        }
    }
}

pub fn load_config() -> AppConfig {
    let config_path = get_config_path();
    
    if let Ok(content) = fs::read_to_string(&config_path) {
        if let Ok(config) = toml::from_str(&content) {
            return config;
        }
    }
    
    // Return default config if file is missing or invalid
    let default_config = AppConfig::default();
    
    // Optionally create the default config file if it doesn't exist
    if !config_path.exists() {
        if let Ok(toml_str) = toml::to_string_pretty(&default_config) {
            let _ = fs::write(&config_path, toml_str);
        }
    }
    
    default_config
}

fn get_config_path() -> PathBuf {
    // 1. Check if l2p-poe1.toml exists in the current directory (useful for cargo run)
    let current_dir_config = PathBuf::from("l2p-poe1.toml");
    if current_dir_config.exists() {
        return current_dir_config;
    }
    
    // 2. Fallback to alongside the executable
    let mut path = std::env::current_exe().unwrap_or_else(|_| PathBuf::from("."));
    path.pop(); // Remove the executable name
    path.push("l2p-poe1.toml");
    
    path
}

pub fn save_config(config: &AppConfig) {
    let config_path = get_config_path();
    if let Ok(toml_str) = toml::to_string_pretty(config) {
        let _ = fs::write(&config_path, toml_str);
    }
}

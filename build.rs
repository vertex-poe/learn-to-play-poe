use std::env;

fn main() {
    if env::var("CARGO_CFG_TARGET_OS").unwrap_or_default() == "windows" {
        let mut res = winres::WindowsResource::new();
        res.set_icon("assets/logo/vertex-icon.ico");
        res.compile().unwrap();
    }
}

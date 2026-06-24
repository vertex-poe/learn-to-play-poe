# Learn to Play PoE1 — task runner
# Install: winget install Casey.Just  |  scoop install just  |  cargo install just

set windows-shell := ["pwsh", "-NoLogo", "-Command"]

default_preset := if os() == "windows" { "windows-msvc" } else { "debug" }

default:
    @just --list

# Configure cmake
configure preset=default_preset:
    cmake --preset {{preset}}

# Build (configures first if needed)
[windows]
build preset=default_preset:
    -Remove-Item -Path "bin/l2p-poe1.exe" -ErrorAction SilentlyContinue
    cmake --preset {{preset}}
    cmake --build --preset {{preset}}
    New-Item -ItemType Directory -Force -Path "bin" | Out-Null
    Copy-Item -Path "build/{{preset}}/src/l2p-poe1.exe" -Destination "bin/" -Force -ErrorAction Stop
    Copy-Item -Path "build/{{preset}}/src/*.dll" -Destination "bin/" -Force -ErrorAction SilentlyContinue

[unix]
build preset=default_preset:
    cmake --preset {{preset}}
    cmake --build --preset {{preset}}

# Run tests (builds first)
test preset=default_preset: (build preset)
    ctest --preset {{preset}} --output-on-failure

# Configure + build + test in one shot
all preset=default_preset: (test preset)

# Build and run the app
[windows]
run preset=default_preset: (build preset)
    bin/l2p-poe1.exe

[unix]
run preset=default_preset: (build preset)
    build/{{preset}}/src/l2p-poe1

# Install to dist/ and run windeployqt / macdeployqt via cmake --install
package preset=default_preset: (build preset)
    cmake --install build/{{preset}}

# Linux: package as AppImage (requires linuxdeployqt on PATH)
package-linux preset=default_preset: (package preset)
    linuxdeployqt dist/{{preset}}/l2p-poe1 -appimage

# macOS: package .app bundle into a DMG (macdeployqt is included with Qt)
package-mac preset=default_preset: (build preset)
    macdeployqt build/{{preset}}/src/l2p-poe1.app -dmg

# Windows: build Inno Setup installer (requires ISCC on PATH, run after `just package`)
installer preset=default_preset: (package preset)
    ISCC installer/windows.iss

# Remove all build and dist artifacts
clean:
    cmake -E rm -rf build dist

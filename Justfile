# Learn to Play PoE1 — task runner
# Install: winget install Casey.Just  |  scoop install just  |  cargo install just

set windows-shell := ["pwsh", "-NoLogo", "-Command"]

default_preset := if os() == "windows" { "windows-mingw" } else { "debug" }

default:
    @just --list

# Configure cmake
configure preset=default_preset:
    cmake --preset {{preset}}

# Build (configures first if needed)
build preset=default_preset:
    cmake --preset {{preset}}
    cmake --build --preset {{preset}}

# Run tests (builds first)
test preset=default_preset: (build preset)
    ctest --preset {{preset}} --output-on-failure

# Configure + build + test in one shot
all preset=default_preset: (test preset)

# Build and run the app
run preset=default_preset: (build preset)
    build/{{preset}}/src/l2p-poe1.exe

# Remove all build and dist artifacts
clean:
    cmake -E rm -rf build dist

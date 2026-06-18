# Learn to Play PoE1 — task runner
# Install: winget install Casey.Just  |  scoop install just  |  cargo install just

set windows-shell := ["pwsh", "-NoLogo", "-Command"]

default:
    @just --list

# Configure cmake (default: debug preset)
configure preset="debug":
    cmake --preset {{preset}}

# Build (configures first if needed)
build preset="debug":
    cmake --preset {{preset}}
    cmake --build --preset {{preset}}

# Run tests (builds first)
test preset="debug": (build preset)
    ctest --preset {{preset}} --output-on-failure

# Configure + build + test in one shot
all preset="debug": (test preset)

# Build and run the app
run preset="debug": (build preset)
    build/{{preset}}/src/l2p-poe1.exe

# Remove all build and dist artifacts
clean:
    cmake -E rm -rf build dist

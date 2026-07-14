<!-- .claude/CLAUDE.md (markdown) -->

# Project Guidelines

## Commits

- Only commit when the user explicitly says "commit"
- Only commit immediately after that message — do not commit after subsequent prompts even if they seem related

## Build Verification

- Always run `just test` after making code changes
- Do not ask the user to test until the tests pass

## Deferred Work

- If something is deferred or tabled for later implementation/fixing instead of being done now, add it to `ROADMAP.md` — don't just leave it unmentioned in code or conversation
- Applies to both `l2p-poe` and `poe-info-service`

## Tests

- When functionality is added, changed, or removed, the associated tests must be added, changed, or removed to match, in the same change
- Applies to both `l2p-poe` and `poe-info-service`

## The `_reference/` directory

`_reference/` holds external reference material (e.g. third-party API docs), not this project's own documentation — it's gitignored entirely, confirming it's local-only and not part of the tracked codebase. Never edit files under `_reference/`. If something learned while working (e.g. a clarified API requirement) needs to be written down, put it in this project's own tracked docs instead (e.g. `poe-info-service/docs/`, `CONTRIBUTING.md`) — link to `_reference/` files rather than editing them.

## The `bin/` directory

`bin/` is staged only by `just build` / `just run` and is reserved for the user's own manual acceptance testing — it may hold their real config (`l2p-poe.toml`) and a live-running instance at any time. Never launch, edit config in, or write log/debug output to anything under `bin/`. For manual repro, debugging, or log-tracing, run the binary straight from the build tree instead (e.g. `build/<preset>/src/l2p-poe.exe`), which uses its own isolated config.

## Bash commands

- Avoid prepending `cd <dir> &&` to a command — the shell's working directory already persists across commands, and a `cd`-prefixed compound command won't match an allowlisted prefix rule (e.g. `Bash(go build *)`), causing an unnecessary permission prompt. Prefer a tool's own directory flag instead (e.g. `go build -C poe-info-service ./...`, `git -C poe-info-service log`) or just rely on the persisted working directory.

## Task Runner

Use `just` tasks whenever possible instead of raw commands:

| Task | Use instead of |
|------|---------------|
| `just build` | `cmake --preset ... && cmake --build ...` |
| `just test` | `ctest --preset ... -LE perf` |
| `just test-all` | `ctest --preset ...` |
| `just test-perf` | running perf tests manually |
| `just run` | running the app binary directly |
| `just clean` | `cmake -E rm -rf build dist` |

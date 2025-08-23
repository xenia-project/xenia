# Repository Guidelines

## Project Structure & Module Organization
- `src/xenia/`: Core C++20 sources. Key modules include `app/`, `cpu/`, `gpu/`, `kernel/`, `apu/`, `hid/`, `ui/`, `vfs/`.
- `src/xenia/base/testing/`: Unit tests (Catch2).
- `docs/`: Build and style docs (`docs/building.md`, `docs/style_guide.md`).
- `tools/`: Build tooling (Premake, helpers). The `xb` launcher symlinks to `xenia-build.py`.
- `third_party/`: Vendored dependencies.
- `assets/`: Icons and other assets.
- Build outputs: `build/` (generated projects, `build/bin/...`).

## Build, Test, and Development Commands
- Setup: `./xb setup` — initialize toolchains and submodules.
- Build: `./xb build --config=Release` — compile (use `Checked`/`Debug` as needed).
- IDE files: `./xb premake` or `./xb devenv` — generate/open projects (VS/Xcode/CMake).
- Format: `./xb format` — run clang-format over the tree.
- Tests: `./xb test` — run unit tests; GPU tests via `./xb gputest`.
- Update: `./xb pull` — fetch/rebase and refresh submodules/projects.
Notes: Windows requires Visual Studio 2022; Linux uses Clang 19 and up-to-date Vulkan drivers.

## Coding Style & Naming Conventions
- clang-format (Google style). Run `./xb format` before committing.
- 2-space indentation, LF endings, ~80-column preference.
- Follow Google C++ naming; attribute TODOs as `// TODO(yourgithubname): ...`.
- Sort includes per the style guide; avoid tabs. See `docs/style_guide.md`.

## Testing Guidelines
- Framework: Catch2 (`third_party/catch`).
- Location & names: place tests near the code or in `src/xenia/*/testing/`, named `*_test.cc`.
- Run locally with `./xb test`; keep tests deterministic and focused.
- Add GPU tests only when relevant and guard with appropriate configurations.

## Commit & Pull Request Guidelines
- Message style: concise, imperative, and scoped, e.g., `[GPU] Fix index buffer overrun` or `[Kernel] Guard XAM attach path`.
- Keep history clean; each commit builds and passes format/tests.
- PRs: include a clear description, reproduction steps, linked issues, and platform details (OS, GPU/driver, backend). Attach logs when investigating runtime issues (`--log_file=stdout` for console output).
- Legal/ethics: do not use or reference Xbox XDK materials; never include game content. Follow `.github/CONTRIBUTING.md`.

## Security & Configuration Tips
- Prefer `--flagfile=flags.txt` for repeatable runs. Avoid hard-coded game IDs or per-title hacks in code; use configuration variables instead.
- On Windows debugging, set `Command` to `$(SolutionDir)$(TargetPath)` and `Working Directory` to `$(SolutionDir)..\..`.


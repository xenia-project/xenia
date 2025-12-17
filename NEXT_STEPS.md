## Baseline alignment plan (arm64-all → metal-backend-clean-msc)

Goal: make `metal-backend-clean-msc` match the known-good ARM64/mac baseline behavior (tests pass) before touching Metal-specific code.

### Rules
- Do **not** run `git reset --hard` / `git clean` in submodules without explicit confirmation.
- Write build + test output to `scratch/logs/`.
- After each cherry-pick, run:
  - `./xb premake`
  - `./xb build --target=xenia-base-tests`
  - `./xb build --target=xenia-cpu-tests`
  - `./xb build --target=xenia-cpu-ppc-tests`
  - Run the binaries:
    - `build/bin/Mac/Checked/xenia-base-tests`
    - `build/bin/Mac/Checked/xenia-cpu-tests`
    - `build/bin/Mac/Checked/xenia-cpu-ppc-tests`

### Step 0 — Preconditions
- [ ] On `metal-backend-clean-msc`, ensure working tree is clean (no unintended staged changes).
- [ ] Confirm `scratch/logs/` exists.

### Step 1 — Tests-only macOS fix
Cherry-pick:
- `3d72872ea` `[Testing] Fix memory_test.cc for macOS ARM64`

Checklist:
- [ ] `git cherry-pick 3d72872ea`
- [ ] Rebuild + run the 3 test binaries

### Step 2 — POSIX/mac memory mapping correctness
Cherry-pick:
- `6a7341ec9` `[Base/Memory] Consolidate mac path into POSIX ... MAP_SHARED + nullptr on failure`

Checklist:
- [ ] `git cherry-pick 6a7341ec9`
- [ ] Resolve conflicts (prefer `arm64-all` mapping semantics)
- [ ] Rebuild + run the 3 test binaries

### Step 3 — A64 PPC-tests correctness
Cherry-pick:
- `1380e02d0` `A64: fix ResolveFunction arg; tighten guard; ... Verified xenia-cpu-ppc-tests passes`

Checklist:
- [ ] `git cherry-pick 1380e02d0`
- [ ] Resolve conflicts in `src/xenia/cpu/backend/a64/*`
- [ ] Rebuild + run the 3 test binaries

### Step 4 — Capstone Xcode include-path fix (partial pick)
Cherry-pick from baseline build fix commit:
- `f116bccd4` but **only** the `third_party/capstone.lua` hunk.
- Do **not** bring in the `premake5.lua` changes (Metal branch already has macOS warning handling).

Checklist:
- [ ] Apply `f116bccd4` (no-commit), drop `premake5.lua` changes, commit only `third_party/capstone.lua`
- [ ] Rebuild + run the 3 test binaries

### Step 5 — Optional QoL follow-ups
Cherry-pick:
- `002b6520f` `[CPU/PPC Tests] Remove stray working-directory print; keep existing test discovery logs`
- `c56ff351f` `[Memory] Demote membase initialization logs to debug on macOS`

Checklist:
- [ ] `git cherry-pick 002b6520f`
- [ ] `git cherry-pick c56ff351f`
- [ ] Rebuild + run the 3 test binaries

### Notes / failures
- Record any failures and the log file name(s) in this section.

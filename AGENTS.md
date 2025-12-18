# AGENTS.md

Guidelines for AI agents working on this codebase.

## Project Overview

**Xenia** is an Xbox 360 emulator. This fork targets **ARM64 macOS** with a native Metal GPU backend.

- **Origin**: Forked from [wunkolo's Xenia fork](https://github.com/Wunkolo/xenia/tree/arm64-windows) which implemented the A64 (ARM64) CPU backend
- **A64 Backend**: Uses [Oaknut](https://github.com/merryhime/oaknut) for ARM64 assembly emission (analogous to xbyak for x64)
- **Current Focus**: Metal GPU backend via `xenia-gpu-metal-trace-dump` target
- **Status**: A64 backend passes all CPU tests; Metal backend is early WIP
- **Why Metal**: MoltenVK (Vulkan-on-Metal) is NOT supported due to primitive restart issues and other Vulkan feature gaps
- **Shader Pipeline**: DXBC → DXIL conversion uses `dxbc2dxil` from `third_party/DirectXShaderCompiler` (see `third_party/DirectXShaderCompiler/build_dxilconv_macos.sh`, or set `DXBC2DXIL_PATH`)

### Branch Structure

| Branch | Purpose |
|--------|---------|
| `master` | wunkolo's upstream A64 backend |
| `arm64-all` | macOS ARM64 platform fixes (64 commits on top of master) |
| `metal-backend-clean-msc` | Current working branch for Metal GPU backend |

## Build Commands

All builds use the `xb` Python script. Output goes to `build/bin/Mac/<Config>/`.

```bash
# Initial setup (submodules + premake)
./xb setup

# Regenerate project files
./xb premake

# Build specific target (default config: debug)
./xb build --target=<target_name>

# Build release
./xb build --target=<target_name> --config=release

# Build Metal shaders
./xb buildshaders --target=metal

# Format code before committing
./xb format
```

### Key Targets

| Target | Purpose |
|--------|---------|
| `xenia-gpu-metal-trace-dump` | **Primary focus** - GPU trace replay for Metal backend testing |
| `xenia-base-tests` | Platform/base utility tests |
| `xenia-cpu-tests` | HIR/CPU operation tests |
| `xenia-cpu-ppc-tests` | PPC instruction tests (167 assembly tests) |
| `xenia-vfs-tests` | Virtual filesystem tests |

### Test Execution

```bash
# Run built test binary directly
./build/bin/Mac/Checked/xenia-base-tests
./build/bin/Mac/Checked/xenia-cpu-ppc-tests
./build/bin/Mac/Checked/xenia-cpu-tests

# GPU trace dump (requires trace file)
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump <trace_file>
```

Reference traces are in `testdata/reference-gpu-traces/traces/`:
- `title_414B07D1_frame_589.xenia_gpu_trace` (small, good for testing)
- `title_414B07D1_frame_6543.xenia_gpu_trace` (large)

## Directory Structure

```
src/xenia/
├── cpu/backend/a64/      # ARM64 JIT backend (Oaknut-based)
├── cpu/backend/x64/      # x86_64 JIT backend (xbyak-based)
├── gpu/metal/            # Metal GPU backend (WIP)
├── gpu/vulkan/           # Vulkan GPU backend
├── gpu/d3d12/            # D3D12 GPU backend (Windows)
├── ui/metal/             # Metal UI layer
├── kernel/               # Xbox kernel emulation
└── base/                 # Platform utilities

third_party/
├── oaknut/               # ARM64 assembler
├── xbyak/                # x64 assembler
├── metal-cpp/            # C++ Metal bindings
└── metal-shader-converter/  # Apple DXIL→Metal IR converter
```

## Agent Guidelines

### Shared Code Policy

**Do not modify shared/upstream code** unless absolutely necessary. The
following directories contain functional code from upstream Xenia that works
correctly on other platforms:

- `src/xenia/gpu/*.cc` (except `src/xenia/gpu/metal/`)
- `src/xenia/cpu/` (except `src/xenia/cpu/backend/a64/`)
- `src/xenia/kernel/`
- `src/xenia/base/` (except `*_posix.cc` and `*_mac.cc` files)

If a bug appears to be in shared code, investigate whether the issue is
actually in the platform-specific (Metal, A64) code that calls it. The D3D12
and Vulkan backends work correctly - use them as reference implementations.

### Documentation-Driven Development

For complex multi-step tasks, maintain documentation in `NEXT_STEPS.md`:

1. **Before starting work**: Document the problem, root cause analysis, and
   implementation plan with checkboxes
2. **During implementation**: Update checkboxes as steps are completed
3. **After completion**: Update status and move to next phase

This ensures context is preserved across sessions and provides a clear record
of what was attempted and why.

**NEXT_STEPS.md structure**:
```markdown
## Phase N: [Current Focus] (STATUS)

### Problem Description
[What's broken and how it manifests]

### Root Cause Analysis
[Why it's broken - be specific about code paths]

### Implementation Checklist
- [ ] Step 1
- [ ] Step 2
...

### Reference Information
[Relevant APIs, code locations, test commands]
```

### Use scratch/ for All Output

The `scratch/` directory is gitignored. Use it for:
- Build logs
- Test output
- Trace run results
- Any temporary files

```bash
# Example directory structure
scratch/
├── logs/           # Build and test logs
├── gpu/            # GPU debugging output
└── metal_trace_runs/  # Trace dump results
```

### Build and Test Pattern

**Do not dump full build output to context.** Always redirect to scratch/ and grep for issues:

```bash
# Build with log capture
./xb build --target=xenia-gpu-metal-trace-dump 2>&1 | tee scratch/logs/build.log

# Check for errors only
grep -E "error:" scratch/logs/build.log

# Check for warnings (if needed)
grep -E "warning:" scratch/logs/build.log | head -20
```

```bash
# Run tests with log capture
./build/bin/Mac/Checked/xenia-cpu-ppc-tests 2>&1 > scratch/logs/test.log

# Check results summary
grep -E "(PASSED|FAILED|passed|failed)" scratch/logs/test.log
tail -20 scratch/logs/test.log
```

```bash
# GPU trace dump
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
    testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace \
    2>&1 | tee scratch/logs/trace.log

# Check for crashes or errors
grep -iE "(error|exception|crash|assert)" scratch/logs/trace.log
```

### Code Quality Requirements

**No shortcuts. No workarounds. Complete implementations only.**

- **No stub implementations** - If functionality is needed, implement it fully
- **No warning silencing** - Fix the underlying issue, do not use `#pragma warning` or `-Wno-*`
- **No "temporary" workarounds** - These become permanent; solve problems correctly
- **No game-specific hacks** - Xenia emulates the Xbox 360 console, not individual games

If something cannot be fully implemented:
1. Document with `// TODO(username): description` per style guide
2. Explain what's missing and why
3. Reference any blocking issues

### Code Style

- Run `./xb format` before committing
- Follow [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- 80 column line limit, 2-space indentation, no tabs
- See `docs/style_guide.md` for full details

## Git Management

### Commit Messages

- **Be informative and professional**
- **No emojis**
- Explain the "why", not just the "what"
- Reference components with brackets: `[Metal]`, `[A64]`, `[Build]`, `[Base]`

Good examples:
```
[Metal] Implement texture cache format conversion for DXT1/DXT5

[A64] Fix PACK_FLOAT16_2 sentinel value handling for Xbox 360 compatibility

[Build] Fix premake bootstrap for ARM64 on Linux and macOS
```

Bad examples:
```
Fix stuff
WIP
Update metal_command_processor.cc
```

### When to Commit

- Commit logical units of work that compile and run
- Do not batch unrelated changes
- Do not commit broken/incomplete code to main branches

### Clean History

- Individual commits must compile on their own
- Use `git rebase` to clean up "fix" commits before merging
- Keep commits atomic and focused

## Documentation

| File | Content |
|------|---------|
| `docs/building.md` | Build instructions per platform |
| `docs/cpu.md` | JIT architecture (x64 and A64 backends) |
| `docs/gpu.md` | GPU emulation, shader tools, trace viewer |
| `docs/style_guide.md` | Code formatting and style rules |
| `docs/kernel.md` | Kernel shim implementation |
| `.github/CONTRIBUTING.md` | Contribution guidelines, legal requirements |

## Quick Reference

```bash
# Full rebuild cycle
./xb premake && ./xb build --target=xenia-gpu-metal-trace-dump 2>&1 | tee scratch/logs/build.log && grep -E "error:" scratch/logs/build.log

# Run all CPU tests
./build/bin/Mac/Checked/xenia-base-tests && ./build/bin/Mac/Checked/xenia-cpu-ppc-tests && ./build/bin/Mac/Checked/xenia-cpu-tests

# Test Metal trace dump
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace 2>&1 | tee scratch/logs/trace.log
```

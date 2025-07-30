# AGENTS – Operating gemini & gemini‑CLI within the Xenia Codebase

> This guide is geared toward large‑language‑model agents (e.g. OpenAI gemini) that manipulate the Xenia Xbox 360 emulator code.\
> All paths are given relative to the repository root unless noted.

## 1. High‑Level Flow

1. **Repo Link** – `https://github.com/wmarti/xenia-mac.git`
2. **Bootstrap** – run `xb setup` to setup build environment
3. **Develop** – edit code, ensuring it passes `xb format` and `xb lint`.
4. **Build** – `xb build [--config=debug]`.
5. **Test** – `./build/bin/Mac/Checked/xenia-base-tests or whatever target we are building`.

## 2. Repository Map

| Directory                | Purpose                                                                |
| ------------------------ | ---------------------------------------------------------------------- |
| `src/xenia/cpu`          | PowerPC front‑end (disassembler, interpreter, JIT).                    |
| `src/xenia/gpu`          | GPU emulation (command processors, shader translators, texture cache). |
| `src/xenia/kernel`       | Xbox 360 kernel syscall stubs and emulated drivers.                    |
| `src/xenia/ui`           | Cross‑platform windowing, input, and graphics helpers.                 |
| `src/xenia/app`          | Application entry point (`xenia_main.cc`) and flag definitions.        |
| `third_party/`           | External deps (Vulkan SDK, GoogleTest, stb, etc.).                     |
| `docs/`                  | Developer documentation (`building.md`, `style_guide.md`, …).          |
| `tools/`                 | Helper scripts and diagnostic utilities.                               |
| `xb`, `xb.ps1`, `xb.bat` | Python launcher wrapping Premake + Ninja/MSBuild workflows.            |

## 3. Build & Environment

### 3.1 Windows (primary)

- **OS**: Windows 7 or newer (Windows 11 preferred).
- **Compiler**: Visual Studio 2022/2019/2017 **with** MSVC v142 toolset.
- **SDK**: Windows 11 SDK 10.0.22000.0+.
- **Python** 3.6 +.
- Typical bootstrap:

```bash
git clone https://github.com/wmarti/xenia-mac.git
cd xenia
xb setup          # fetch deps + generate projects
xb build          # Debug build (add --config=release for Release)
xb devenv         # open generated VS solution 'xenia-app'
```

For debugging inside VS, set **Command** to `$(TargetPath)` and **Working Directory** to `$(SolutionDir)..\..`. Pass flags in **Command Arguments** (e.g. `--emit_source_annotations --log_file=stdout game.xex`).

### 3.2 Linux (experimental)

- LLVM/Clang 9 + (build uses `xb build` with Make).
- Install dev packages: `libgtk-3-dev libx11-dev libvulkan-dev libsdl2-dev ...`
- `xb premake --devenv=cmake` can generate CLion projects.

## 4. Coding Standards

- **clang‑format + Google style**, 80‑column limit, 2‑space indents.
- Run `xb format` before committing; CI runs `xb lint --all`.
- Comments punctuated; TODO lines attributed (`// TODO(yourname): …`).
- See `docs/style_guide.md` for full rules.

## 5. Configuration & Runtime Flags

- First run creates `xenia.config.toml` under `Documents\\Xenia`.
- If a `portable.txt` file is present (or using Canary builds) the config lives next to `xenia.exe`.
- Useful flags:
  | Flag                           | Description                             |
  | ------------------------------ | --------------------------------------- |
  | `--log_file=stdout`            | stream logs to console rather than file |
  | `--flagfile=flags.txt`         | load flags from a file                  |
  | `--gpu=vulkan` / `--gpu=d3d12` | choose backend                          |
  | `--emit_source_annotations`    | show JIT code with helpful markers      |

**NOTE** The Vulkan backend is NOT complete or fully implemented, we can't rely on it.

## 6. Key Code Areas

| Component                 | Entry file(s)                                    | Notes                                                   |
| ------------------------- | ------------------------------------------------ | ------------------------------------------------------- |
| **App**                   | `src/xenia/app/xenia_main.cc`                    | Parses flags, sets up window, launches emulation loop.  |
| **GPU D3D12**             | `src/xenia/gpu/d3d12/d3d12_command_processor.cc` | Core GPU command stream interpreter for D3D12 backend.  |
| **Shader Translator**     | `src/xenia/gpu/dxbc_shader_translator.cc`        | Translates Xbox 360 microcode to DXBC for D3D12.        |
| **Texture Cache**         | `src/xenia/gpu/d3d12/texture_cache.cc`           | Manages RAM‑to‑VRAM lifetime; common hotspot for leaks. |
| **CPU JIT**               | `src/xenia/cpu/ppc_*.cc`                         | Lifting PowerPC opcodes to IR + LLVM.                   |
| **Microcode definitions** | `src/xenia/gpu/ucode.h`                          | C structs mirroring GPU packet formats.                 |

## 7. Typical Agent Tasks

- **Refactor / bug‑fix** – e.g. deduplicate textures in `texture_cache.cc`.
- **Add opcode** – implement missing PPC op in `cpu/` and update tests.
- **Port backend** – extend `ui/` and `gpu/` providers for Vulkan improvements.
- **Documentation** – keep `docs/` in sync; update Game Compatibility wiki.

## 8. Example gemini‑cli Prompts

```text
# Locate all TODOs that mention 'memexport' in D3D12 backend
gemini grep 'TODO.*memexport' src/xenia/gpu/d3d12

# Generate a patch to flush the constant buffer cache every frame
gemini fix src/xenia/gpu/d3d12/command_processor.cc \
  --describe "Add per‑frame constant buffer flush to avoid overflows"

# Run the full unit test suite after applying current staged changes
gemini test
```

## 9. Etiquette & Legal

- Xenia is for **research and preservation**; sharing pirated games is forbidden.
- Discuss development in `#dev` on Discord; stay on topic and follow the Google C++ style.
- Large or controversial changes should start with a draft PR and discussion.

---

*Last updated: 2025‑07‑29*

---

## 10. ARM64 **macOS** Port Roadmap

> Goal: run retail titles natively on Apple Silicon Macs (M1/M2/M3) using the new **a64** JIT backend and Metal/Vulkan graphics.
>
> Current status: `xenia-base-tests` green on macOS **except** fixed‑address memory tests; `xenia-cpu-ppc-tests` in progress with Wunkolo’s **a64** branch.
>
> Key upstream work we’ll build on:
>
> | Area                                      | Source                                                                                                                                                                                                                                                             | Notes                                                                          |
> | ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------ |
> | **ARM64 build scaffolding (Windows)**     | PR #2258 “Add Windows ARM64 support” by Wunkolo ([github.com](https://github.com/xenia-project/xenia/pull/2258))                                                                                                                                                   | Splits platform/arch in build scripts; useful template for macOS arch flagging |
> | **a64 CPU backend**                       | PR #2259 “Implement an ARM64 backend” ([github.com](https://github.com/xenia-project/xenia/pull/2259))                                                                                                                                                             | 140+ commits; passes core CPU unit tests on ThinkPad X13s                      |
> | **MSL Shader Path**                       | Triang3l comment (Jun 16 2022) – most XeSL shaders compile to **Metal** with commit `166be46` ([github.com](https://github.com/xenia-project/xenia/issues/596?utm_source=chatgpt.com))                                                                             | Removes dependency on MoltenVK for basic GPU bring‑up                          |
> | **Canary fixes & CI**                     | xenia‑canary repo: continuous Vulkan/Metal experiments & MoltenVK integration ([github.com](https://github.com/xenia-canary/xenia-canary?utm_source=chatgpt.com))                                                                                                  |                                                                                |
> | **Fixed‑address guest memory discussion** | Issue #85 (4 GB mapping trick) – hard‑coded 0x7FFF0000 range is Windows‑centric ([github.com](https://github.com/xenia-project/xenia/issues/85?utm_source=chatgpt.com))                                                                                            |                                                                                |
> | **Apple VM constraints**                  | Mach VM docs – submaps occupy low 4 GB; `vmmap` shows shared cache at 0x00000000‑0x100000000 ([developer.apple.com](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/ManagingMemory/Articles/VMPages.html?utm_source=chatgpt.com)) |                                                                                |

### 10.1 Architectural blockers (macOS)

1. **Guest memory in <4 GB range** – macOS reserves large chunks (dyld shared cache, commpage) below 0x100000000; `mach_vm_allocate(..., VM_FLAGS_FIXED)` fails ([developer.apple.com](https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/KernelProgramming/vm/vm.html?utm_source=chatgpt.com)).
   - *Mitigation*: fall back to **dynamic chunks** + table lookup. Create `src/xenia/base/memory_mac.cc` with an `AddressSpace` class that lazily `mmap`s / `mach_vm_allocate`s 64 MiB slabs and stores host pointers in a 65 536‑entry page table (64 MiB × 64 K = 4 GB).
   - Update JIT memory helpers: replace fixed `reinterpret_cast<T*>(guest_base + offset)` with `LookupGuestPtr(offset)` inline or moved to A64 helper macro.
2. **16 KiB page size** on Apple Silicon breaks hard‑coded 4 KiB assumptions (aligned heaps, GPU texture upload). Add `kSystemPageSize` constant detected at runtime via `vm_page_size`.
3. **SysV/Mach‑O calling convention** differs for JIT trampolines (x8 = “link register”, x16/x17 = intra‑procedural scratch). Audit `a64_emit.cc` prolog/epilog macros.
4. **Metal vs Vulkan** – initial bring‑up will rely on **MoltenVK**; later switch translator to emit MSL directly (use Triang3l’s pipeline).
5. **Thread‑local range registers** – Darwin’s `pthread_jit_write_protect_np` & `pthread_jit_base_address_np` must guard executable pages when self‑modifying.

### 10.2  Milestone plan (small, concrete chunks)

| # | Target                         | Success Criteria                                                                                                                          | Owner  | Est. Time |
| - | ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------- | ------ | --------- |
| 0 | **Toolchain bootstrap**        | `./xb setup && ./xb build` finishes on macOS 14 arm64, builds `xenia-base-tests`                                                          | *you*  | ½ day     |
| 1 | **Dynamic guest memory**       | Replace fixed‑map allocator; `xenia-base-tests` fully green except for the two fixed-map tests.                                           | *you*  | 2 days    |
| 2 | **Integrate a64 backend**      | Rebase Wunkolo’s branch; get `xenia-cpu-ppc-tests` passing (`./xb build --target xenia-cpu-ppc-tests && ./build/bin/xenia-cpu-ppc-tests`) | *you*  | 3 days    |
| 3 | **Mach‑O exception unwinding** | A64 stack‑walker reports correct frames (leveraging #2258 code paths)                                                                     | *you*  | 1 day     |
| 4 | **Metal shading path P0**      | Compile built‑in shaders to MSL; render first vertex‑clear test on Apple GPU                                                              | *you*  | 4 days    |
| 5 | **Input / HID**                | SDL2 gamepad events feed into Xenia’s HID layer; confirm with log spam                                                                    | *you*  | 1 day     |
| 6 | **Audio**                      | Port XAUDIO2 backend to Core Audio (`xenia-apu-coreaudio.cc`)                                                                             | *you*  | 2 days    |
| 7 | **Game boot smoke test**       | First title (`dash.xex`) reaches dashboard; CI artifact posted                                                                            | *team* | —         |

**Tip**: Land each milestone behind `#ifdef XE_PLATFORM_MAC` blocks and keep filenames descriptive (e.g. `a64_backend_mac.cc`). Follow Google style, run `xb format` & `xb lint --all` before PRs.

### 10.3  Immediate action items

- Step 1, let's replaced the fixed-map allocator in memory\_mac.cc.

---

*Next review checkpoint: once milestone #2 (CPU tests) is green.*

*Last updated: 2025‑07‑29*


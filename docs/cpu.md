# CPU Documentation

## The JIT

![JIT Diagram](images/CPU-JIT.png?raw=true)

The JIT is the core of Xenia. It translates Xenon PowerPC code into native
code runnable on the host computer.

There are 3 phases to translation:
1. Translation to IR (intermediate representation)
2. IR compilation/optimization
3. Backend emission

PowerPC instructions are translated to Xenia's intermediate representation
format in src/xenia/cpu/ppc/ppc_emit_*.cc (e.g. processor control is done in
[ppc_emit_control.cc](../src/xenia/cpu/ppc/ppc_emit_control.cc)). HIR opcodes
are relatively simple opcodes such that any host can define an implementation.

After the HIR is generated, it is ran through a compiler to prep it for generation.
The compiler is ran in a series of passes, the order of which is defined in
[ppc_translator.cc](../src/xenia/cpu/ppc/ppc_translator.cc). Some passes are
essential to the successful generation, while others are merely for optimization
purposes. Compiler passes are defined in src/xenia/cpu/compiler/passes with
descriptive class names.

Finally, the backend consumes the HIR and emits code that runs natively on the
host. Currently, two backends exist:
- **x64 backend**: For x86_64 hosts, using [xbyak](https://github.com/herumi/xbyak)
- **A64 backend**: For ARM64 hosts, using [Oaknut](https://github.com/merryhime/oaknut)

## Backends

### x64 Backend

The x64 backend emits x86_64 native code. All emission is done in
[x64_sequences.cc](../src/xenia/cpu/backend/x64/x64_sequences.cc).

Key files:
- `src/xenia/cpu/backend/x64/x64_backend.cc` - Backend initialization, transition thunks
- `src/xenia/cpu/backend/x64/x64_emitter.cc` - Code emission infrastructure
- `src/xenia/cpu/backend/x64/x64_sequences.cc` - HIR opcode implementations
- `src/xenia/cpu/backend/x64/x64_stack_layout.h` - Stack frame layout

### A64 Backend

The A64 backend emits ARM64 native code using [Oaknut](https://github.com/merryhime/oaknut),
a lightweight ARM64 assembler. This backend was originally developed by
[wunkolo](https://github.com/Wunkolo/xenia/tree/arm64-windows) for ARM64 Windows
and has been ported to ARM64 macOS.

Key files:
- `src/xenia/cpu/backend/a64/a64_backend.cc` - Backend initialization, transition thunks
- `src/xenia/cpu/backend/a64/a64_emitter.cc` - Code emission infrastructure
- `src/xenia/cpu/backend/a64/a64_sequences.cc` - HIR opcode implementations (scalar)
- `src/xenia/cpu/backend/a64/a64_seq_vector.cc` - Vector/SIMD opcode implementations
- `src/xenia/cpu/backend/a64/a64_seq_memory.cc` - Memory operation implementations
- `src/xenia/cpu/backend/a64/a64_seq_control.cc` - Control flow implementations
- `src/xenia/cpu/backend/a64/a64_stack_layout.h` - Stack frame layout

The A64 backend supports optional CPU features detected at runtime:
- **LSE** (Large System Extensions): Atomic operations
- **F16C**: Half-precision floating point conversion

## ABI

Xenia guest functions are not directly callable, but rather must be called
through APIs provided by Xenia. Xenia will first execute a thunk to transition
the host context to a state dependent on the JIT backend, and that will call the
guest code.

### x64 ABI

Transition thunks defined in [x64_backend.cc](../src/xenia/cpu/backend/x64/x64_backend.cc#L389).
Registers are stored on the stack as defined by [StackLayout::Thunk](../src/xenia/cpu/backend/x64/x64_stack_layout.h#L96)
for later transitioning back to the host.

Some registers are reserved for usage by the JIT to store temporary variables.
See: [X64Emitter::gpr_reg_map_ and X64Emitter::xmm_reg_map_](../src/xenia/cpu/backend/x64/x64_emitter.cc#L57).

#### x64 Integer Registers

Register | Usage
---      | ---
RAX      | Scratch
RBX      | JIT temp
RCX      | Scratch
RDX      | Scratch
RSP      | Stack Pointer
RBP      | Unused
RSI      | PowerPC Context
RDI      | Virtual Memory Base
R8-R11   | Unused (parameters)
R12-R15  | JIT temp

#### x64 Floating Point Registers

Register   | Usage
---        | ---
XMM0-XMM5  | Scratch
XMM6-XMM15 | JIT temp

### A64 ABI

Transition thunks defined in [a64_backend.cc](../src/xenia/cpu/backend/a64/a64_backend.cc).
Registers are stored on the stack as defined by [StackLayout::Thunk](../src/xenia/cpu/backend/a64/a64_stack_layout.h).

#### A64 Integer Registers

Register | Usage
---      | ---
X0       | Guest return address (from host-to-guest thunk)
X1-X15   | Scratch / argument registers
X16-X17  | Intra-procedure-call scratch (IP0/IP1)
X18      | Platform register (reserved on macOS/iOS)
X19-X26  | JIT temp (callee-saved, mapped to HIR registers)
X27      | PowerPC context pointer (`PPCContext*`)
X28      | Virtual memory base (membase)
X29      | Frame Pointer (FP)
X30      | Link Register (LR) / Scratch
SP       | Stack Pointer

JIT register mapping (`gpr_reg_map_`): X19, X20, X21, X22, X23, X24, X25, X26

#### A64 Vector Registers

Register | Usage
---      | ---
V0-V7    | Scratch (also parameter/result registers)
V8-V15   | JIT temp (callee-saved lower 64 bits, mapped to HIR registers)
V16-V31  | Scratch

JIT register mapping (`fpr_reg_map_`): V8, V9, V10, V11, V12, V13, V14, V15

## Memory

Xenia defines virtual memory as a mapped range beginning at Memory::virtual_membase(),
and physical memory as another mapped range from Memory::physical_membase()
(usually 0x100000000 and 0x200000000, respectively). If the default bases are
not available, they are shifted left 1 bit until an available range is found.

The guest only has access to these ranges, nothing else.

### Map
```
0x00000000 - 0x3FFFFFFF (1024mb) - virtual 4k pages
0x40000000 - 0x7FFFFFFF (1024mb) - virtual 64k pages
0x80000000 - 0x8BFFFFFF ( 192mb) - xex 64k pages
0x8C000000 - 0x8FFFFFFF (  64mb) - xex 64k pages (encrypted)
0x90000000 - 0x9FFFFFFF ( 256mb) - xex 4k pages
0xA0000000 - 0xBFFFFFFF ( 512mb) - physical 64k pages (overlapped)
0xC0000000 - 0xDFFFFFFF          - physical 16mb pages (overlapped)
0xE0000000 - 0xFFFFFFFF          - physical 4k pages (overlapped)
```

Virtual pages are usually allocated by NtAllocateVirtualMemory, and
physical pages are usually allocated by MmAllocatePhysicalMemoryEx.

Virtual pages mapped to physical memory are also mapped to the physical membase,
i.e. virtual 0xA0000000 == physical 0x00000000

The 0xE0000000-0xFFFFFFFF range is mapped to physical memory with a single 4 KB
page offset. On Windows, memory mappings must be aligned to 64 KB, so the offset
has to be added when guest addresses are converted to host addresses in the
translated CPU code. This can't be faked other ways because calculations
involving the offset are built into games - see the following sequence:

```
srwi      r9, r10, 20       # r9 = r10 >> 20
clrlwi    r10, r10, 3       # r10 = r10 & 0x1FFFFFFF (physical address)
addi      r11, r9, 0x200
rlwinm    r11, r11, 0,19,19 # r11 = r11 & 0x1000
add       r11, r11, r10     # add 1 page to addresses > 0xE0000000

# r11 = addess passed to GPU
```

## Memory Management

### Platform-Specific Implementations

Memory allocation varies by platform:

**Windows** (`memory_win.cc`):
- Uses VirtualAlloc/VirtualFree for memory management
- Supports fixed address allocation natively
- 64 KB allocation granularity

**macOS ARM64** (`memory_posix.cc`):
- Custom implementation for ARM64 macOS in `AllocFixed()`
- Uses `MAP_JIT` flag for executable memory (required by Apple Silicon)
- Falls back to OS-chosen addresses if fixed mapping fails
- Uses `mmap`/`munmap` for allocation
- File mappings use `shm_open` with `MAP_SHARED`

**Linux/POSIX** (`memory_posix.cc`):
- Standard POSIX `mmap` with `MAP_FIXED | MAP_ANONYMOUS`
- Uses `mmap64` for file views

**Android**:
- Uses ASharedMemory (API 26+) or `/dev/ashmem` (older APIs)

### Current Status

The macOS ARM64 memory implementation has been specifically updated to handle:
- Apple Silicon's stricter memory protection requirements
- `MAP_JIT` requirement for W^X (write XOR execute) policy
- Page size differences (16 KB on Apple Silicon vs 4 KB on Intel)

Note: The original Windows/Linux fixed-address allocation behavior has not yet
been fully integrated back for other platforms after the ARM64 macOS changes.
Cross-platform testing is needed.

## References

### PowerPC

The processor in the 360 is a 64-bit PowerPC chip running in 32-bit mode.
Programs are still allowed to use 64-bit PowerPC instructions, and registers
are 64-bit as well, but 32-bit instructions will run in 32-bit mode.
The CPU is largely similar to the PPC part in the PS3, so Cell documents
often line up for the core instructions. The 360 adds some additional AltiVec
instructions, though, which are only documented in a few places (like the gcc source code, etc).

* [Free60 Info](https://free60project.github.io/wiki/Xenon_(CPU))
* [Power ISA docs](https://web.archive.org/web/20140603115759/https://www.power.org/wp-content/uploads/2012/07/PowerISA_V2.06B_V2_PUBLIC.pdf) (aka 'PowerISA')
* [PowerPC Programming Environments Manual](https://web.archive.org/web/20141028181028/https://www-01.ibm.com/chips/techlib/techlib.nsf/techdocs/F7E732FF811F783187256FDD004D3797/$file/pem_64bit_v3.0.2005jul15.pdf) (aka 'pem_64')
* [PowerPC Vector PEM](https://web.archive.org/web/20130502201029/https://www-01.ibm.com/chips/techlib/techlib.nsf/techdocs/C40E4C6133B31EE8872570B500791108/$file/vector_simd_pem_v_2.07c_26Oct2006_cell.pdf)
* [AltiVec PEM](https://web.archive.org/web/20151110180336/https://cache.freescale.com/files/32bit/doc/ref_manual/ALTIVECPEM.pdf)
* [VMX128 Opcodes](http://biallas.net/doc/vmx128/vmx128.txt)
* [AltiVec Decoding](https://github.com/kakaroto/ps3ida/blob/master/plugins/PPCAltivec/src/main.cpp)

### x64

* [Intel Manuals](https://software.intel.com/en-us/articles/intel-sdm)
   * [Combined Intel Manuals](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-manual-325462.pdf)
* [Apple AltiVec/SSE Migration Guide](https://developer.apple.com/legacy/library/documentation/Performance/Conceptual/Accelerate_sse_migration/Accelerate_sse_migration.pdf)

### ARM64

* [ARM Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest)
* [ARM Cortex-A Series Programmer's Guide](https://developer.arm.com/documentation/den0024/latest)
* [Oaknut ARM64 Assembler](https://github.com/merryhime/oaknut)
* [ARM NEON Intrinsics Reference](https://developer.arm.com/architectures/instruction-sets/intrinsics/)
* [Apple Silicon CPU Optimization Guide](https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon)

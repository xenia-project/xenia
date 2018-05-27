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
host. Currently, the only backend that exists is the x64 backend, with all the
emission done in
[x64_sequences.cc](../src/xenia/cpu/backend/x64/x64_sequences.cc).

## ABI

Xenia guest functions are not directly callable, but rather must be called
through APIs provided by Xenia. Xenia will first execute a thunk to transition
the host context to a state dependent on the JIT backend, and that will call the
guest code.

### x64

Transition thunks defined in [x64_backend.cc](../src/xenia/cpu/backend/x64/x64_backend.cc#L389).
Registers are stored on the stack as defined by [StackLayout::Thunk](../src/xenia/cpu/backend/x64/x64_stack_layout.h#L96)
for later transitioning back to the host.

Some registers are reserved for usage by the JIT to store temporary variables.
See: [X64Emitter::gpr_reg_map_ and X64Emitter::xmm_reg_map_](../src/xenia/cpu/backend/x64/x64_emitter.cc#L57).

#### Integer Registers

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

#### Floating Point Registers
Register   | Usage
---        | ---
XMM0-XMM5  | Scratch
XMM6-XMM15 | JIT temp

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

Unfortunately, the 0xE0000000-0xFFFFFFFF range is unused in Xenia because
it maps to physical memory with a single page offset, which is impossible
to do under the Win32 API. We can't fake this either, as this offset is 
built into games - see the following sequence:

```
srwi      r9, r10, 20       # r9 = r10 >> 20
clrlwi    r10, r10, 3       # r10 = r10 & 0x1FFFFFFF (physical address)
addi      r11, r9, 0x200
rlwinm    r11, r11, 0,19,19 # r11 = r11 & 0x1000
add       r11, r11, r10     # add 1 page to addresses > 0xE0000000

# r11 = addess passed to GPU
```

## Memory Management

TODO

## References

### PowerPC

The processor in the 360 is a 64-bit PowerPC chip running in 32-bit mode.
Programs are still allowed to use 64-bit PowerPC instructions, and registers
are 64-bit as well, but 32-bit instructions will run in 32-bit mode.
The CPU is largely similar to the PPC part in the PS3, so Cell documents
often line up for the core instructions. The 360 adds some additional AltiVec
instructions, though, which are only documented in a few places (like the gcc source code, etc).

* [Free60 Info](http://www.free60.org/Xenon_\(CPU\))
* [Power ISA docs](https://www.power.org/wp-content/uploads/2012/07/PowerISA_V2.06B_V2_PUBLIC.pdf) (aka 'PowerISA')
* [PowerPC Programming Environments Manual](https://www-01.ibm.com/chips/techlib/techlib.nsf/techdocs/F7E732FF811F783187256FDD004D3797/$file/pem_64bit_v3.0.2005jul15.pdf) (aka 'pem_64')
* [PowerPC Vector PEM](https://www-01.ibm.com/chips/techlib/techlib.nsf/techdocs/C40E4C6133B31EE8872570B500791108/$file/vector_simd_pem_v_2.07c_26Oct2006_cell.pdf)
* [AltiVec PEM](http://cache.freescale.com/files/32bit/doc/ref_manual/ALTIVECPEM.pdf)
* [VMX128 Opcodes](http://biallas.net/doc/vmx128/vmx128.txt)
* [AltiVec Decoding](https://github.com/kakaroto/ps3ida/blob/master/plugins/PPCAltivec/src/main.cpp)

### x64

* [Intel Manuals](http://www.intel.com/content/www/us/en/processors/architectures-software-developer-manuals.html)
   * [Combined Intel Manuals](http://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-manual-325462.pdf)
* [Apple AltiVec/SSE Migration Guide](https://developer.apple.com/legacy/library/documentation/Performance/Conceptual/Accelerate_sse_migration/Accelerate_sse_migration.pdf)

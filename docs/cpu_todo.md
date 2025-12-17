# CPU TODO

There are many improvements that can be done under `xe::cpu` to improve
debugging, performance (both to JIT and of generated code), and portability.
Some are in various states of completion, and others are just thoughts that need
more exploring.

## A64 Backend Status

The A64 (ARM64) backend was developed by [wunkolo](https://github.com/Wunkolo/xenia/tree/arm64-windows)
and has been ported to macOS. Current status:

### Completed

- Full HIR opcode coverage (all sequences implemented)
- Register allocation with ARM64 calling convention
- NEON SIMD for vector operations
- LSE (Large System Extensions) atomic operations (runtime detected)
- F16C half-precision float conversion (runtime detected)
- All CPU tests passing (xenia-base-tests, xenia-cpu-ppc-tests, xenia-cpu-tests)
- Stack walking and debugging support
- Instruction stepping

### Known Limitations

- Cache maintenance instructions cause issues on some platforms (see TODO comments
  in `a64_seq_memory.cc`)
- Some emulated opcodes still use CallNativeSafe (expensive host transition)

### Potential Improvements

- Profile and optimize hot sequences for Apple Silicon specifically
- Investigate SVE/SVE2 usage for wider vector operations (if available)
- Better constant pooling for vector constants

## Debugging Improvements

### Reproducible Native Emission

It'd be useful to be able to run a PPC function through the entire pipeline and
spit out native code that is byte-for-byte identical across runs. This would allow
automated verification, bulk analysis, etc.

**x64**: Currently `X64Emitter::Emplace` will relocate the x64 when placing it in
memory, which will be at a different location each time. Instead it would be nice
to have the xbyak `calcJmpAddress` that performs the relocations use the address
of our choosing.

**A64**: Similar considerations apply with Oaknut's code generation.

### Sampling Profiler

Once we have stack walking it'd be nice to take something like
[micro-profiler](https://code.google.com/p/micro-profiler/) and augment it to
support our system. This would let us run continuous performance analysis and
track hotspots in JITed code without a large performance impact. Automatically
showing the top hot functions in the debugger could help track down poor
translation much faster.

### Architecture-Specific Analysis Tools

**x64**: The [Intel ACA](https://software.intel.com/en-us/articles/intel-architecture-code-analyzer)
can detail theoretical performance characteristics on different processors down
to cycle timings and potential bottlenecks.

**A64**: Apple's Instruments with the CPU Profiler template can analyze generated
code on Apple Silicon. The `llvm-mca` tool can also analyze ARM64 code.

### Function Tracing/Coverage Information

`function_trace_data.h` contains the `FunctionTraceData` struct, which is
currently partially populated by backends. This enables tracking of which
threads a function is called on, function call count, recent callers of the
function, and even instruction-level counts.

This is all only partially implemented, though, and there's no tool to read it
out. This would be nice to get integrated into the debugger so that it can
overlay the information when viewing a function, but also useful in aggregate to
find hot functions/code paths or enhance callstacks by automatically annotating
thread information.

#### Block-level Counting

Currently the code assumes each instruction has a count, however this is
expensive and often unneeded as it can be done on a block level and then the
instruction counts can be derived from that. This can reduce the overhead (both
in memory and accounting time) by an order of magnitude.

### On-Stack Context Inspection

Currently the debugger only works with `--store_all_context_values`, as it can
only get the values of PPC registers when they are stored to the PPC context
after each instruction. As this can slow things down by ~10-20% it could be
useful to be able to preserve the optimized and register-allocated HIR so that
host registers holding context values can be derived on demand. Or, we could
just make `--store_all_context_values` faster.

## JIT Performance Improvements

### Reduce HIR Size

Currently there are a lot of pointers stored within `Instr`, `Value`, and
related types. These are big 8B values that eat a lot of memory and really
hurt the cache (especially with all the block/instruction walking done).
Aligning everything to 16B values in the arena and using 16bit indices
(or something) could shrink things a lot.

### Serialize Code Cache

The code cache is currently set up to use fixed memory addresses and is even
represented as mapped memory. It should be fairly easy to back this with a file
and have all code written to disk. Adding more metadata, or perhaps a side-car
file, would allow for the code to be written to disk. On future runs the code
cache could load this data (by mapping the file containing the code right into
memory) and short cut JIT'ing entirely.

It would be possible to use a common container format (ELF/etc), however there's
elegance in not requiring any additional steps beyond the memory mapping. Such
containers could be useful for running static tools against, though.

## Portability Improvements

### Emulated Opcode Layer

Having a way to use emulated variants for any HIR opcode in a backend would
help when writing a new backend as well as when verifying the existing backends.
This may look like a C library with functions for each opcode/type pairing and
utilities to call out to them. Something like the x64 backend could then call
out to these with CallNativeSafe (or some faster equivalent) and something like
an interpreter backend would be fairly trivial to write.

## X64 Backend Improvements

### Implement Emulated Instructions

There are a ton of half-implemented HIR opcodes that call out to C++ to do their
work. These are extremely expensive as they incur a full guest-to-host thunk
(~hundreds of instructions!). Basically, any of the `Emulate*`/`CallNativeSafe`
functions in `x64_sequences.cc` need to be replaced with proper AVX/AVX2
variants.

### Increase Register Availability

Currently only a few x64 registers are usable (due to reservations by the
backend or ABI conflicts). Though register pressure is surprisingly light in
most cases there are pathological cases that result in a lot of spills. By
freeing up some of the registers these spills could be reduced.

### Constant Pooling

This may make sense as a compiler pass instead.

Right now, particular sequences of instructions are nasty - such as anything
using `LoadConstantXmm` to load non-zero or non-1 vec128's. Instead of doing the
super fat (20-30byte!) constant loads as they are done now it may be better to
keep a per-function constant table and instead use RIP-relative addressing (or
something) to use the memory-form AVX instructions.

For example, right now this:
```
  v82.v128 = [0,1,2,3]
  v83.v128 = or v81.v128, v82.128
```

Translates to (something like):
```
  mov([rsp+0x...], 0x00000000)
  mov([rsp+0x...+4], 0x00000001)
  mov([rsp+0x...+8], 0x00000002)
  mov([rsp+0x...+12], 0x00000003)
  vmovdqa(xmm2, [rsp+0x...])
  vor(xmm2, xmm2, xmm2)
```

Where as it could be:
```
  vor(xmm2, xmm2, [rip+0x...])
```

Whether the cost of doing the constant de-dupe is worth it remains to be seen.
Right now it's wasting a lot of instruction cache space, increasing decode time,
and potentially using a lot more memory bandwidth.

## A64 Backend Improvements

### Reduce CallNativeSafe Usage

Similar to x64, some opcodes fall back to C++ emulation via `CallNativeSafe`.
These should be replaced with native NEON implementations where possible.

### Optimize for Apple Silicon

Apple's M-series chips have specific performance characteristics:
- Very wide decode (8+ instructions/cycle)
- Deep reorder buffer
- Excellent branch prediction

Consider:
- Reducing branch density in hot paths
- Leveraging the large register file more aggressively
- Using Apple-specific optimizations when detected

### Vector Constant Loading

The A64 backend has optimizations for common vector constants using `MOVI`/`FMOV`
instructions. Additional patterns could be recognized:
- Byte-splat patterns
- Replicated element patterns
- Sequences that can use `MOVI` with shift

## Optimization Improvements

### Speed Up RegisterAllocationPass

Currently the slowest pass, this could be improved by requiring less use
tracking or perhaps maintaining the use tracking in other passes. A faster
SortUsageList (radix or something fancy?) may be helpful as well.

### More Opcodes in ConstantPropagationPass

There's a few HIR opcodes with no handling, and others with minimal handling.
It'd be nice to know what paths need improvement and add them, as any work here
makes things free later on.

### Cross-Block ConstantPropagationPass

Constant propagation currently only occurs within a single block. This makes it
difficult to optimize common PPC patterns like loading the constants 0 or 1 into
a register before a loop and other loads of expensive altivec values.

Either ControlFlowAnalysisPass or DataFlowAnalysisPass could be piggy-backed to
track constant load_context/store_context's across block bounds and propagate
the values. This is simpler than dynamic values as no phi functions or anything
fancy needs to happen.

### Add TypePropagationPass

There are many extensions/truncations in generated code right now due to
various load/stores of varying widths. Being able to find and short-
circuit the conversions early on would make following passes cleaner
and faster as they'd have to trace through fewer value definitions and there'd
be less extraneous movs in the final code.

Example (after ContextPromotion):
```
  v82.i32 = truncate v81.i64
  v83.i32 = and v82.i32, 3F
  v85.i64 = zero_extend v84.i32
```

Becomes (after DCE/etc):
```
  v85.i64 = and v81.i64, 3F
```

### Enhance MemorySequenceCombinationPass with Extend/Truncate

Currently this pass will look for byte_swap and merge that into loads/stores.
This allows for better final codegen at the cost of making optimization more
difficult, so it only happens at the end of the process.

There's currently TODOs in there for adding extend/truncate support, which
will extend what it does with swaps to also merge the
sign_extend/zero_extend/truncate into the matching load/store. This allows for
the backend to generate the proper mov's that do these operations without
requiring additional steps. Note that if we had a LIR and a peephole optimizer
this would be better done there.

Load with swap and extend:
```
  v1.i32 = load v0
  v2.i32 = byte_swap v1.i32
  v3.i64 = zero_extend v2.i32
```

Becomes:
```
  v1.i64 = load_convert v0, [swap|i32->i64,zero]
```

Store with truncate and swap:
```
  v1.i64 = ...
  v2.i32 = truncate v1.i64
  v3.i32 = byte_swap v2.i32
  store v0, v3.i32
```

Becomes:
```
  store_convert v0, v1.i64, [swap|i64->i32,trunc]
```

### Add DeadStoreEliminationPass

Generic DSE pass, removing all redundant stores. ContextPromotion may be
able to take care of most of these, as the input assembly is generally
pretty optimized already. This pass would mainly be looking for introduced
stores, such as those from comparisons.

Currently ControlFlowAnalysisPass will annotate blocks with incoming/outgoing
edges as well as dominators, and that could be used to check whether stores into
the context are used in their destination block or instead overwritten
(currently they almost never are).

If this pass was able to remove a good number of the stores then the comparisons
would also be removed with dead code elimination and dramatically reduce branch
overhead.

Example:
```
<block0>:
  v0 = compare_ult ...     (later removed by DCE)
  v1 = compare_ugt ...     (later removed by DCE)
  v2 = compare_eq ...
  store_context +300, v0   <-- removed
  store_context +301, v1   <-- removed
  store_context +302, v2   <-- removed
  branch_true v1, ...
<block1>:
  v3 = compare_ult ...
  v4 = compare_ugt ...
  v5 = compare_eq ...
  store_context +300, v3   <-- these may be required if at end of function
  store_context +301, v4       or before a call
  store_context +302, v5
  branch_true v5, ...
```

### Add CanonicalizationPass

For various opcodes add copies/commute the arguments to match target
operand semantics. This makes code generation easier and if done
before register allocation can prevent a lot of extra shuffling in
the emitted code.

Example:
```
<block0>:
  v0 = ...
  v1 = ...
  v2 = add v0, v1          <-- v1 now unused
```

Becomes:
```
  v0 = ...
  v1 = ...
  v1 = add v1, v0          <-- src1 = dest/src, so reuse for both
                               by commuting and setting dest = src1
```

### Add MergeLocalSlotsPass

As the RegisterAllocationPass runs it generates load_local/store_local as it
spills. Currently each set of locals is unique to each block, which in very
large functions can result in a lot of locals that are only used briefly. It
may be useful to use the results of the ControlFlowAnalysisPass to track local
liveness and merge the slots so they are reused when they cannot possibly be
live at the same time. This saves stack space and potentially improves cache
behavior.

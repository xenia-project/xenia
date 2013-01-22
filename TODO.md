## Instructions

```
stbu
lwzu

addx
addix
addic
addzex
subfx
subfex
subficx

mulli
mullwx
divwux

andix
orx

rlwinmx
rldiclx
extsbx
slwx
srawix

# can be no-op
dcbt
dcbtst

twi

```

## Codegen

### Tracing

Tracing modes:
- off (0)
- syscalls (1)
- fn calls (2)
- all instructions (3)

Inject extern functions into module:
- XeTraceKernelCall(caller_ia, cia, name, state)
- XeTraceUserCall(caller_ia, cia, name, state)
- XeTraceInstruction(cia, name, state)

### Calling convention

Experiment with fastcc? May need typedef fn ptrs to call into the JITted code.

### Indirect branches (ctr/lr)

emit_control.cc XeEmitBranchTo
Need to take the value in LR/CTR and do something with it.

Return path:
- In SDB see if the function follows the 'return' semantic:
  - mfspr LR / mtspr LR/CTR / bcctr -- at end?
- In codegen add a BB that is just return.
- If a block 'returns', branch to the return BB.

Tail calls:
- If in a call BB check next BB.
- If next is a return, optimize to a tail call.

Fast path:
- Every time LR would be stashed, add the value of LR (some NIA) to a lookup
  table.
- When doing an indirect branch first lookup the address in the table.
- If not found, slow path, else jump.

Slow path:
- Call out and do an SDB lookup.
- If found, return, add to lookup table, and jump.
- If not found, need new function codegen!

### Caching of register values in basic blocks

Right now the SSA values seem to leak from the blocks somehow. All caching
is disabled.

```

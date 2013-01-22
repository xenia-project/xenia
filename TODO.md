
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

### Indirect branches (ctr/lr)

emit_control.cc XeEmitBranchTo
Need to take the value in LR/CTR and do something with it.

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

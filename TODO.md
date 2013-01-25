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
rlwimix
rldiclx
extsbx
slwx
srawix

# can be no-op, or @llvm.prefetch
dcbt
dcbtst

twi # @llvm.debugtrap ?

```

Overflow bits can be set via the intrinsics:
`llvm.sadd.with.overflow`/etc
It'd be nice to avoid doing this unless absolutely required. The SDB could
walk functions to see if they ever read or branch on the SO bit of things.

Condition bits are, after each function:
```
if (target_reg < 0) { CR0 = b100 | XER[SO] }
if (target_reg > 0) { CR0 = b010 | XER[SO] }
else                { CR0 = b001 | XER[SO] }
```
Most PPC instructions are optimized by the compiler to have Rc=0 and not set the
bits if possible. There are some instructions, though, that always set them.
For those, it would be nice to remove redundant sets. Maybe LLVM will do it
automatically due to the local cr? May need to split that up into a few locals
(one for each bit?) to ensure deduping.

`@llvm.expect.i32`/`.i64` could be used with the BH bits in branches to
indicate expected values.

## Codegen

### Calling convention

Experiment with fastcc? May need typedef fn ptrs to call into the JITted code.

### Function calling convention analysis

Track functions to see if they follow the standard calling convention.
This could use the hints from the EH data in the XEX. Looking specifically for
stack prolog/epilog and branches to LR.

Benefits:
- Optimized prolog/epilog generation.
- Local variables for stack storage (alloca/etc) instead of user memory.
- Better return detection and fast returns.

### Indirect branches (ctr/lr)

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
```

## Debugging


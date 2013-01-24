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

# can be no-op, or @llvm.prefetch
dcbt
dcbtst

twi # @llvm.debugtrap ?

```

Overflow bits can be set via the intrinsics:
`llvm.sadd.with.overflow`/etc
It'd be nice to avoid doing this unless absolutely required. The SDB could
walk functions to see if they ever read or branch on the SO bit of things.

`@llvm.expect.i32`/`.i64` could be used with the BH bits in branches to
indicate expected values.

## Codegen

### Branch generation

Change style to match: http://llvm.org/docs/tutorial/LangImpl5.html
Insert check code, then push_back the branch block and implicit else after
its generated. This ensures ordering stays legit.

### Stack variables

Use stack variables for registers.

- All allocas should go in the entry block.
  - Lazily add or just add all registers/etc at the head.
  - Must be 1 el, int64
- Reuse through function.
- On FlushRegisters write back to state.
- FlushRegisters on indirect branch or call.

```
/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          const std::string &VarName) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                 TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(getGlobalContext()), 0,
                           VarName.c_str());
}
// stash result of above and reuse
// on first use in entry get the value from state?

// Promote allocas to registers.
OurFPM.add(createPromoteMemoryToRegisterPass());
// Do simple "peephole" optimizations and bit-twiddling optzns.
OurFPM.add(createInstructionCombiningPass());
// Reassociate expressions.
OurFPM.add(createReassociatePass());
```

### Tracing

- Trace kernel export info (missing/present/etc).
- Trace user call info (name/?).
- Trace instruction info (disasm).

### Calling convention

Experiment with fastcc? May need typedef fn ptrs to call into the JITted code.

nonlazybind fn attribute to prevent lazy binding (slow down startup)

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


If the indirect br looks like it may be local (no stack setup/etc?) then
build a jump table:

```
Branch register with no link:
switch i32 %nia, label %non_local [ i32 0x..., label %loc_...
                                    i32 0x..., label %loc_...
                                    i32 0x..., label %loc_... ]
%non_local: going outside of the function

Could put one of these tables at the bottom of each function and share
it.
This could be done via indirectbr if branchaddress is used to stash the
address. The address must be within the function, though.

Branch register with link:
check, never local?
```

### Caching of register values in basic blocks

Right now the SSA values seem to leak from the blocks somehow. All caching
is disabled.

```

## Debugging


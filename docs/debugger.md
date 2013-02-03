# Debugger

## Protocol

Framed messages:

```
[4b type] (0=request, 1=notification, etc)
[4b content source id]
[4b request/response id]
[4b size]
[payload]
```

## Content Sources

### `xe::dbg::sources::MemorySource`

[ ] Paged view into memory
[ ] Search operations
[ ] Live streaming updates
[ ] Writes

### `xe::dbg::sources::ProcessorSource`

[ ] Thread list
[ ] State read/write (per thread)
[ ] Modules
[ ] Statistics
[ ] Basic control (pause/resume)
[ ] Breakpoints/checkpoints/traces
[ ] Trace stream

### `xe::dbg::sources::ModuleSource`

[ ] Paged view into all symbols
[ ] Get function contents (data, disasm, llvm, x86, etc)

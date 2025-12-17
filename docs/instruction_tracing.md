# Instruction Tracing

Instruction tracing allows detailed logging of PPC instruction execution and
HIR data for debugging purposes.

## x64 Backend

In `x64_tracers.cc`:

Enable tracing:
```cpp
#define ITRACE 1  // PPC instructions only
#define DTRACE 1  // Add HIR data
```

## A64 Backend

In `a64_tracers.cc`:

Enable tracing:
```cpp
#define ITRACE 1  // PPC instructions only
#define DTRACE 1  // Add HIR data
```

## Common Configuration

### Runtime Flags

If tracing data, run with the following flags:
```
--store_all_context_values
```

### Thread Filtering

By default, tracing will start at the beginning and only for the specified
thread.

Change traced thread by thread creation ID:
```cpp
#define TARGET_THREAD 4
```

### Conditional Tracing

To only trace at a certain point, change default trace flag to false:
```cpp
bool trace_enabled = false;  // Change from true
```

Add a breakpoint:
```
--break_on_instruction=0x821009A4
```

On break, add the following to the Watch window (adjust namespace for backend):

**x64**:
```
xe::cpu::backend::x64::trace_enabled
```

**A64**:
```
xe::cpu::backend::a64::trace_enabled
```

Set it to `true`, continue, and watch stuff appear in the log.

## Output

Trace output goes to the debug log. Use `--log_file=stdout` to see it in
the terminal, or check the log file (named after the executable by default).

The output includes:
- **ITRACE**: PPC instruction address and disassembly
- **DTRACE**: HIR values, register contents, memory operations

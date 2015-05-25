In x64_tracers.cc:

Enable tracing:
```
#define ITRACE 1  <--- for only ppc instructions
#define DTRACE 1  <--- add HIR data
```

If tracing data, run with the following flags:
```
--store_all_context_values
```

By default, tracing will start at the beginning and only for the specified
thread.

Change traced thread by thread creation ID:
```
#define TARGET_THREAD 4
```

To only trace at a certain point, change default trace flag to false:
```
bool trace_enabled = true;
```
Add a breakpoint:
```
--break_on_instruction=0x821009A4
```
On break, add the following to the Watch window and set it to true:
```
xe::cpu::backend::x64::trace_enabled
```
Continue, and watch stuff appear in the log.

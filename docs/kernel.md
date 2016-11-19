# Kernel Documentation

## Kernel shims
Xenia implements all kernel APIs as native functions under the host.

When a module is loaded, the loader will find all kernel imports, put a syscall in
their place, then lookup a kernel export and link it to each import. The JIT will
generate a sequence of instructions to call into Xenia's export if it encounters a syscall.

Currently, there are two ways an export can be defined -
[for example](../src/xenia/kernel/xboxkrnl/xboxkrnl_audio.cc):
* `SHIM_CALL XAudioGetSpeakerConfig_shim(PPCContext* ppc_context, KernelState* kernel_state)`
* `dword_result_t XAudioGetSpeakerConfig(lpdword_t config_ptr)`

The `SHIM_CALL` convention is deprecated, but allows a closer look at the internals of how
calls are done. `ppc_context` is the guest PowerPC context, which holds all guest
registers. Function parameters are fetched from r3...r10 (`SHIM_GET_ARG_32`), and
additional parameters are loaded from the stack. The return value (if there is one)
is stored in r3 (`SHIM_SET_RETURN_32`).

Details on how calls transition from guest -> host can be found in the [cpu documentation](cpu.md).

The newer convention does the same, but uses templates to automate the process
of loading arguments and setting a return value.

## Kernel Modules
Xenia has an implementation of two xbox kernel modules, xboxkrnl.exe and xam.xex

### xboxkrnl.exe - Xbox kernel

Defined under src/xenia/kernel/xboxkrnl.

This is a slightly modified version of the NT kernel. Most of the APIs 
are equivalent to ones you'd find on MSDN or other online sources.

Source files are organized into groups of APIs.

### xam.xex - Xbox Auxiliary Methods

Defined under src/xenia/kernel/xam.

This module implements functionality specific to the Xbox.
# GPU Documentation

## References

### Command Buffer/Registers

### Shaders

* [LLVM R600 Tables](https://llvm.org/viewvc/llvm-project/llvm/trunk/lib/Target/R600/R600Instructions.td)
** The opcode formats don't match, but the name->psuedo code is correct.
* [xemit](https://github.com/gligli/libxemit/blob/master/xemitops.c)

## Tools

### apitrace

[apitrace](http://apitrace.github.io/) can be used to capture and replay D3D11
call streams. To disable stdout spew first set `XE_OPTION_ENABLE_LOGGING` to 0
in `logging.h`.

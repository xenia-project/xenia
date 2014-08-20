alloy: small dynamic recompiler engine
===

Alloy is a transpiler framework that allows for pluggable frontends for decoding
guest machine instructions (such as PPC) and pluggable backends for generating
host code (such as x64). It features an SSA IR designed to model machine
instructions and vector operations and compilation passes that seek to efficiently
optimize previously-optimized code.

Future versions will cache generated code across runs and enable offline
precompilation.

Frontends
---

Frontends are responsible for translating guest machine instructions into IR.
Information about the guest machine and ABI is used to support efficient CPU
state accesses and debug information.

* PPC64 with Altivec extenions

Backends
---

A backend takes optimized IR and assembles an implementation-specific result.
The backend is also responsible for executing the code it generates and supporting
debugging features (such as breakpoints).

* IVM: bytecode interpreter
* x64: IA-64 with AVX2 code generator

# Codegen Tests

This directory contains the test assets used by the automated codegen test
runner.

Each test is structured as a source `[name].s` PPC assembly file and the
generated outputs. The outputs are made using the custom build of binutils
setup when `xenia-build setup` is called and are checked in to make it easier
to run the tests on Windows.

Tests are run using the `xenia-test` app or via `xenia-build test`.

## Execution

The test binary is placed into memory at `0x82010000` and all other memory is
zeroed.

All registers are reset to zero. In order to provide useful inputs tests can
specify `# REGISTER_IN` values.

The code is jumped into at the starting address and executed until the last
instruction in the input file is reached.

After all instructions complete any `# REGISTER_OUT` values are checked and if
they do not match the test is failed.

## Annotations

Annotations can appear at any line in a file. If a number is required it can
be in either hex or decimal form, or IEEE if floating-point.

### REGISTER_IN

```
# REGISTER_IN [register name] [register value]
```

Sets the value of a register prior to executing the instructions.

Examples:
```
# REGISTER_IN r4 0x1234
# REGISTER_IN r4 5678
```

### REGISTER_OUT

```
# REGISTER_OUT [register name] [register value]
```

Defines the expected register value when the instructions have executed.
If after all instructions have completed the register value does not match
the value given here the test will fail.

Examples:
```
# REGISTER_OUT r3 123
```

TODO: memory setup/assertions

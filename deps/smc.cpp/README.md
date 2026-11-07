# SMC C++20 implementation

This repository contains an in-progress C++20 implementation of the core SMC
workflow. The lexer handles SMC directives, comments, source blocks, and
structural tokens; the parser builds an FSM model with maps, states,
transitions, guards, and opaque embedded C++ action/parameter text. The syntax
checker validates required C++ declarations, start-state references,
transition destinations, duplicate declarations/guards, and restrictions on
the default state.

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Validate a state-machine file without generating code:

```sh
./build/smc_cpp -c++ --check SmcLexer.sm
```

The documented option shape is accepted:

```sh
./build/smc_cpp -c++ -d . SmcLexer.sm
```

The C++ generator API is an explicit interface placeholder in this milestone.
Without `--check`, the CLI reports that generation is not implemented rather
than claiming to have produced output. Diagnostics include the input file,
line, and column.

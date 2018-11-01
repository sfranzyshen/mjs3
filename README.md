# mJS, a JS engine for embedded systems

[![Build Status](https://travis-ci.org/cpq/mjs3.svg?branch=master)](https://travis-ci.org/cpq/mjs3)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)


mJS is designed for microcontrollers with limited resources.
Main design goals are: small footprint and simple C/C++ interoperability.
mJS implements a strict subset of ES6 (JavaScript version 6):

- Any valid mJS code is a valid ES6 code
- Any valid ES6 code is not necessarily a valid mJS code


## Features

- Clean ISO C, ISO C++. Builds on VC98, modern compilers, 8-bit Arduinos, etc
- No dependencies
- No standard library. See API Reference on what is available
- No closures, only lexical scoping
- Strict mode only
- Supported contructs: 
- No `==` or `!=`, only `===` and `!==`
- mJS strings are byte strings, not Unicode strings: `'ы'.length === 2`,
 `'ы'[0] === '\xd1'`, `'ы'[1] === '\x8b'`
 mJS string can represent any binary data chunk.
- Preallocates all necessary memory and never calls `malloc`, `realloc` in
  the run time. On OOM, the VM is halted
- Object pool, property pool, and string pool sizes are defined at compile time
- Runtime RAM usage: an object takes 6 bytes, a property takes 16 bytes,
  a string takes string length + 6 bytes, any other type takes 4 bytes

## Usage example

```c
#include "mjs.h"

...
struct mjs *mjs = mjs_create();
mjs_destroy(mjs);
```

## JS API Reference

## C API Reference
See `msj.h` file!

## LICENSE

GPLv2 + commercial

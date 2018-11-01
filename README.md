# mJS, a JS engine for embedded systems

[![Build Status](https://travis-ci.org/cpq/mjs3.svg?branch=master)](https://travis-ci.org/cpq/mjs3)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)


mJS is a single-header JavaScript engine for microcontrollers.

## Features

- Clean ISO C, ISO C++. Builds on VC98, modern compilers, 8-bit Arduinos, etc
- No dependencies
- Implements a restricted subset of ES6 with limitations
- Preallocates all necessary memory and never calls `malloc`, `realloc` in
  the run time. On OOM, the VM is halted
- Object pool, property pool, and string pool sizes are defined at compile time
- The minimal configuration takes only a **few hundred** bytes of RAM
- Runtime RAM usage: an object takes 6 bytes, a property takes 16 bytes,
  a string takes string length + 6 bytes, any other type takes 4 bytes
- mJS strings are byte strings, not Unicode strings: `'ы'.length === 2`,
 `'ы'[0] === '\xd1'`, `'ы'[1] === '\x8b'`
- Strict mode only

## Supported syntax and API

| Name              |  Operation                                | Supported |
| ----------------- | ----------------------------------------- | ------ |
| Closures          |                                           | no |
| Ternary           | `... ? ... : ...`                         | yes    |
| Assignments       | `&#124;=`, `^=`, `&=`, `>>>=`, `>>=`, `<<=`, `%=`, `/=`, `*=`, `**=`, `-=`, `+=`, `=`  | yes    |
| Arithmetic ops    | `+`, `-`, `*`, `/`, `%`                   | yes    |
| Equality          | `==`, `!=`                                | no |
| Strict equality   | `!==`, `!==`                              | yes    |

## Usage example

```c
#include "mjs.h"

...
struct mjs *mjs = mjs_create();
mjs_destroy(mjs);
```

## JS API Reference

## C API Reference

## LICENSE

Dual license: GPLv2 or commercial. For commercial
licensing, please contact support@mongoose-os.com

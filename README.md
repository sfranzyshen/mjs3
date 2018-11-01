# mJS, a JS engine for embedded systems

[![Build Status](https://travis-ci.org/cpq/mjs3.svg?branch=master)](https://travis-ci.org/cpq/mjs3)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)


mJS is a single-header JavaScript engine for microcontrollers.

## Features

- Clean ISO C, ISO C++. Builds on old (VC98) and modern compilers, from 8-bit (e.g. Arduino mini) to 64-bit systems
- No dependencies
- Implements a restricted subset of ES6 with limitations
- Preallocates all necessary memory and never calls `malloc`, `realloc`
  at run time. Upon OOM, the VM is halted
- Object pool, property pool, and string pool sizes are defined at compile time
- The minimal configuration takes only a few hundred bytes of RAM
- Runtime RAM usage: an object takes 6 bytes, a property takes 16 bytes,
  a string takes length + 6 bytes, any other type takes 4 bytes
- mJS strings are byte strings, not Unicode strings.
  For example, `'ы'.length === 2`, `'ы'[0] === '\xd1'`, `'ы'[1] === '\x8b'`

## Supported operations and constructs

| Name              |  Operation                                |
| ----------------- | ----------------------------------------- |
| Logical, bitwise  | `\|\|`, `&&`, `\|`, `^`, `&`, `<<`, `>>`, `>>>`   |
| Comparison        | `>`, `<`, `>=`, `<=`                      |
| Ternary           | `... ? ... : ...`                         |
| Assignments       | `\|=`, `^=`, `&=`, `>>>=`, `>>=`, `<<=`, `%=`, `/=`, `*=`, `**=`, `-=`, `+=`, `=`  |
| Arithmetic        | `+`, `-`, `*`, `/`, `%`, `**`             |
| Strict equality   | `!==`, `!==`                              |
| Prefix, postfix, unary   | `--...`, `++...`, `...++`, `...-- `, `+...`, `-...` |
| typeof            | `typeof(some_variable)`                   |
| for..in loop      | `for (let k in obj) { ... }`              |
| delete            | `delete obj.k`                            |
| Variable decl     | `let a, b, c = 12.3, d = 'a'; ` |
| Simple types      | `let a = null, b = undefined, c = false, d = true;` |
| Functions         | `let f = function(x, y) { return x + y; }; ` |
| Objects           | `let obj = {a: 1, f: function(x) { return x * 2}}; obj.f();` |
| Arrays            | `let arr = [1, 2, 'hi there']` |

## Unsupported operations and constructs

| Name              |  Operation                                |
| ----------------- | ----------------------------------------- |
| Equality          | `==`, `!=`  (note: use strict equality `===`, `!==`) |
| Closures          | `let f = function() { let x = 1; return function() { return x; } };`  |
| instanceof        | `instanceof some_variable` |
| const             | `const ...` |
| var               | `var ...` |
| await             | `await ...` |
| void              | `void ...` |
| new               | `new ...` |

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

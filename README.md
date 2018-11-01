# mJS - a JS engine for embedded systems

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Build Status](https://travis-ci.org/cpq/mjs3.svg?branch=master)](https://travis-ci.org/cpq/mjs3)

mJS is a single-header JavaScript engine for microcontrollers.

## Features

- Clean ISO C, ISO C++. Builds on old (VC98) and modern compilers, from 8-bit (e.g. Arduino mini) to 64-bit systems
- No dependencies
- Implements a restricted subset of ES6 with limitations
- Preallocates all necessary memory and never calls `malloc`, `realloc`
  at run time. Upon OOM, the VM is halted
- Object pool, property pool, and string pool sizes are defined at compile time
- The minimal configuration takes only a few hundred bytes of RAM
- RAM usage: an object takes 6 bytes, each property: 16 bytes,
  a string: length + 6 bytes, any other type: 4 bytes
- Strings are byte strings, not Unicode.
  For example, `'ы'.length === 2`, `'ы'[0] === '\xd1'`, `'ы'[1] === '\x8b'`
- Limitations: max string length is 256 bytes, numbers hold
  32-bit float value, no standard JS library
- mJS VM executes JS source directly, no AST/bytecode is generated

## Supported operations and constructs

| Name              |  Operation                   |
| ----------------- | ---------------------------- |
| Operations        | All but `!=`, `==`. Use `!==`, `===` |
| typeof            | `typeof(...)`                |
| delete            | `delete obj.k`               |
| for, while, for..in  | `for (let k in obj) { ... }` |
| Declations        | `let a, b, c = 12.3, d = 'a'; ` |
| Simple types      | `let a = null, b = undefined, c = false, d = true;` |
| Functions         | `let f = function(x, y) { return x + y; }; ` |
| Objects           | `let obj = {a: 1, f: function(x) { return x * 2}}; obj.f();` |
| Arrays            | `let arr = [1, 2, 'hi there']` |

## Unsupported operations and constructs

| Name              |  Operation                                |
| ----------------- | ----------------------------------------- |
| Equality          | `==`, `!=`  (note: use strict equality `===`, `!==`) |
| var               | `var ...`  (note: use `let ...`) |
| Closures          | `let f = function() { let x = 1; return function() { return x; } };`  |
| Const, etc        | `const ...`, `await ...` , `void ...` , `new ...`, `instanceof ...`  |

## JS API Reference

## C API Reference

```c
// Types
typedef uint32_t mjs_val_t;
typedef uint32_t mjs_len_t;

// API
struct mjs *mjs_create(void);    // Create instance
void mjs_destroy(struct mjs *);  // Destroy instance
mjs_val_t mjs_eval(struct mjs *mjs, const char *buf, int len);  // Evaluate
const char *mjs_stringify(struct mjs *, mjs_val_t v);  // Stringify value
unsigned long mjs_size(void);                          // Get VM size

float mjs_get_number(mjs_val_t v);
char *mjs_get_string(struct mjs *, mjs_val_t v, mjs_len_t *len);
```

## Usage example

See `example.c`

## LICENSE

Dual license: GPLv2 or commercial. For commercial
licensing, please contact support@mongoose-os.com

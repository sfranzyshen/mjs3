# Elk - a restricted JS engine for embedded systems

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Build Status](https://travis-ci.org/cpq/elk.svg?branch=master)](https://travis-ci.org/cpq/elk)
[![Code Coverage](https://codecov.io/gh/cpq/elk/branch/master/graph/badge.svg)](https://codecov.io/gh/cpq/elk)


Elk is a single-file JavaScript engine for microcontrollers.

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
- Simple FFI API to inject existing C functions into JS

## Embedded example: blinky in JavaScript on Arduino Mini

```c++
#define MJS_STRING_POOL_SIZE 200    // Buffer for all strings
#include "elk.c"                    // Sketch -> Add File -> elk.c

extern "C" void myDelay(int x) { delay(x); }
extern "C" void myDigitalWrite(int x, int y) { digitalWrite(x, y); }

void setup() {
  struct vm *vm = js_create();                        // Create JS instance
  js_ffi(vm, "delay", (cfn_t) myDelay, "vi");        	// Import delay()
  js_ffi(vm, "write", (cfn_t) myDigitalWrite, "vii"); // Import write()
  js_eval(vm, "while (1) { write(13, 0); delay(100); write(13, 1); delay(100); }", -1);
}

void loop() { delay(1000); }
```

```
Sketch uses 17620 bytes (57%) of program storage space. Maximum is 30720 bytes.
Global variables use 955 bytes (46%) of dynamic memory, leaving 1093 bytes for local variables. Maximum is 2048 bytes.
```

## Supported standard operations and constructs

| Name              |  Operation                   |
| ----------------- | ---------------------------- |
| Operations        | All but `!=`, `==`. Use `!==`, `===` instead |
| typeof            | `typeof(...)`                |
| delete            | `delete obj.k`               |
| while  					  | `while (...) {...}`          |
| Declarations      | `let a, b, c = 12.3, d = 'a'; ` |
| Simple types      | `let a = null, b = undefined, c = false, d = true;` |
| Functions         | `let f = function(x, y) { return x + y; }; ` |
| Objects           | `let obj = {a: 1, f: function(x) { return x * 2}}; obj.f();` |


## Unsupported standard operations and constructs

| Name              |  Operation                                |
| ----------------- | ----------------------------------------- |
| Arrays            | `let arr = [1, 2, 'hi there']` |
| Loops/switch      | `for (...) { ... }`,`for (let k in obj) { ... }`, `do { ... } while (...)`, `switch (...) {...}` |
| Equality          | `==`, `!=`  (note: use strict equality `===`, `!==`) |
| var               | `var ...`  (note: use `let ...`) |
| Closures          | `let f = function() { let x = 1; return function() { return x; } };`  |
| Const, etc        | `const ...`, `await ...` , `void ...` , `new ...`, `instanceof ...`  |
| Standard types    | No `Date`, `ReGexp`, `Function`, `String`, `Number` |
| Prototypes        | No prototype based inheritance |

## Supported non-standard JS API

| Function          |  Description                              |
| ----------------- | ----------------------------------------- |
| `s[offset]`       | Return byte value at `offset`. `s` is either a string, or a number. A number is interprepted as `uint8_t *` pointer. Example: `'abc'[0]` returns 0x61. To read a byte at address `0x100`, use `0x100[0];`. | |


## LICENSE

Dual license: GPLv2 or commercial. For commercial
licensing, please contact support@mongoose-os.com

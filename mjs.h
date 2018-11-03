// Copyright (c) 2013-2018 Cesanta Software Limited
// All rights reserved

#ifndef MJS_H
#define MJS_H

#if defined(__cplusplus)
extern "C" {
#endif

#if !defined(_MSC_VER) || _MSC_VER >= 1700
#include <stdint.h>
#else
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
#define vsnprintf _vsnprintf
#define snprintf _snprintf
#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)
#define __func__ __FILE__ ":" STRINGIFY(__LINE__)
#pragma warning(disable : 4127)
#endif

#define mjs vm  // Aliasing `struct mjs` to `struct vm`

typedef uint32_t mjs_val_t;         // JS value placeholder
typedef uint32_t mjs_len_t;         // String length placeholder
typedef void (*mjs_cfunc_t)(void);  // Native C function, for exporting to JS
typedef enum { CT_FLOAT = 0, CT_CHAR_PTR = 1 } mjs_ctype_t;  // C FFI types

struct mjs *mjs_create(void);            // Create instance
void mjs_destroy(struct mjs *);          // Destroy instance
mjs_val_t mjs_get_global(struct mjs *);  // Get global namespace object
mjs_val_t mjs_eval(struct mjs *, const char *buf, int len);  // Evaluate expr
mjs_val_t mjs_set(struct vm *, mjs_val_t obj, mjs_val_t key,
                  mjs_val_t val);                      // Set attribute
const char *mjs_stringify(struct mjs *, mjs_val_t v);  // Stringify value
unsigned long mjs_size(void);                          // Get VM size

// Converting from C type to mjs_val_t
// Use MJS_UNDEFINED, MJS_NULL, MJS_TRUE, MJS_FALSE for other scalar types
mjs_val_t mjs_mk_obj(struct mjs *);
mjs_val_t mjs_mk_str(struct mjs *, const char *, int len);
mjs_val_t mjs_mk_num(float value);
mjs_val_t mjs_mk_js_func(struct mjs *, const char *, int len);

// Exporting C function into JS
mjs_val_t mjs_inject_0(struct mjs *, const char *name, mjs_cfunc_t f,
                       mjs_ctype_t rettype);
mjs_val_t mjs_inject_1(struct mjs *, const char *name, mjs_cfunc_t f,
                       mjs_ctype_t rettype, mjs_ctype_t t1);
mjs_val_t mjs_inject_2(struct vm *vm, const char *name, mjs_cfunc_t f,
                       mjs_ctype_t rettype, mjs_ctype_t t1, mjs_ctype_t t2);

// Converting from mjs_val_t to C/C++ types
float mjs_to_float(mjs_val_t v);                         // Unpack number
char *mjs_to_str(struct mjs *, mjs_val_t, mjs_len_t *);  // Unpack string

#if defined(__cplusplus)
}
#endif

// VM tunables

#ifndef MJS_DATA_STACK_SIZE
#define MJS_DATA_STACK_SIZE 10
#endif

#ifndef MJS_CALL_STACK_SIZE
#define MJS_CALL_STACK_SIZE 10
#endif

#ifndef MJS_STRING_POOL_SIZE
#define MJS_STRING_POOL_SIZE 64
#endif

#ifndef MJS_OBJ_POOL_SIZE
#define MJS_OBJ_POOL_SIZE 5
#endif

#ifndef MJS_PROP_POOL_SIZE
#define MJS_PROP_POOL_SIZE 10
#endif

#ifndef MJS_CFUNC_POOL_SIZE
#define MJS_CFUNC_POOL_SIZE 5
#endif

#ifndef MJS_ERROR_MESSAGE_SIZE
#define MJS_ERROR_MESSAGE_SIZE 40
#endif

///////////////////////////////// IMPLEMENTATION //////////////////////////
//
// 32bit floating-point number: 1 bit sign, 8 bits exponent, 23 bits mantissa
//
//  7f80 0000 = 01111111 10000000 00000000 00000000 = infinity
//  ff80 0000 = 11111111 10000000 00000000 00000000 = −infinity
//  ffc0 0001 = x1111111 11000000 00000000 00000001 = qNaN (on x86 and ARM)
//  ff80 0001 = x1111111 10000000 00000000 00000001 = sNaN (on x86 and ARM)
//
//  seeeeeee|emmmmmmm|mmmmmmmm|mmmmmmmm
//  11111111|1ttttvvv|vvvvvvvv|vvvvvvvv
//    INF     TYPE     PAYLOAD

#define IS_FLOAT(v) (((v) &0xff800000) != 0xff800000)
#define MK_VAL(t, p) (0xff800000 | ((val_t)(t) << 19) | (p))
#define VAL_TYPE(v) ((mjs_type_t)(((v) >> 19) & 0x0f))
#define VAL_PAYLOAD(v) ((v) & ~0xfff80000)

#define MJS_UNDEFINED MK_VAL(MJS_TYPE_UNDEFINED, 0)
#define MJS_ERROR MK_VAL(MJS_TYPE_ERROR, 0)
#define MJS_TRUE MK_VAL(MJS_TYPE_TRUE, 0)
#define MJS_FALSE MK_VAL(MJS_TYPE_FALSE, 0)
#define MJS_NULL MK_VAL(MJS_TYPE_NULL, 0)

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef mjs_val_t val_t;
typedef mjs_len_t len_t;
typedef uint16_t ind_t;
typedef uint32_t tok_t;
#define INVALID_INDEX ((ind_t) ~0)

// clang-format off
typedef enum {
  MJS_TYPE_UNDEFINED, MJS_TYPE_NULL, MJS_TYPE_TRUE, MJS_TYPE_FALSE,
  MJS_TYPE_STRING, MJS_TYPE_OBJECT, MJS_TYPE_ARRAY, MJS_TYPE_FUNCTION,
  MJS_TYPE_NUMBER, MJS_TYPE_ERROR, MJS_TYPE_C_FUNCTION,
} mjs_type_t;
// clang-format on

struct prop {
  val_t key;
  val_t val;
  ind_t flags;  // see MJS_PROP_* below
  ind_t next;   // index of the next prop, or INVALID_INDEX if last one
};
#define PROP_ALLOCATED 1

struct obj {
  ind_t flags;  // see MJS_OBJ_* defines below
  ind_t props;  // index of the first property, or INVALID_INDEX
};
#define OBJ_ALLOCATED 1
#define OBJ_CALL_ARGS 2  // This oject sits in the call stack, holds call args

struct cfunc {
  mjs_cfunc_t fp;    // Pointer to function
  uint8_t num_args;  // Number of arguments
  uint8_t types[3];  // types[0] is a return type, followed by arg types
};

struct vm {
  char error_message[MJS_ERROR_MESSAGE_SIZE];
  val_t data_stack[MJS_DATA_STACK_SIZE];
  val_t call_stack[MJS_CALL_STACK_SIZE];
  ind_t sp;                               // Points to the top of the data stack
  ind_t csp;                              // Points to the top of the call stack
  struct obj objs[MJS_OBJ_POOL_SIZE];     // Objects pool
  struct prop props[MJS_PROP_POOL_SIZE];  // Props pool
  struct cfunc cfuncs[MJS_CFUNC_POOL_SIZE];  // C functions pool
  uint8_t stringbuf[MJS_STRING_POOL_SIZE];   // String pool
  ind_t sblen;                               // String pool current length
};

#define ARRSIZE(x) ((sizeof(x) / sizeof((x)[0])))

#define DBGPREFIX "[DEBUG] "
#ifdef MJS_DEBUG
#define LOG(x) printf x
#else
#define LOG(x)
#endif

#define TRY(expr)                     \
  do {                                \
    res = expr;                       \
    if (res == MJS_ERROR) return res; \
  } while (0)

//////////////////////////////////// HELPERS /////////////////////////////////
static val_t vm_err(struct vm *vm, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->error_message, sizeof(vm->error_message), fmt, ap);
  va_end(ap);
  // LOG((DBGPREFIX "%s: %s\n", __func__, vm->error_message));
  return MJS_ERROR;
}

union mjs_type_holder {
  val_t v;
  float f;
};

static mjs_type_t mjs_type(val_t v) {
  return IS_FLOAT(v) ? MJS_TYPE_NUMBER : VAL_TYPE(v);
}

static val_t tov(float f) {
  union mjs_type_holder u;
  u.f = f;
  return u.v;
}

static float tof(val_t v) {
  union mjs_type_holder u;
  u.v = v;
  return u.f;
}

static const char *mjs_typeof(val_t v) {
  const char *names[] = {"undefined", "null",   "true",   "false",
                         "string",    "object", "object", "function",
                         "number",    "error",  "cfunc",  "?",
                         "?",         "?",      "?",      "?"};
  return names[mjs_type(v)];
}

static const char *tostr(struct vm *vm, val_t v) {
  static char buf[64];
  mjs_type_t t = mjs_type(v);
  switch (t) {
    case MJS_TYPE_NUMBER: {
      double f = tof(v), iv;
      if (modf(f, &iv) == 0) {
        snprintf(buf, sizeof(buf), "%ld", (long) f);
      } else {
        snprintf(buf, sizeof(buf), "%g", f);
      }
      break;
    }
    case MJS_TYPE_STRING:
    case MJS_TYPE_FUNCTION: {
      len_t len;
      const char *ptr = mjs_to_str(vm, v, &len);
      snprintf(buf, sizeof(buf), "%.*s", len, ptr);
      break;
    }
    case MJS_TYPE_C_FUNCTION:
      snprintf(buf, sizeof(buf), "cfunc@%p", vm->cfuncs[VAL_PAYLOAD(v)].fp);
      break;
    case MJS_TYPE_ERROR:
      snprintf(buf, sizeof(buf), "ERROR: %s", vm->error_message);
      break;
    default:
      snprintf(buf, sizeof(buf), "%s", mjs_typeof(v));
      break;
  }
  return buf;
}

#ifdef MJS_DEBUG
static void vm_dump(const struct vm *vm) {
  ind_t i;
  printf("[VM] %8s: ", "objs");
  for (i = 0; i < ARRSIZE(vm->objs); i++) {
    putchar(vm->objs[i].flags & OBJ_ALLOCATED ? 'v' : '-');
  }
  putchar('\n');
  printf("[VM] %8s: ", "props");
  for (i = 0; i < ARRSIZE(vm->props); i++) {
    putchar(vm->props[i].flags & PROP_ALLOCATED ? 'v' : '-');
  }
  putchar('\n');
  printf("[VM] %8s: ", "cfuncs");
  for (i = 0; i < ARRSIZE(vm->cfuncs); i++) {
    putchar(vm->cfuncs[i].fp ? 'v' : '-');
  }
  putchar('\n');
  printf("[VM] %8s: %d/%d\n", "strings", vm->sblen,
         (int) sizeof(vm->stringbuf));
  printf("[VM]  sp %d, csp %d, sb %d\n", vm->sp, vm->csp, vm->sblen);
}
#else
#define vm_dump(x)
#endif

////////////////////////////////////// VM ////////////////////////////////////
static val_t *vm_top(struct vm *vm) { return &vm->data_stack[vm->sp - 1]; }

static void abandon(struct vm *vm, val_t v) {
  if (mjs_type(v) == MJS_TYPE_OBJECT) {
    ind_t i, obj_index = (ind_t) VAL_PAYLOAD(v);
    struct obj *o = &vm->objs[obj_index];
    o->flags = 0;  // Mark object free
    i = o->props;
    while (i != INVALID_INDEX) {  // Deallocate obj's properties too
      struct prop *prop = &vm->props[i];
      i = prop->next;   // Point to the next property
      prop->flags = 0;  // Mark property free
      if (mjs_type(prop->val) == MJS_TYPE_OBJECT) abandon(vm, prop->val);
    }
  }
}

static val_t vm_push(struct vm *vm, val_t v) {
  if (vm->sp < ARRSIZE(vm->data_stack)) {
    LOG((DBGPREFIX "%s: %s\n", __func__, tostr(vm, v)));
    vm->data_stack[vm->sp] = v;
    vm->sp++;
    return MJS_TRUE;
  } else {
    return vm_err(vm, "stack overflow");
  }
}

static val_t vm_drop(struct vm *vm) {
  if (vm->sp > 0) {
    LOG((DBGPREFIX "%s: %s\n", __func__, tostr(vm, *vm_top(vm))));
    vm->sp--;
    abandon(vm, vm->data_stack[vm->sp]);
    return MJS_TRUE;
  } else {
    return vm_err(vm, "stack underflow");
  }
}

static val_t mk_str(struct vm *vm, const char *p, int n) {
  len_t len = n < 0 ? (len_t) strlen(p) : (len_t) n;
  if (len > 0xff) {
    return vm_err(vm, "string is too long");
  } else if (len + 2 > sizeof(vm->stringbuf) - vm->sblen) {
    return vm_err(vm, "string OOM");
  } else {
    val_t v = MK_VAL(MJS_TYPE_STRING, vm->sblen);
    vm->stringbuf[vm->sblen++] = (uint8_t)(len & 0xff);  // save length
    if (p) memmove(&vm->stringbuf[vm->sblen], p, len);   // copy data
    vm->sblen += (ind_t) len;
    vm->stringbuf[vm->sblen++] = 0;  // nul-terminate
    return v;
  }
}

char *mjs_to_str(struct vm *vm, val_t v, len_t *len) {
  uint8_t *p = vm->stringbuf + VAL_PAYLOAD(v);
  if (len != NULL) *len = p[0];
  return (char *) p + 1;
}

static val_t mjs_concat(struct vm *vm, val_t v1, val_t v2) {
  val_t v = MJS_ERROR;
  len_t n1, n2;
  char *p1 = mjs_to_str(vm, v1, &n1), *p2 = mjs_to_str(vm, v2, &n2);
  if ((v = mk_str(vm, NULL, n1 + n2)) != MJS_ERROR) {
    char *p = mjs_to_str(vm, v, NULL);
    memmove(p, p1, n1);
    memmove(p + n1, p2, n2);
  }
  return v;
}

static val_t mk_cfunc(struct vm *vm) {
  ind_t i;
  for (i = 0; i < ARRSIZE(vm->cfuncs); i++) {
    if (vm->cfuncs[i].fp != NULL) continue;
    return MK_VAL(MJS_TYPE_C_FUNCTION, i);
  }
  return vm_err(vm, "cfunc OOM");
}

static val_t mk_obj(struct vm *vm) {
  ind_t i;
  // Start iterating from 1, because object 0 is always a global object
  for (i = 1; i < ARRSIZE(vm->objs); i++) {
    if (vm->objs[i].flags & OBJ_ALLOCATED) continue;
    vm->objs[i].flags = OBJ_ALLOCATED;
    vm->objs[i].props = INVALID_INDEX;
    return MK_VAL(MJS_TYPE_OBJECT, i);
  }
  return vm_err(vm, "obj OOM");
}

static val_t mk_func(struct vm *vm, const char *code, int len) {
  val_t v = mk_str(vm, code, len);
  if (v != MJS_ERROR) {
    v &= ~((val_t) 0x0f << 19);
    v |= (val_t) MJS_TYPE_FUNCTION << 19;
  }
  return v;
}

static val_t create_scope(struct vm *vm) {
  val_t scope;
  if (vm->csp >= ARRSIZE(vm->call_stack) - 1) {
    return vm_err(vm, "Call stack OOM");
  }
  if ((scope = mk_obj(vm)) == MJS_ERROR) return MJS_ERROR;
  vm->call_stack[vm->csp] = scope;
  vm->csp++;
  return scope;
}

static val_t delete_scope(struct vm *vm) {
  if (vm->csp <= 0 || vm->csp >= ARRSIZE(vm->call_stack)) {
    return vm_err(vm, "Corrupt call stack");
  } else {
    vm->csp--;
    abandon(vm, vm->call_stack[vm->csp]);
    return MJS_TRUE;
  }
}

// Lookup property in a given object
static val_t *findprop(struct vm *vm, val_t obj, const char *ptr, len_t len) {
  ind_t i, obj_index = (ind_t) VAL_PAYLOAD(obj);
  struct obj *o = &vm->objs[obj_index];
  if (obj_index >= ARRSIZE(vm->objs)) {
    vm_err(vm, "corrupt obj, index %x", obj_index);
    return NULL;
  }
  i = o->props;
  while (i != INVALID_INDEX) {
    struct prop *p = &vm->props[i];
    len_t n = 0;
    char *key = mjs_to_str(vm, p->key, &n);
    if (n == len && memcmp(key, ptr, n) == 0) return &vm->props[i].val;
    i = p->next;
  }
  return NULL;
}

// Lookup variable and push its value on stack on success
static val_t *lookup(struct vm *vm, const char *ptr, len_t len) {
  ind_t i;
  for (i = vm->csp; i > 0; i--) {
    val_t scope = vm->call_stack[i - 1];
    val_t *prop = findprop(vm, scope, ptr, len);
    if (prop != NULL) return prop;
  }
  return NULL;
}

// Lookup variable and push its value on stack on success
static val_t lookup_and_push(struct vm *vm, const char *ptr, len_t len) {
  val_t *vp = lookup(vm, ptr, len);
  if (vp != NULL) return vm_push(vm, *vp);
  return vm_err(mjs, "[%.*s] undefined", len, ptr);
}

val_t mjs_set(struct vm *vm, val_t obj, val_t key, val_t val) {
  if (mjs_type(obj) == MJS_TYPE_OBJECT) {
    len_t len;
    const char *ptr = mjs_to_str(vm, key, &len);
    val_t *prop = findprop(vm, obj, ptr, len);
    if (prop != NULL) {
      *prop = val;
      return MJS_TRUE;
    } else {
      ind_t i, obj_index = (ind_t) VAL_PAYLOAD(obj);
      struct obj *o = &vm->objs[obj_index];
      if (obj_index >= ARRSIZE(vm->objs)) {
        return vm_err(vm, "corrupt obj, index %x", obj_index);
      }
      for (i = 0; i < ARRSIZE(vm->props); i++) {
        struct prop *p = &vm->props[i];
        if (p->flags & PROP_ALLOCATED) continue;
        p->flags = PROP_ALLOCATED;
        p->next = o->props;  // Link to the current
        o->props = i;        // props list
        p->key = key;
        p->val = val;
        LOG((DBGPREFIX "%s: %s: ", __func__, tostr(vm, obj)));
        LOG(("%s -> ", tostr(vm, key)));
        LOG(("%s\n", tostr(vm, val)));
        return MJS_TRUE;
      }
      return vm_err(vm, "props OOM");
    }
  } else {
    return vm_err(vm, "setting prop on non-object");
  }
}

static int is_true(struct vm *vm, val_t v) {
  len_t len;
  mjs_type_t t = mjs_type(v);
  return t == MJS_TYPE_TRUE || (t == MJS_TYPE_NUMBER && tof(v) != 0.0) ||
         t == MJS_TYPE_OBJECT || t == MJS_TYPE_FUNCTION ||
         (t == MJS_TYPE_STRING && mjs_to_str(vm, v, &len) && len > 0);
}

////////////////////////////////// TOKENIZER /////////////////////////////////

struct tok {
  tok_t tok, len;
  const char *ptr;
  float num_value;
};

struct parser {
  const char *file_name;  // Source code file name
  const char *buf;        // Nul-terminated source code buffer
  const char *pos;        // Current position
  const char *end;        // End position
  int line_no;            // Line number
  tok_t prev_tok;         // Previous token, for prefix increment / decrement
  struct tok tok;         // Parsed token
  int noexec;             // Parse only, do not execute
  struct vm *vm;
};

#define DT(a, b) ((tok_t)(a) << 8 | (b))
#define TT(a, b, c) ((tok_t)(a) << 16 | (tok_t)(b) << 8 | (c))
#define QT(a, b, c, d) \
  ((tok_t)(a) << 24 | (tok_t)(b) << 16 | (tok_t)(c) << 8 | (d))

// clang-format off
enum {
  TOK_EOF, TOK_INVALID, TOK_NUM, TOK_STR, TOK_IDENT = 200,
  TOK_BREAK, TOK_CASE, TOK_CATCH, TOK_CONTINUE, TOK_DEBUGGER, TOK_DEFAULT,
  TOK_DELETE, TOK_DO, TOK_ELSE, TOK_FALSE, TOK_FINALLY, TOK_FOR, TOK_FUNCTION,
  TOK_IF, TOK_IN, TOK_INSTANCEOF, TOK_NEW, TOK_NULL, TOK_RETURN, TOK_SWITCH,
  TOK_THIS, TOK_THROW, TOK_TRUE, TOK_TRY, TOK_TYPEOF, TOK_VAR, TOK_VOID,
  TOK_WHILE, TOK_WITH, TOK_LET, TOK_UNDEFINED,
  TOK_UNARY_MINUS, TOK_UNARY_PLUS, TOK_POSTFIX_PLUS, TOK_POSTFIX_MINUS,
};
// clang-format on

// We're not relying on the target libc ctype, as it may incorrectly
// handle negative arguments, e.g. isspace(-1).
static int mjs_is_space(int c) {
  return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f' ||
         c == '\v';
}

static int mjs_is_digit(int c) { return c >= '0' && c <= '9'; }

static int mjs_is_alpha(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int mjs_is_ident(int c) {
  return c == '_' || c == '$' || mjs_is_alpha(c);
}

// Try to parse a token that can take one or two chars.
static int longtok(struct parser *p, const char *first_chars,
                   const char *second_chars) {
  if (strchr(first_chars, p->pos[0]) == NULL) return TOK_EOF;
  if (p->pos + 1 < p->end && p->pos[1] != '\0' &&
      strchr(second_chars, p->pos[1]) != NULL) {
    p->tok.len++;
    p->pos++;
    return p->pos[-1] << 8 | p->pos[0];
  }
  return p->pos[0];
}

// Try to parse a token that takes exactly 3 chars.
static int longtok3(struct parser *p, char a, char b, char c) {
  if (p->pos + 2 < p->end && p->pos[0] == a && p->pos[1] == b &&
      p->pos[2] == c) {
    p->tok.len += 2;
    p->pos += 2;
    return ((tok_t) p->pos[-2] << 16) | ((tok_t) p->pos[-1] << 8) | p->pos[0];
  }
  return TOK_EOF;
}

// Try to parse a token that takes exactly 4 chars.
static int longtok4(struct parser *p, char a, char b, char c, char d) {
  if (p->pos + 3 < p->end && p->pos[0] == a && p->pos[1] == b &&
      p->pos[2] == c && p->pos[3] == d) {
    p->tok.len += 3;
    p->pos += 3;
    return ((tok_t) p->pos[-3] << 24) | ((tok_t) p->pos[-2] << 16) |
           ((tok_t) p->pos[-1] << 8) | p->pos[0];
  }
  return TOK_EOF;
}

static int getnum(struct parser *p) {
  if (p->pos[0] == '0' && p->pos[1] == 'x') {
    // MSVC6 strtod cannot parse 0x... numbers, thus this ugly workaround.
    p->tok.num_value = (float) strtoul(p->pos + 2, (char **) &p->pos, 16);
  } else {
    p->tok.num_value = (float) strtod(p->pos, (char **) &p->pos);
  }
  p->tok.len = p->pos - p->tok.ptr;
  p->pos--;
  return TOK_NUM;
}

static int is_reserved_word_token(const char *s, int len) {
  const char *reserved[] = {
      "break",     "case",   "catch", "continue",   "debugger", "default",
      "delete",    "do",     "else",  "false",      "finally",  "for",
      "function",  "if",     "in",    "instanceof", "new",      "null",
      "return",    "switch", "this",  "throw",      "true",     "try",
      "typeof",    "var",    "void",  "while",      "with",     "let",
      "undefined", NULL};
  int i;
  if (!mjs_is_alpha(s[0])) return 0;
  for (i = 0; reserved[i] != NULL; i++) {
    if (len == (int) strlen(reserved[i]) && strncmp(s, reserved[i], len) == 0)
      return i + 1;
  }
  return 0;
}

static int getident(struct parser *p) {
  while (mjs_is_ident(p->pos[0]) || mjs_is_digit(p->pos[0])) p->pos++;
  p->tok.len = p->pos - p->tok.ptr;
  p->pos--;
  return TOK_IDENT;
}

static int getstr(struct parser *p) {
  int quote = *p->pos++;
  p->tok.ptr++;
  while (p->pos[0] != '\0' && p->pos < p->end && p->pos[0] != quote) {
    if (p->pos[0] == '\\' && p->pos[1] != '\0' &&
        (p->pos[1] == quote || strchr("bfnrtv\\", p->pos[1]) != NULL)) {
      p->pos += 2;
    } else {
      p->pos++;
    }
  }
  p->tok.len = p->pos - p->tok.ptr;
  return TOK_STR;
}

static void skip_spaces_and_comments(struct parser *p) {
  const char *pos;
  do {
    pos = p->pos;
    while (p->pos < p->end && mjs_is_space(p->pos[0])) {
      if (p->pos[0] == '\n') p->line_no++;
      p->pos++;
    }
    if (p->pos + 1 < p->end && p->pos[0] == '/' && p->pos[1] == '/') {
      while (p->pos[0] != '\0' && p->pos[0] != '\n') p->pos++;
    }
    if (p->pos + 4 < p->end && p->pos[0] == '/' && p->pos[1] == '*') {
      p->pos += 2;
      while (p->pos < p->end && p->pos[0] != '\0') {
        if (p->pos[0] == '\n') p->line_no++;
        if (p->pos + 1 < p->end && p->pos[0] == '*' && p->pos[1] == '/') {
          p->pos += 2;
          break;
        }
        p->pos++;
      }
    }
  } while (pos < p->pos);
}

static tok_t pnext(struct parser *p) {
  tok_t tmp, tok = TOK_INVALID;

  skip_spaces_and_comments(p);
  p->tok.ptr = p->pos;
  p->tok.len = 1;

  if (p->pos[0] == '\0' || p->pos >= p->end) tok = TOK_EOF;
  if (mjs_is_digit(p->pos[0])) {
    tok = getnum(p);
  } else if (p->pos[0] == '\'' || p->pos[0] == '"') {
    tok = getstr(p);
  } else if (mjs_is_ident(p->pos[0])) {
    tok = getident(p);
    // NOTE: getident() has side effects on `p`, and `is_reserved_word_token()`
    // relies on them. Since in C the order of evaluation of the operands is
    // undefined, `is_reserved_word_token()` should be called in a separate
    // statement.
    tok += is_reserved_word_token(p->tok.ptr, p->tok.len);
  } else if (strchr(",.:;{}[]()?", p->pos[0]) != NULL) {
    tok = p->pos[0];
  } else if ((tmp = longtok3(p, '<', '<', '=')) != TOK_EOF ||
             (tmp = longtok3(p, '>', '>', '=')) != TOK_EOF ||
             (tmp = longtok4(p, '>', '>', '>', '=')) != TOK_EOF ||
             (tmp = longtok3(p, '>', '>', '>')) != TOK_EOF ||
             (tmp = longtok3(p, '=', '=', '=')) != TOK_EOF ||
             (tmp = longtok3(p, '!', '=', '=')) != TOK_EOF ||
             (tmp = longtok(p, "&", "&=")) != TOK_EOF ||
             (tmp = longtok(p, "|", "|=")) != TOK_EOF ||
             (tmp = longtok(p, "<", "<=")) != TOK_EOF ||
             (tmp = longtok(p, ">", ">=")) != TOK_EOF ||
             (tmp = longtok(p, "-", "-=")) != TOK_EOF ||
             (tmp = longtok(p, "+", "+=")) != TOK_EOF) {
    tok = tmp;
  } else if ((tmp = longtok(p, "^~+-%/*<>=!|&", "=")) != TOK_EOF) {
    tok = tmp;
  }
  if (p->pos < p->end && p->pos[0] != '\0') p->pos++;
  p->prev_tok = p->tok.tok;
  p->tok.tok = tok;
  // LOG((DBGPREFIX "%s: tok %d [%c] [%.*s]\n", __func__, tok, tok,
  //     (int) (p->end - p->pos), p->pos));
  return p->tok.tok;
}

////////////////////////////////// PARSER /////////////////////////////////

static val_t parse_statement_list(struct parser *p, tok_t endtok);
static val_t parse_expr(struct parser *p);
static val_t parse_statement(struct parser *p);

#define EXPECT(p, t)                                               \
  do {                                                             \
    if ((p)->tok.tok != (t))                                       \
      return vm_err((p)->vm, "%s: expecting '%c'", __func__, (t)); \
  } while (0)

static struct parser mk_parser(struct vm *vm, const char *buf, int len) {
  struct parser p;
  memset(&p, 0, sizeof(p));
  p.line_no = 1;
  p.buf = p.pos = buf;
  p.end = buf + len;
  p.vm = vm;
  return p;
}

// clang-format off
static int s_assign_ops[] = {
  '=', DT('+', '='), DT('-', '='),  DT('*', '='), DT('/', '='), DT('%', '='),
  TT('<', '<', '='), TT('>', '>', '='), QT('>', '>', '>', '='), DT('&', '='),
  DT('^', '='), DT('|', '='), TOK_EOF
};
// clang-format on
static int s_postfix_ops[] = {DT('+', '+'), DT('-', '-'), TOK_EOF};
static int s_unary_ops[] = {'!',        '~', DT('+', '+'), DT('-', '-'),
                            TOK_TYPEOF, '-', '+',          TOK_EOF};

static int findtok(const int *toks, int tok) {
  int i = 0;
  while (tok != toks[i] && toks[i] != TOK_EOF) i++;
  return toks[i];
}

static val_t do_arith_op(float f1, float f2, int op) {
  float res = 0;
  // clang-format off
  switch (op) {
    case '+': res = f1 + f2; break;
    case '-': res = f1 - f2; break;
    case '*': res = f1 * f2; break;
    case '/': res = f1 / f2; break;
    case '%': res = (float) ((val_t) f1 % (val_t) f2); break;
  }
  // clang-format on
  return tov(res);
}

static val_t do_op(struct parser *p, int op) {
  val_t *top = vm_top(p->vm), a = top[-1], b = top[0];
  if (p->noexec) return MJS_TRUE;
  LOG((DBGPREFIX "%s: sp %d op %c %d\n", __func__, p->vm->sp, op, op));
  LOG((DBGPREFIX "    top-1 %s\n", tostr(p->vm, b)));
  LOG((DBGPREFIX "    top-2 %s\n", tostr(p->vm, a)));
  switch (op) {
    case '+':
      if (mjs_type(a) == MJS_TYPE_STRING && mjs_type(b) == MJS_TYPE_STRING) {
        val_t v = mjs_concat(p->vm, a, b);
        if (v == MJS_ERROR) return v;
        top[-1] = v;
        vm_drop(p->vm);
        break;
      }
    // Fallthrough
    case '-':
    case '*':
    case '/':
    case '%':
      if (mjs_type(a) == MJS_TYPE_NUMBER && mjs_type(b) == MJS_TYPE_NUMBER) {
        top[-1] = do_arith_op(tof(a), tof(b), op);
        vm_drop(p->vm);
      } else {
        return vm_err(p->vm, "apples to apples please");
      }
      break;
    case TOK_POSTFIX_MINUS:
    case TOK_POSTFIX_PLUS: {
      struct prop *prop = &p->vm->props[(ind_t) tof(b)];
      if (mjs_type(prop->val) != MJS_TYPE_NUMBER)
        return vm_err(p->vm, "please no");
      top[0] = prop->val =
          tov(tof(prop->val) + ((op == TOK_POSTFIX_PLUS) ? 1 : -1));
      break;
    }
    case '=': {
      val_t obj = p->vm->call_stack[p->vm->csp - 1];
      val_t res = mjs_set(p->vm, obj, a, b);
      top[0] = a;
      top[-1] = b;
      if (res == MJS_ERROR) return res;
      return vm_drop(p->vm);
    }
    default:
      return vm_err(p->vm, "Unknown op: %c (%d)", op, op);
  }
  return MJS_TRUE;
}

typedef val_t bpf_t(struct parser *p, int prev_op);

static val_t parse_ltr_binop(struct parser *p, bpf_t f1, bpf_t f2,
                             const int *ops, int prev_op) {
  val_t res = MJS_TRUE;
  TRY(f1(p, TOK_EOF));
  if (prev_op != TOK_EOF) TRY(do_op(p, prev_op));
  if (findtok(ops, p->tok.tok) != TOK_EOF) {
    int op = p->tok.tok;
    pnext(p);
    TRY(f2(p, op));
  }
  return res;
}

static val_t parse_rtl_binop(struct parser *p, bpf_t f1, bpf_t f2,
                             const int *ops, int prev_op) {
  val_t res = MJS_TRUE;
  (void) prev_op;
  TRY(f1(p, TOK_EOF));
  if (findtok(ops, p->tok.tok) != TOK_EOF) {
    int op = p->tok.tok;
    pnext(p);
    TRY(f2(p, TOK_EOF));
    TRY(do_op(p, op));
  }
  return res;
}

static tok_t lookahead(struct parser *p) {
  struct parser tmp = *p;
  tok_t tok = pnext(p);
  *p = tmp;
  return tok;
}

static val_t parse_block(struct parser *p, int mkscope) {
  val_t res = MJS_TRUE;
  if (mkscope && !p->noexec) TRY(create_scope(p->vm));
  TRY(parse_statement_list(p, '}'));
  EXPECT(p, '}');
  if (mkscope && !p->noexec) TRY(delete_scope(p->vm));
  return res;
}

static val_t parse_function(struct parser *p) {
  val_t res = MJS_TRUE;
  int arg_no = 0, name_provided = 0;
  struct tok tmp = p->tok;
  LOG((DBGPREFIX "%s: START: [%d]\n", __func__, p->vm->sp));
  p->noexec++;
  pnext(p);
  if (p->tok.tok == TOK_IDENT) {  // Function name provided: function ABC()...
    // struct tok tmp = p->tok;
    name_provided = 1;
    pnext(p);
  }
  EXPECT(p, '(');
  pnext(p);
  // Emit names of function arguments
  while (p->tok.tok != ')') {
    EXPECT(p, TOK_IDENT);
    arg_no++;
    if (lookahead(p) == ',') pnext(p);
    pnext(p);
  }
  EXPECT(p, ')');
  pnext(p);
  TRY(parse_block(p, 0));
  if (name_provided) TRY(do_op(p, '='));
  {
    val_t f = mk_func(p->vm, tmp.ptr, p->tok.ptr - tmp.ptr + 1);
    TRY(f);
    res = vm_push(p->vm, f);
  }
  p->noexec--;
  LOG((DBGPREFIX "%s: STOP: [%d]\n", __func__, p->vm->sp));
  return res;
}

static val_t parse_literal(struct parser *p, tok_t prev_op) {
  val_t res = MJS_TRUE;
  (void) prev_op;
  switch (p->tok.tok) {
    case TOK_NUM:
      if (!p->noexec) TRY(vm_push(p->vm, tov(p->tok.num_value)));
      break;
    case TOK_STR:
      if (!p->noexec) {
        val_t v = mk_str(p->vm, p->tok.ptr, p->tok.len);
        TRY(v);
        TRY(vm_push(p->vm, v));
      }
      break;
    case TOK_IDENT:
      // LOG((DBGPREFIX "%s: IDENT: [%d]\n", __func__, prev_op));
      if (!p->noexec) {
        tok_t prev_tok = p->prev_tok;
        tok_t next_tok = lookahead(p);
        if (!findtok(s_assign_ops, next_tok) &&
            !findtok(s_postfix_ops, next_tok) &&
            !findtok(s_postfix_ops, prev_tok)) {
          // Get value
          res = lookup_and_push(p->vm, p->tok.ptr, p->tok.len);
        } else {
          // Assign
          val_t *v = lookup(p->vm, p->tok.ptr, p->tok.len);
          LOG((DBGPREFIX "%s: AS: [%.*s]\n", __func__, p->tok.len, p->tok.ptr));
          if (v == NULL) {
            return vm_err(p->vm, "doh");
          } else {
            // Push the index of a property that holds this key
            size_t off = offsetof(struct prop, val);
            struct prop *prop = (struct prop *) ((char *) v - off);
            ind_t ind = (ind_t)(prop - p->vm->props);
            LOG((DBGPREFIX "   ind %d\n", ind));
            TRY(vm_push(p->vm, tov(ind)));
          }
        }
      }
      break;
    case TOK_FUNCTION:
      res = parse_function(p);
      break;
    case '(':
      pnext(p);
      res = parse_expr(p);
      EXPECT(p, ')');
      break;
    default:
      return vm_err(p->vm, "Bad literal: [%.*s]", p->tok.len, p->tok.ptr);
      break;
  }
  pnext(p);
  return res;
}

static val_t call_js_function(struct parser *p, val_t f) {
  val_t res = MJS_TRUE;
  ind_t saved_scp = p->vm->csp;
  val_t scope;  // Function to call

  // Create parser for the function code
  len_t code_len;
  char *code = mjs_to_str(p->vm, f, &code_len);
  struct parser p2 = mk_parser(p->vm, code, code_len);

  // Create scope
  TRY(create_scope(p->vm));
  scope = p->vm->call_stack[p->vm->csp - 1];
  LOG((DBGPREFIX "%s: %d [%.*s]\n", __func__, mjs_type(scope), code_len, code));

  // Skip `function(` in the function definition
  pnext(&p2);
  pnext(&p2);
  pnext(&p2);  // Now p2.tok points either to the first argument, or to the ')'

  // Parse parameters, append them to the scope
  while (p->tok.tok != ')') {
    // Evaluate next argument - pushed to the data_stack
    TRY(parse_expr(p));
    if (p->tok.tok == ',') pnext(p);
    // Check whether we have a defined name for this argument
    if (p2.tok.tok == TOK_IDENT) {
      val_t val = *vm_top(p->vm);
      val_t key = mk_str(p->vm, p2.tok.ptr, p2.tok.len);
      TRY(key);
      TRY(mjs_set(p->vm, scope, key, val));
      pnext(&p2);
    }
    vm_drop(p->vm);  // Drop argument value from the data_stack
    LOG((DBGPREFIX "%s: P sp %d\n", __func__, p->vm->sp));
  }
  while (p2.tok.tok != '{') pnext(&p2);  // Consume any leftover arguments
  res = parse_block(&p2, 0);             // Execute function body
  LOG((DBGPREFIX "%s: R sp %d\n", __func__, p->vm->sp));
  while (p->vm->csp > saved_scp) delete_scope(p->vm);  // Restore current scope
  return res;
}

static val_t call_c_function(struct parser *p, val_t f) {
  struct cfunc *cfunc = &p->vm->cfuncs[VAL_PAYLOAD(f)];
  uint8_t num_args = 0;
  val_t res = MJS_UNDEFINED;
  while (p->tok.tok != ')') {
    TRY(parse_expr(p));  // Push next arg to the data_stack
    if (p->tok.tok == ',') pnext(p);
    num_args++;
  }
  if (cfunc->num_args != num_args) {
    res = vm_err(p->vm, "expecting %d args", (int) cfunc->num_args);
  } else {
    val_t *top = vm_top(p->vm), v;
    uint8_t *t = cfunc->types;
    int choice = cfunc->num_args | (t[0] << 2) | (t[1] << 3) | (t[2] << 4);
    LOG((DBGPREFIX "%s: %d\n", __func__, choice));
    switch (choice) {
      case 0:  // 000: ret CT_FLOAT, nargs 0
        v = tov(((float (*)(void)) cfunc->fp)());
        break;
      case 4:  // 100: ret CT_CHAR_PTR, nargs 0
        v = mk_str(p->vm, ((char *(*) (void) ) cfunc->fp)(), -1);
        break;
      case 1:  // 0001: CT_FLOAT, ret CT_FLOAT, nargs 1
        v = tov(((float (*)(float)) cfunc->fp)(tof(top[0])));
        break;
      case 5:  // 0101: CT_FLOAT, ret CT_CHAR_PTR, nargs 1
        v = mk_str(p->vm, ((char *(*) (float) ) cfunc->fp)(tof(top[0])), -1);
        break;
      case 9:  // 1001: CT_CHAR_PTR, ret CT_FLOAT, nargs 1
        v = tov(
            ((float (*)(char *)) cfunc->fp)(mjs_to_str(p->vm, top[0], NULL)));
        break;
      case 13:  // 1101: CT_CHAR_PTR, ret CT_CHAR_PTR, nargs 1
        v = mk_str(
            p->vm,
            ((char *(*) (char *) ) cfunc->fp)(mjs_to_str(p->vm, top[0], NULL)),
            -1);
        break;
      case 2:  // 00010: CT_FLOAT, CT_FLOAT, ret CT_FLOAT, nargs 2
        v = tov(
            ((float (*)(float, float)) cfunc->fp)(tof(top[-1]), tof(top[0])));
        break;
      case 14:  // 01110: CT_FLOAT, CT_CHAR_PTR, ret CT_CHAR_PTR, nargs 2
        v = mk_str(p->vm,
                   ((char *(*) (char *, float) ) cfunc->fp)(
                       mjs_to_str(p->vm, top[-1], NULL), tof(top[0])),
                   -1);
        break;
      default:
        v = vm_err(p->vm, "unsupported FFI");
        break;
    }
    while (num_args-- > 0) vm_drop(p->vm);  // Abandon pushed args
    vm_drop(p->vm);                         // Abandon function object
    res = vm_push(p->vm, v);                // Push call result
  }
  LOG((DBGPREFIX "%s: %d\n", __func__, p->tok.tok));
  return res;
}

static val_t parse_call_dot_mem(struct parser *p, int prev_op) {
  val_t res = MJS_TRUE;
  TRY(parse_literal(p, p->tok.tok));
  while (p->tok.tok == '.' || p->tok.tok == '(' || p->tok.tok == '[') {
    if (p->tok.tok == '[') {
      tok_t prev_tok = p->prev_tok;
      pnext(p);
      TRY(parse_expr(p));
      // emit_byte(p, OP_SWAP);
      EXPECT(p, ']');
      pnext(p);
      if (!findtok(s_assign_ops, p->tok.tok) &&
          !findtok(s_postfix_ops, p->tok.tok) &&
          !findtok(s_postfix_ops, prev_tok)) {
        // emit_byte(p, OP_GET);
      }
    } else if (p->tok.tok == '(') {
      pnext(p);
      if (p->noexec) {
        while (p->tok.tok != ')') {
          TRY(parse_expr(p));
          if (p->tok.tok == ',') pnext(p);
        }
        return res;
      } else {
        val_t f = *vm_top(p->vm);
        mjs_type_t t = mjs_type(f);
        if (t == MJS_TYPE_FUNCTION) {
          res = call_js_function(p, f);
        } else if (t == MJS_TYPE_C_FUNCTION) {
          res = call_c_function(p, f);
        } else {
          res = vm_err(p->vm, "calling non-func");
        }
      }
      EXPECT(p, ')');
      pnext(p);
    } else if (p->tok.tok == '.') {
      pnext(p);
      TRY(parse_call_dot_mem(p, '.'));
    }
  }
  (void) prev_op;
  return res;
}

static val_t parse_postfix(struct parser *p, int prev_op) {
  val_t res = MJS_TRUE;
  TRY(parse_call_dot_mem(p, prev_op));
  if (p->tok.tok == DT('+', '+') || p->tok.tok == DT('-', '-')) {
    int op = p->tok.tok == DT('+', '+') ? TOK_POSTFIX_PLUS : TOK_POSTFIX_MINUS;
    TRY(do_op(p, op));
    pnext(p);
  }
  return res;
}

static val_t parse_unary(struct parser *p, int prev_op) {
  val_t res = MJS_TRUE;
  int op = TOK_EOF;
  if (findtok(s_unary_ops, p->tok.tok) != TOK_EOF) {
    op = p->tok.tok;
    pnext(p);
  }
  if (findtok(s_unary_ops, p->tok.tok) != TOK_EOF) {
    res = parse_unary(p, prev_op);
  } else {
    res = parse_postfix(p, prev_op);
  }
  if (res == MJS_ERROR) return res;
  if (op != TOK_EOF) {
    if (op == '-') op = TOK_UNARY_MINUS;
    if (op == '+') op = TOK_UNARY_PLUS;
    do_op(p, op);
  }
  return res;
}

static val_t parse_mul_div_rem(struct parser *p, int prev_op) {
  int ops[] = {'*', '/', '%', TOK_EOF};
  return parse_ltr_binop(p, parse_unary, parse_mul_div_rem, ops, prev_op);
}

static val_t parse_plus_minus(struct parser *p, int prev_op) {
  int ops[] = {'+', '-', TOK_EOF};
  return parse_ltr_binop(p, parse_mul_div_rem, parse_plus_minus, ops, prev_op);
}

static val_t parse_ternary(struct parser *p, int prev_op) {
  val_t res = MJS_TRUE;
  TRY(parse_plus_minus(p, TOK_EOF));
  if (prev_op != TOK_EOF) do_op(p, prev_op);
  if (p->tok.tok == '?') {
    pnext(p);
    TRY(parse_ternary(p, TOK_EOF));
    EXPECT(p, ':');
    pnext(p);
    TRY(parse_ternary(p, TOK_EOF));
  }
  return res;
}

static val_t parse_assignment(struct parser *p, int pop) {
  return parse_rtl_binop(p, parse_ternary, parse_assignment, s_assign_ops, pop);
}

static val_t parse_expr(struct parser *p) {
  return parse_assignment(p, TOK_EOF);
}

static val_t parse_let(struct parser *p) {
  val_t res = MJS_TRUE;
  pnext(p);
  for (;;) {
    struct tok tmp = p->tok;
    val_t obj = p->vm->call_stack[p->vm->csp - 1], key, val = MJS_UNDEFINED;
    if (p->tok.tok != TOK_IDENT) return vm_err(p->vm, "indent expected");
    if (findprop(p->vm, obj, p->tok.ptr, p->tok.len) != NULL) {
      return vm_err(p->vm, "[%.*s] already declared", p->tok.len, p->tok.ptr);
    }
    pnext(p);
    if (p->tok.tok == '=') {
      pnext(p);
      TRY(parse_expr(p));
      val = *vm_top(p->vm);
    } else if (!p->noexec) {
      vm_push(p->vm, val);
    }
    key = mk_str(p->vm, tmp.ptr, tmp.len);
    TRY(key);
    TRY(mjs_set(p->vm, obj, key, val));
    LOG((DBGPREFIX "%s: sp %d, %d\n", __func__, p->vm->sp, p->tok.tok));
    if (p->tok.tok == ',') {
      TRY(vm_drop(p->vm));
      pnext(p);
    }
    if (p->tok.tok == ';' || p->tok.tok == TOK_EOF) break;
  }
  return res;
}

static val_t parse_return(struct parser *p) {
  val_t res = MJS_TRUE;
  pnext(p);
  // It is either "return;" or "return EXPR;"
  if (p->tok.tok == ';' || p->tok.tok == '}') {
    if (!p->noexec) vm_push(p->vm, MJS_UNDEFINED);
  } else {
    res = parse_expr(p);
  }
  // Point parser to the end of func body, so that parse_block() gets '}'
  if (!p->noexec) p->pos = p->end - 1;
  return res;
}

static val_t parse_block_or_stmt(struct parser *p, int create_scope) {
  if (lookahead(p) == '{') {
    return parse_block(p, create_scope);
  } else {
    return parse_statement(p);
  }
}

static val_t parse_while(struct parser *p) {
  val_t res = MJS_TRUE;
  struct parser tmp;
  pnext(p);
  EXPECT(p, '(');
  pnext(p);
  tmp = *p;  // Remember the location of the condition expression
  for (;;) {
    *p = tmp;  // On each iteration, re-evaluate the condition
    TRY(parse_expr(p));
    EXPECT(p, ')');
    pnext(p);
    if (is_true(p->vm, *vm_top(p->vm))) {
      // Condition is true. Drop evaluated condition expression from the stack
      if (!p->noexec) vm_drop(p->vm);
    } else {
      // TODO(lsm): optimise here, p->pos = post_condition if it is saved
      p->noexec++;
      LOG((DBGPREFIX "%s: FALSE!!.., sp %d\n", __func__, p->vm->sp));
    }
    TRY(parse_block_or_stmt(p, 1));
    LOG((DBGPREFIX "%s: done.., sp %d\n", __func__, p->vm->sp));
    if (p->noexec) break;
    vm_drop(p->vm);
    vm_dump(p->vm);
  }
  LOG((DBGPREFIX "%s: out.., sp %d\n", __func__, p->vm->sp));
  p->noexec = tmp.noexec;
  return res;
}

static val_t parse_statement(struct parser *p) {
  switch (p->tok.tok) {
    case ';':
      pnext(p);
      return MJS_TRUE;
    case TOK_LET:
      return parse_let(p);
    case '{': {
      val_t res = parse_block(p, 1);
      pnext(p);
      return res;
    }
    case TOK_RETURN:
      return parse_return(p);
    case TOK_WHILE:
      return parse_while(p);
// clang-format off
#if 0
    case TOK_FOR: return parse_for(p);
    case TOK_BREAK: pnext1(p); return MJS_SUCCESS;
    case TOK_CONTINUE: pnext1(p); return MJS_SUCCESS;
    case TOK_IF: return parse_if(p);
#endif
    case TOK_CASE: case TOK_CATCH: case TOK_DELETE: case TOK_DO:
    case TOK_INSTANCEOF: case TOK_NEW: case TOK_SWITCH: case TOK_THROW:
    case TOK_TRY: case TOK_VAR: case TOK_VOID: case TOK_WITH:
      // clang-format on
      return vm_err(p->vm, "[%.*s] not implemented", p->tok.len, p->tok.ptr);
    default: {
      val_t res = MJS_TRUE;
      for (;;) {
        TRY(parse_expr(p));
        if (p->tok.tok != ',') break;
        pnext(p);
      }
      return res;
    }
  }
}

static val_t parse_statement_list(struct parser *p, tok_t endtok) {
  val_t res = MJS_TRUE;
  pnext(p);
  LOG((DBGPREFIX "%s: tok %d endtok %d\n", __func__, p->tok.tok, endtok));
  while (res != MJS_ERROR && p->tok.tok != TOK_EOF && p->tok.tok != endtok) {
    // Drop previous value from the stack
    if (!p->noexec && p->vm->sp > 0) vm_drop(p->vm);
    res = parse_statement(p);
    while (p->tok.tok == ';') pnext(p);
  }
  return res;
}

/////////////////////////////// EXTERNAL API /////////////////////////////////

struct vm *mjs_create(void) {
  struct vm *vm = (struct mjs *) calloc(1, sizeof(*vm));
  vm->objs[0].flags = OBJ_ALLOCATED;
  vm->objs[0].props = INVALID_INDEX;
  vm->call_stack[0] = MK_VAL(MJS_TYPE_OBJECT, 0);
  vm->csp++;
  LOG((DBGPREFIX "%s: size %d bytes\n", __func__, (int) sizeof(*vm)));
  return vm;
};

void mjs_destroy(struct vm *vm) { free(vm); }

val_t mjs_eval(struct vm *vm, const char *buf, int len) {
  struct parser p = mk_parser(vm, buf, len >= 0 ? len : (int) strlen(buf));
  val_t v = MJS_ERROR;
  vm->error_message[0] = '\0';
  if (parse_statement_list(&p, TOK_EOF) != MJS_ERROR && vm->sp == 1) {
    v = *vm_top(vm);
  }
  vm_dump(vm);
  LOG((DBGPREFIX "%s: %s\n", __func__, tostr(vm, *vm_top(vm))));
  return v;
}

mjs_val_t mjs_mk_c_func(struct vm *vm, mjs_cfunc_t f, uint8_t num_args,
                        mjs_ctype_t *types) {
  val_t v = mk_cfunc(vm);
  uint8_t i = 0;
  struct cfunc *cfunc = &vm->cfuncs[VAL_PAYLOAD(v)];
  if (v == MJS_ERROR) return v;
  if (num_args > (uint8_t) ARRSIZE(cfunc->types)) return vm_err(vm, "cmon!");
  cfunc->fp = f;
  cfunc->num_args = num_args;
  cfunc->types[0] = (uint8_t) types[0];
  for (i = 0; i < num_args; i++) cfunc->types[i + 1] = (uint8_t) types[i + 1];
  return v;
}

mjs_val_t mjs_inject(struct vm *vm, const char *p, mjs_cfunc_t f, uint8_t nargs,
                     mjs_ctype_t *types) {
  return mjs_set(mjs, mjs_get_global(mjs), mjs_mk_str(vm, p, -1),
                 mjs_mk_c_func(vm, f, nargs, types));
}

mjs_val_t mjs_inject_0(struct vm *vm, const char *p, mjs_cfunc_t f,
                       mjs_ctype_t rettype) {
  return mjs_set(mjs, mjs_get_global(mjs), mjs_mk_str(vm, p, -1),
                 mjs_mk_c_func(vm, f, 0, &rettype));
}

mjs_val_t mjs_inject_1(struct vm *vm, const char *p, mjs_cfunc_t f,
                       mjs_ctype_t rettype, mjs_ctype_t t1) {
  mjs_ctype_t types[2];
  types[0] = rettype;
  types[1] = t1;
  return mjs_set(mjs, mjs_get_global(mjs), mjs_mk_str(vm, p, -1),
                 mjs_mk_c_func(vm, f, 1, types));
}

mjs_val_t mjs_inject_2(struct vm *vm, const char *p, mjs_cfunc_t f,
                       mjs_ctype_t rettype, mjs_ctype_t t1, mjs_ctype_t t2) {
  mjs_ctype_t types[3];
  types[0] = rettype;
  types[1] = t1;
  types[2] = t2;
  return mjs_set(mjs, mjs_get_global(mjs), mjs_mk_str(vm, p, -1),
                 mjs_mk_c_func(vm, f, 2, types));
}

float mjs_to_float(val_t v) { return tof(v); }
mjs_val_t mjs_mk_str(struct vm *vm, const char *s, int len) {
  return mk_str(vm, s, len);
}
mjs_val_t mjs_mk_obj(struct vm *vm) { return mk_obj(vm); }
mjs_val_t mjs_mk_num(float f) { return tov(f); }
mjs_val_t mjs_get_global(struct vm *vm) { return vm->call_stack[0]; }
const char *mjs_stringify(struct vm *vm, val_t v) { return tostr(vm, v); }

#endif  // MJS_H

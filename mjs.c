// Copyright (c) 2013-2018 Cesanta Software Limited
// All rights reserved

#include "mjs.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef mjs_val_t val_t;
typedef mjs_err_t err_t;
typedef mjs_len_t len_t;
typedef struct mjs vm;
typedef unsigned short ind_t;
typedef unsigned int tok_t;
#define INVALID_INDEX ((ind_t) ~0)

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

struct vm {
  char error_message[MJS_ERROR_MESSAGE_SIZE];
  val_t data_stack[MJS_DATA_STACK_SIZE];
  val_t call_stack[MJS_CALL_STACK_SIZE];
  ind_t sp;                               // Points to the top of the data stack
  ind_t csp;                              // Points to the top of the call stack
  struct obj objs[MJS_OBJ_POOL_SIZE];     // Objects pool
  struct prop props[MJS_PROP_POOL_SIZE];  // Props pool
  char stringbuf[MJS_STRING_POOL_SIZE];   // String pool
  ind_t sblen;                            // String pool current length
};

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
#define VAL_TYPE(v) (((v) >> 19) & 0x0f)
#define VAL_PAYLOAD(v) ((v) & ~0xfff80000)

#define ARRSIZE(x) ((sizeof(x) / sizeof((x)[0])))

#define DBGPREFIX "[DEBUG] "
#ifdef MJS_DEBUG
#define LOG(x) printf x
#else
#define LOG(x)
#endif

#if !defined(_MSC_VER) || _MSC_VER >= 1700
#else
#define vsnprintf _vsnprintf
#define snprintf _snprintf
#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)
#define __func__ __FILE__ ":" STRINGIFY(__LINE__)
#pragma warning(disable : 4127)
#endif

#define TRY(expr)                       \
  do {                                  \
    res = expr;                         \
    if (res != MJS_SUCCESS) return res; \
  } while (0)

//////////////////////////////////// HELPERS /////////////////////////////////
static err_t vm_err(struct vm *vm, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->error_message, sizeof(vm->error_message), fmt, ap);
  va_end(ap);
  LOG((DBGPREFIX "%s: %s\n", __func__, vm->error_message));
  return MJS_FAILURE;
}

static val_t mjs_tov(float f) {
  union {
    val_t v;
    float f;
  } u;
  u.f = f;
  return u.v;
}

static float mjs_tof(val_t v) {
  union {
    val_t v;
    float f;
  } u;
  u.v = v;
  return u.f;
}

#ifdef MJS_DEBUG
static const char *tostr(struct vm *vm, val_t v) {
  static char buf[256];
  const char *names[] = {"(undefined)", "(null)",   "(true)",  "(false)",
                         "(string)",    "(object)", "(array)", "(function)",
                         "(number)",    "?",        "?",       "?",
                         "?",           "?",        "?",       "?"};
  mjs_type_t t = mjs_type(v);
  switch (t) {
    case MJS_NUMBER:
      snprintf(buf, sizeof(buf), "(number) %g", mjs_tof(v));
      break;
    case MJS_STRING:
    case MJS_FUNCTION:
      snprintf(buf, sizeof(buf), "%s [%s]", names[t], mjs_get_string(vm, v, 0));
      break;
    default:
      snprintf(buf, sizeof(buf), "%s", names[t]);
      break;
  }
  return buf;
}

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
  printf("[VM] %8s: %d/%d\n", "strings", vm->sblen,
         (int) sizeof(vm->stringbuf));
}
#endif

static val_t mkval(mjs_type_t t, ind_t payload) {
  val_t v = MK_VAL(t, payload);
  return v;
}

////////////////////////////////////// VM ////////////////////////////////////
mjs_type_t mjs_type(val_t v) { return IS_FLOAT(v) ? MJS_NUMBER : VAL_TYPE(v); }

static val_t *vm_top(struct vm *vm) { return &vm->data_stack[vm->sp - 1]; }
float mjs_get_number(val_t v) { return mjs_tof(v); }

static err_t vm_push(struct vm *vm, val_t v) {
  if (vm->sp < ARRSIZE(vm->data_stack)) {
    LOG((DBGPREFIX "%s: %s\n", __func__, tostr(vm, v)));
    vm->data_stack[vm->sp] = v;
    vm->sp++;
    return MJS_SUCCESS;
  } else {
    return vm_err(vm, "stack overflow");
  }
}

static err_t vm_drop(struct vm *vm) {
  if (vm->sp > 0) {
    LOG((DBGPREFIX "%s: %s\n", __func__, tostr(vm, *vm_top(vm))));
    vm->sp--;
    return MJS_SUCCESS;
  } else {
    return vm_err(vm, "stack underflow");
  }
}

static err_t mk_str(struct vm *vm, const char *p, len_t len, val_t *v) {
  if (len + 3 > sizeof(vm->stringbuf) - vm->sblen) {
    return vm_err(vm, "stringbuf too small");
  } else {
    *v = mkval(MJS_STRING, vm->sblen);
    vm->stringbuf[vm->sblen++] = (char) ((len >> 8) & 0xff);  // save
    vm->stringbuf[vm->sblen++] = (char) (len & 0xff);         // length
    if (p) memmove(&vm->stringbuf[vm->sblen], p, len);        // copy data
    vm->sblen += len;                   // data will be here
    vm->stringbuf[vm->sblen++] = '\0';  // nul-terminate
    return MJS_SUCCESS;
  }
}

char *mjs_get_string(struct vm *vm, val_t v, len_t *len) {
  char *p = vm->stringbuf + VAL_PAYLOAD(v);
  if (len != NULL) *len = (unsigned char) p[0] << 8 | (unsigned char) p[1];
  return p + 2;
}

static err_t mjs_concat(struct vm *vm, val_t v1, val_t v2, val_t *v) {
  err_t res = MJS_SUCCESS;
  len_t n1, n2;
  char *p1 = mjs_get_string(vm, v1, &n1), *p2 = mjs_get_string(vm, v2, &n2);
  if ((res = mk_str(vm, NULL, n1 + n2, v)) == MJS_SUCCESS) {
    char *p = mjs_get_string(vm, *v, NULL);
    memmove(p, p1, n1);
    memmove(p + n1, p2, n2);
  }
  return res;
}

static val_t mk_obj(struct vm *vm) {
  ind_t i;
  // Start iterating from 1, because object 0 is always a global object
  for (i = 1; i < ARRSIZE(vm->objs); i++) {
    if (vm->objs[i].flags & OBJ_ALLOCATED) continue;
    vm->objs[i].flags = OBJ_ALLOCATED;
    vm->objs[i].props = INVALID_INDEX;
    return mkval(MJS_OBJECT, i);
  }
  vm_err(vm, "obj OOM");
  return mkval(MJS_UNDEFINED, 0);
}

static err_t mk_func(struct vm *vm, const char *code, int len, val_t *v) {
  err_t res = mk_str(vm, code, len, v);
  if (res == MJS_SUCCESS) {
    *v &= ~(0x0f << 19);
    *v |= MJS_FUNCTION << 19;
  }
  return MJS_SUCCESS;
}

static err_t create_scope(struct vm *vm) {
  if (vm->csp >= ARRSIZE(vm->call_stack) - 1) {
    return vm_err(vm, "Call stack OOM");
  }
  vm->call_stack[vm->csp] = mk_obj(vm);  // This can set error_message
  if (vm->error_message[0] != '\0') return MJS_FAILURE;
  vm->csp++;
  return MJS_SUCCESS;
}

static err_t delete_scope(struct vm *vm) {
  if (vm->csp <= 0 || vm->csp >= ARRSIZE(vm->call_stack)) {
    return vm_err(vm, "Corrupt call stack");
  }
  vm->csp--;
  return MJS_SUCCESS;
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
    char *key = mjs_get_string(vm, p->key, &n);
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
static err_t lookup_and_push(struct vm *vm, const char *ptr, len_t len) {
  val_t *vp = lookup(vm, ptr, len);
  if (vp != NULL) return vm_push(vm, *vp);
  return vm_err(mjs, "[%.*s] undefined", len, ptr);
}

static err_t mjs_set(struct vm *vm, val_t obj, val_t key, val_t val) {
  if (mjs_type(obj) == MJS_OBJECT) {
    len_t len;
    const char *ptr = mjs_get_string(vm, key, &len);
    val_t *prop = findprop(vm, obj, ptr, len);
    if (prop != NULL) {
      *prop = val;
      return MJS_SUCCESS;
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
        return MJS_SUCCESS;
      }
      return vm_err(vm, "props OOM");
    }
  } else {
    return vm_err(vm, "setting prop on non-object");
  }
}

static int is_true(struct vm *vm, val_t v) {
  len_t len;
  return mjs_type(v) == MJS_TRUE ||
         (mjs_type(v) == MJS_NUMBER && mjs_tof(v) != 0.0) ||
         mjs_type(v) == MJS_OBJECT || mjs_type(v) == MJS_FUNCTION ||
         (mjs_type(v) == MJS_STRING && mjs_get_string(vm, v, &len) && len > 0);
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

#define DT(a, b) ((a) << 8 | (b))
#define TT(a, b, c) ((a) << 16 | (b) << 8 | (c))
#define QT(a, b, c, d) ((a) << 24 | (b) << 16 | (c) << 8 | (d))

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
    return p->pos[-2] << 16 | p->pos[-1] << 8 | p->pos[0];
  }
  return TOK_EOF;
}

// Try to parse a token that takes exactly 4 chars.
static int longtok4(struct parser *p, char a, char b, char c, char d) {
  if (p->pos + 3 < p->end && p->pos[0] == a && p->pos[1] == b &&
      p->pos[2] == c && p->pos[3] == d) {
    p->tok.len += 3;
    p->pos += 3;
    return p->pos[-3] << 24 | p->pos[-2] << 16 | p->pos[-1] << 8 | p->pos[0];
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

static err_t parse_statement_list(struct parser *p, tok_t endtok);
static err_t parse_expr(struct parser *p);
static err_t parse_statement(struct parser *p);

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
  return mjs_tov(res);
}

static err_t do_op(struct parser *p, int op) {
  val_t *top = vm_top(p->vm), a = top[-1], b = top[0];
  if (p->noexec) return MJS_SUCCESS;
  LOG((DBGPREFIX "%s: op %c %d\n", __func__, op, op));
  LOG((DBGPREFIX "    top-1 %s\n", tostr(p->vm, a)));
  LOG((DBGPREFIX "    top-2 %s\n", tostr(p->vm, b)));
  switch (op) {
    case '+':
      if (mjs_type(a) == MJS_STRING && mjs_type(b) == MJS_STRING) {
        val_t v;
        err_t res = mjs_concat(p->vm, a, b, &v);
        if (res != MJS_SUCCESS) return res;
        top[-1] = v;
        p->vm->sp--;
        break;
      }
    // Fallthrough
    case '-':
    case '*':
    case '/':
    case '%':
      if (mjs_type(a) == MJS_NUMBER && mjs_type(b) == MJS_NUMBER) {
        top[-1] = do_arith_op(mjs_tof(a), mjs_tof(b), op);
        p->vm->sp--;
      } else {
        return vm_err(p->vm, "apples to apples please");
      }
      break;
    case TOK_POSTFIX_MINUS: {
      len_t len;
      const char *name = mjs_get_string(p->vm, top[-1], &len);
      float *vp = (float *) lookup(p->vm, name, len);
      if (vp == NULL) return vm_err(p->vm, "\"%.*s\" undefined", len, name);
      (*vp)--;
      top[0] = mjs_tov(*vp);
      break;
    }
    case '=': {
      val_t obj = p->vm->call_stack[p->vm->csp - 1];
      err_t res = mjs_set(p->vm, obj, a, b);
      val_t tmp = a;
      top[-1] = b;
      top[0] = tmp;
      if (res != MJS_SUCCESS) return res;
      return vm_drop(p->vm);
    }
    default:
      return vm_err(p->vm, "Unknown op: %c (%d)", op, op);
  }
  return MJS_SUCCESS;
}

typedef err_t bpf_t(struct parser *p, int prev_op);

static err_t parse_ltr_binop(struct parser *p, bpf_t f1, bpf_t f2,
                             const int *ops, int prev_op) {
  err_t res = MJS_SUCCESS;
  TRY(f1(p, TOK_EOF));
  if (prev_op != TOK_EOF) TRY(do_op(p, prev_op));
  if (findtok(ops, p->tok.tok) != TOK_EOF) {
    int op = p->tok.tok;
    pnext(p);
    TRY(f2(p, op));
  }
  return res;
}

static err_t parse_rtl_binop(struct parser *p, bpf_t f1, bpf_t f2,
                             const int *ops, int prev_op) {
  err_t res = MJS_SUCCESS;
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

static err_t parse_block(struct parser *p, int mkscope) {
  err_t res = MJS_SUCCESS;
  (void) mkscope;
  if (mkscope && !p->noexec) TRY(create_scope(p->vm));
  TRY(parse_statement_list(p, '}'));
  EXPECT(p, '}');
  if (mkscope && !p->noexec) TRY(delete_scope(p->vm));
  return res;
}

static err_t parse_function(struct parser *p) {
  err_t res = MJS_SUCCESS;
  int arg_no = 0, name_provided = 0;
  struct tok tmp = p->tok;
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
    val_t f;
    TRY(mk_func(p->vm, tmp.ptr, p->tok.ptr - tmp.ptr + 1, &f));
    res = vm_push(p->vm, f);
  }
  p->noexec--;
  return res;
}

static err_t parse_literal(struct parser *p, tok_t prev_op) {
  err_t res = MJS_SUCCESS;
  (void) prev_op;
  switch (p->tok.tok) {
    case TOK_NUM:
      if (!p->noexec) TRY(vm_push(p->vm, mjs_tov(p->tok.num_value)));
      break;
    case TOK_STR:
      if (!p->noexec) {
        val_t v;
        TRY(mk_str(p->vm, p->tok.ptr, p->tok.len, &v));
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
          val_t v;
          TRY(mk_str(p->vm, p->tok.ptr, p->tok.len, &v));
          TRY(vm_push(p->vm, v));
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

static err_t do_call(struct parser *p) {
  err_t res = MJS_SUCCESS;
  ind_t saved_scp = p->vm->csp;
  val_t scope, f = *vm_top(p->vm);  // Function to call

  // Create parser for the function code
  len_t code_len;
  char *code = mjs_get_string(p->vm, f, &code_len);
  struct parser p2 = mk_parser(p->vm, code, code_len);

  // Fail if not a function
  if (mjs_type(f) != MJS_FUNCTION) return vm_err(p->vm, "calling non-func");

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
      val_t key, val = *vm_top(p->vm);
      TRY(mk_str(p->vm, p2.tok.ptr, p2.tok.len, &key));
      TRY(mjs_set(p->vm, scope, key, val));
      pnext(&p2);
    }
    p->vm->sp--;  // Drop argument value from the data_stack
    LOG((DBGPREFIX "%s: P sp %d\n", __func__, p->vm->sp));
  }
  while (p2.tok.tok != '{') pnext(&p2);  // Consume any leftover arguments
  res = parse_block(&p2, 0);             // Execute function body
  LOG((DBGPREFIX "%s: R sp %d\n", __func__, p->vm->sp));
  while (p->vm->csp > saved_scp) delete_scope(p->vm);  // Restore current scope
  return res;
}

static err_t parse_call_dot_mem(struct parser *p, int prev_op) {
  err_t res = MJS_SUCCESS;
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
        res = do_call(p);
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

static err_t parse_postfix(struct parser *p, int prev_op) {
  err_t res = MJS_SUCCESS;
  TRY(parse_call_dot_mem(p, prev_op));
  if (p->tok.tok == DT('+', '+') || p->tok.tok == DT('-', '-')) {
    int op = p->tok.tok == DT('+', '+') ? TOK_POSTFIX_PLUS : TOK_POSTFIX_MINUS;
    TRY(do_op(p, op));
    pnext(p);
  }
  return res;
}

static err_t parse_unary(struct parser *p, int prev_op) {
  err_t res = MJS_SUCCESS;
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
  if (res != MJS_SUCCESS) return res;
  if (op != TOK_EOF) {
    if (op == '-') op = TOK_UNARY_MINUS;
    if (op == '+') op = TOK_UNARY_PLUS;
    do_op(p, op);
  }
  return res;
}

static err_t parse_mul_div_rem(struct parser *p, int prev_op) {
  int ops[] = {'*', '/', '%', TOK_EOF};
  return parse_ltr_binop(p, parse_unary, parse_mul_div_rem, ops, prev_op);
}

static err_t parse_plus_minus(struct parser *p, int prev_op) {
  int ops[] = {'+', '-', TOK_EOF};
  return parse_ltr_binop(p, parse_mul_div_rem, parse_plus_minus, ops, prev_op);
}

static err_t parse_ternary(struct parser *p, int prev_op) {
  err_t res = MJS_SUCCESS;
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

static err_t parse_assignment(struct parser *p, int pop) {
  return parse_rtl_binop(p, parse_ternary, parse_assignment, s_assign_ops, pop);
}

static err_t parse_expr(struct parser *p) {
  return parse_assignment(p, TOK_EOF);
}

static err_t parse_let(struct parser *p) {
  err_t res = MJS_SUCCESS;
  pnext(p);
  for (;;) {
    struct tok tmp = p->tok;
    val_t obj, key, val = mkval(MJS_UNDEFINED, 0);
    if (p->tok.tok != TOK_IDENT) return vm_err(p->vm, "indent expected");
    pnext(p);
    if (p->tok.tok == '=') {
      pnext(p);
      TRY(parse_expr(p));
      val = *vm_top(p->vm);
    }
    TRY(mk_str(p->vm, tmp.ptr, tmp.len, &key));
    obj = p->vm->call_stack[p->vm->csp - 1];
    TRY(mjs_set(p->vm, obj, key, val));
    if (p->tok.tok == ',') {
      TRY(vm_drop(p->vm));
      pnext(p);
    }
    if (p->tok.tok == ';' || p->tok.tok == TOK_EOF) break;
  }
  return res;
}

static err_t parse_return(struct parser *p) {
  err_t res = MJS_SUCCESS;
  pnext(p);
  // It is either "return;" or "return EXPR;"
  if (p->tok.tok == ';' || p->tok.tok == '}') {
    if (!p->noexec) vm_push(p->vm, mkval(MJS_UNDEFINED, 0));
  } else {
    res = parse_expr(p);
  }
  // Point parser to the end of func body, so that parse_block() gets '}'
  if (!p->noexec) p->pos = p->end - 1;
  return res;
}

static err_t parse_block_or_stmt(struct parser *p, int create_scope) {
  if (lookahead(p) == '{') {
    return parse_block(p, create_scope);
  } else {
    return parse_statement(p);
  }
}

static err_t parse_while(struct parser *p) {
  err_t res = MJS_SUCCESS;
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
    }
    TRY(parse_block_or_stmt(p, 1));
    LOG((DBGPREFIX "%s: done..\n", __func__));
    if (p->noexec) break;
    vm_drop(p->vm);
    vm_dump(p->vm);
  }
  LOG((DBGPREFIX "%s: out..\n", __func__));
  p->noexec = tmp.noexec;
  return res;
}

static err_t parse_statement(struct parser *p) {
  switch (p->tok.tok) {
    case ';':
      pnext(p);
      return MJS_SUCCESS;
    case TOK_LET:
      return parse_let(p);
    case '{':
      return parse_block(p, 1);
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
      err_t res = MJS_SUCCESS;
      for (;;) {
        TRY(parse_expr(p));
        if (p->tok.tok != ',') break;
        pnext(p);
      }
      return res;
    }
  }
}

static err_t parse_statement_list(struct parser *p, tok_t endtok) {
  err_t res = MJS_SUCCESS;
  pnext(p);
  while (res == MJS_SUCCESS && p->tok.tok != TOK_EOF && p->tok.tok != endtok) {
    if (p->vm->sp > 0) vm_drop(p->vm);  // Drop previous value from the stack
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
  vm->call_stack[0] = mkval(MJS_OBJECT, 0);
  vm->csp++;
  LOG((DBGPREFIX "%s: size %d bytes\n", __func__, (int) sizeof(*vm)));
  return vm;
};

void mjs_destroy(struct vm *vm) { free(vm); }

err_t mjs_exec(struct vm *vm, const char *buf, int len, val_t *v) {
  struct parser p = mk_parser(vm, buf, len);
  err_t e;
  vm->error_message[0] = '\0';
  e = parse_statement_list(&p, TOK_EOF);
  if (e == MJS_SUCCESS && v != NULL) *v = *vm_top(vm);
  LOG((DBGPREFIX "%s: %s\n", __func__, tostr(vm, *vm_top(vm))));
  return e;
}

#ifdef MJS_MAIN

int main(int argc, char *argv[]) {
  int i;
  struct vm *vm = mjs_create();
  err_t err = MJS_SUCCESS;
  for (i = 1; i < argc && argv[i][0] == '-' && err == MJS_SUCCESS; i++) {
    if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
      const char *code = argv[++i];
      err = mjs_exec(mjs, code, strlen(code), NULL);
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage: %s [-e js_expression]\n", argv[0]);
      return EXIT_SUCCESS;
    } else {
      fprintf(stderr, "Unknown flag: [%s]\n", argv[i]);
      return EXIT_FAILURE;
    }
  }
  vm_dump(vm);
  if (err != MJS_SUCCESS || vm->sp <= 0 || vm->sp > 1) {
    printf("Error: %s, sp=%d\n", vm->error_message, vm->sp);
  } else {
    printf("%s\n", tostr(mjs, *vm_top(vm)));
    putchar('\n');
  }
  mjs_destroy(mjs);
  return EXIT_SUCCESS;
}
#endif  // MJS_MAIN

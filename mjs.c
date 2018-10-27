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
typedef struct mjs vm;

struct prop {
  val_t key;
  val_t val;
  short flags;  // see MJS_PROP_* below
  short next;   // index of the next property, or -1 if this is the last one
};
#define PROP_ALLOCATED 1

struct obj {
  short flags;  // see MJS_OBJ_* defines below
  short props;  // index of the first property of this object, or -1
};
#define OBJ_ALLOCATED 1

struct vm {
  char error_message[50];
  val_t data_stack[50];
  val_t call_stack[20];
  int sp;                  // Points to the top of the data stack
  int csp;                 // Points to the top of the call stack
  char stringbuf[1024];    // String pool
  int sblen;               // String pool current length
  struct obj objs[50];     // Objects pool
  struct prop props[100];  // Props pool
};

// clang-format off
enum mjs_type {
  TYPE_UNDEFINED, TYPE_NULL, TYPE_TRUE, TYPE_FALSE, TYPE_STRING, TYPE_OBJECT,
  TYPE_ARRAY, TYPE_FUNCTION, TYPE_NUMBER
};
// clang-format on

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

#define ARRSIZE(x) ((int) (sizeof(x) / sizeof((x)[0])))

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
#endif

//////////////////////////////////// HELPERS /////////////////////////////////
static err_t mjs_err(struct vm *vm, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->error_message, sizeof(vm->error_message), fmt, ap);
  va_end(ap);
  LOG((DBGPREFIX "%s: %s\n", __func__, vm->error_message));
  return MJS_ERROR;
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

static const char *vstr(val_t v) {
  static char buf[30];
  const char *names[] = {"(undefined)", "(null)",   "(true)",  "(false)",
                         "(string)",    "(object)", "(array)", "(function)",
                         "(number)",    "?",        "?",       "?",
                         "?",           "?",        "?",       "?"};

  if (mjs_is_number(v)) {
    snprintf(buf, sizeof(buf), "(number) %f", mjs_tof(v));
  } else {
    snprintf(buf, sizeof(buf), "%s", names[VAL_TYPE(v)]);
  }
  return buf;
}

static val_t mkval(enum mjs_type t, unsigned int payload) {
  val_t v = MK_VAL(t, payload);
  LOG((DBGPREFIX "%s: %s\n", __func__, vstr(v)));
  return v;
}

////////////////////////////////////// VM ////////////////////////////////////
int mjs_is_string(val_t v) {
  return VAL_TYPE(v) == TYPE_STRING;
}

int mjs_is_number(val_t v) {
  return IS_FLOAT(v);
}

int mjs_is_object(val_t v) {
  return VAL_TYPE(v) == TYPE_OBJECT;
}

static void mjs_push(struct vm *vm, val_t v) {
  if (vm->sp >= 0 && vm->sp < ARRSIZE(vm->data_stack)) {
    vm->data_stack[vm->sp] = v;
    vm->sp++;
  } else {
    mjs_err(vm, "stack overflow");
  }
}

static void mjs_drop(struct vm *vm) {
  if (vm->sp > 0) {
    LOG((DBGPREFIX "%s: %s\n", __func__, vstr(vm->data_stack[vm->sp - 1])));
    vm->sp--;
  }
}

static val_t mk_str(struct vm *vm, const char *p, int len) {
  if (len > (int) sizeof(vm->stringbuf) + 3 - vm->sblen) {
    mjs_err(vm, "stringbuf too small");
    return mkval(TYPE_UNDEFINED, 0);
  } else {
    val_t v = mkval(TYPE_STRING, vm->sblen);
    vm->stringbuf[vm->sblen++] = (char) ((len >> 8) & 0xff);  // save
    vm->stringbuf[vm->sblen++] = (char) (len & 0xff);         // length
    if (p) memmove(&vm->stringbuf[vm->sblen], p, len);        // copy data
    vm->sblen += len;                   // data will be here
    vm->stringbuf[vm->sblen++] = '\0';  // nul-terminate
    return v;
  }
}

float mjs_get_number(val_t v) {
  return mjs_tof(v);
}

char *mjs_get_string(struct vm *vm, val_t v, int *len) {
  char *p = vm->stringbuf + VAL_PAYLOAD(v);
  if (len != NULL) *len = (unsigned char) p[0] << 8 | (unsigned char) p[1];
  return p + 2;
}

static val_t mjs_concat(struct vm *vm, val_t v1, val_t v2) {
  int n1, n2;
  char *p1 = mjs_get_string(vm, v1, &n1), *p2 = mjs_get_string(vm, v2, &n2);
  val_t v = mk_str(vm, NULL, n1 + n2);
  char *p = mjs_get_string(vm, v, NULL);
  memmove(p, p1, n1);
  memmove(p + n1, p2, n2);
  return v;
}

static val_t mjs_mk_obj(struct vm *vm) {
  int i;
  // Start iterating from 1, because object 0 is always a global object
  for (i = 1; i < ARRSIZE(vm->objs); i++) {
    if (vm->objs[i].flags & OBJ_ALLOCATED) continue;
    vm->objs[i].flags = OBJ_ALLOCATED;
    vm->objs[i].props = -1;
    return mkval(TYPE_OBJECT, i);
  }
  mjs_err(vm, "obj OOM");
  return mkval(TYPE_UNDEFINED, 0);
}

static err_t mjs_set(struct vm *vm, val_t obj, val_t key, val_t val) {
  if (mjs_is_object(obj)) {
    int i, obj_index = VAL_PAYLOAD(obj);
    struct obj *o = &vm->objs[obj_index];
    if (obj_index < 0 || obj_index >= ARRSIZE(vm->objs)) {
      return mjs_err(vm, "corrupt obj, index %x", obj_index);
    }
    for (i = 0; i < ARRSIZE(vm->props); i++) {
      struct prop *p = &vm->props[i];
      if (p->flags & PROP_ALLOCATED) continue;
      p->flags = PROP_ALLOCATED;
      p->next = o->props;  // Link to the current
      o->props = i;        // props list
      p->key = key;
      p->val = val;
      LOG((DBGPREFIX "%s: %s: ", __func__, vstr(obj)));
      LOG(("%s -> ", vstr(key)));
      LOG(("%s\n", vstr(val)));
      return MJS_SUCCESS;
    }
    return mjs_err(vm, "props OOM");
  } else {
    return mjs_err(vm, "setting prop on non-object");
  }
}

static err_t lookup(struct vm *vm, const char *ptr, int len, val_t *v) {
  int i, n, obj_index = VAL_PAYLOAD(vm->call_stack[0]);
  struct obj *o = &vm->objs[obj_index];
  if (obj_index < 0 || obj_index >= ARRSIZE(vm->objs)) {
    return mjs_err(vm, "corrupt obj, index %x", obj_index);
  }
  i = o->props;
  while (i >= 0) {
    struct prop *p = &vm->props[i];
    char *key = mjs_get_string(vm, p->key, &n);
    if (n == len && memcmp(key, ptr, n) == 0) {
      if (v) *v = p->val;
      return MJS_SUCCESS;
    }
    i = p->next;
  }
  return mjs_err(mjs, "[%.*s] undefined", len, ptr);
}

////////////////////////////////// TOKENIZER /////////////////////////////////

struct tok {
  int tok, len;
  const char *ptr;
  float num_value;
};

struct parser {
  const char *file_name;  // Source code file name
  const char *buf;        // Nul-terminated source code buffer
  const char *pos;        // Current position
  int line_no;            // Line number
  int prev_tok;           // Previous token, for prefix increment / decrement
  struct tok tok;         // Parsed token
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
  TOK_WHILE, TOK_WITH, TOK_LET, TOK_UNDEFINED
};
// clang-format on

static void pinit(const char *buf, struct parser *p) {
  memset(p, 0, sizeof(*p));
  p->line_no = 1;
  p->buf = p->pos = buf;
}

// We're not relying on the target libc ctype, as it may incorrectly
// handle negative arguments, e.g. isspace(-1).
static int mjs_is_space(int c) {
  return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f' ||
         c == '\v';
}

static int mjs_is_digit(int c) {
  return c >= '0' && c <= '9';
}

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
  if (p->pos[1] != '\0' && strchr(second_chars, p->pos[1]) != NULL) {
    p->tok.len++;
    p->pos++;
    return p->pos[-1] << 8 | p->pos[0];
  }
  return p->pos[0];
}

// Try to parse a token that takes exactly 3 chars.
static int longtok3(struct parser *p, char a, char b, char c) {
  if (p->pos[0] == a && p->pos[1] == b && p->pos[2] == c) {
    p->tok.len += 2;
    p->pos += 2;
    return p->pos[-2] << 16 | p->pos[-1] << 8 | p->pos[0];
  }
  return TOK_EOF;
}

// Try to parse a token that takes exactly 4 chars.
static int longtok4(struct parser *p, char a, char b, char c, char d) {
  if (p->pos[0] == a && p->pos[1] == b && p->pos[2] == c && p->pos[3] == d) {
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
  while (p->pos[0] != '\0' && p->pos[0] != quote) {
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
    while (mjs_is_space(p->pos[0])) {
      if (p->pos[0] == '\n') p->line_no++;
      p->pos++;
    }
    if (p->pos[0] == '/' && p->pos[1] == '/') {
      while (p->pos[0] != '\0' && p->pos[0] != '\n') p->pos++;
    }
    if (p->pos[0] == '/' && p->pos[1] == '*') {
      p->pos += 2;
      while (p->pos[0] != '\0') {
        if (p->pos[0] == '\n') p->line_no++;
        if (p->pos[0] == '*' && p->pos[1] == '/') {
          p->pos += 2;
          break;
        }
        p->pos++;
      }
    }
  } while (pos < p->pos);
}

static int pnext(struct parser *p) {
  int tmp, tok = TOK_INVALID;

  skip_spaces_and_comments(p);
  p->tok.ptr = p->pos;
  p->tok.len = 1;

  if (p->pos[0] == '\0') {
    tok = TOK_EOF;
  }
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
  if (p->pos[0] != '\0') p->pos++;
  p->prev_tok = p->tok.tok;
  p->tok.tok = tok;
  return p->tok.tok;
}

////////////////////////////////// PARSER /////////////////////////////////

#define PARSE_LTR_BINOP(p, f1, f2, ops, prev_op)                     \
  do {                                                               \
    err_t res = MJS_SUCCESS;                                         \
    if ((res = f1(p, TOK_EOF)) != MJS_SUCCESS) return res;           \
    if (prev_op != TOK_EOF && (res = do_op(p, prev_op))) return res; \
    if (findtok(ops, p->tok.tok) != TOK_EOF) {                       \
      int op = p->tok.tok;                                           \
      pnext(p);                                                      \
      if ((res = f2(p, op)) != MJS_SUCCESS) return res;              \
    }                                                                \
    return res;                                                      \
  } while (0)

#define PARSE_RTL_BINOP(p, f1, f2, ops, prev_op)             \
  do {                                                       \
    err_t res = MJS_SUCCESS;                                 \
    (void) prev_op;                                          \
    if ((res = f1(p, TOK_EOF)) != MJS_SUCCESS) return res;   \
    if (findtok(ops, p->tok.tok) != TOK_EOF) {               \
      int op = p->tok.tok;                                   \
      pnext(p);                                              \
      if ((res = f2(p, TOK_EOF)) != MJS_SUCCESS) return res; \
      if ((res = do_op(p, op)) != MJS_SUCCESS) return res;   \
    }                                                        \
    return res;                                              \
  } while (0)

static int findtok(int *toks, int tok) {
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
  val_t *top = &p->vm->data_stack[p->vm->sp];
  LOG((DBGPREFIX "%s: op %c\n", __func__, op));
  switch (op) {
    case '+':
      if (mjs_is_string(top[-2]) && mjs_is_string(top[-1])) {
        top[-2] = mjs_concat(p->vm, top[-2], top[-1]);
        p->vm->sp--;
        break;
      }
    // Fallthrough
    case '-':
    case '*':
    case '/':
    case '%':
      if (mjs_is_number(top[-2]) && mjs_is_number(top[-1])) {
        top[-2] = do_arith_op(mjs_tof(top[-2]), mjs_tof(top[-1]), op);
        p->vm->sp--;
      } else {
        return mjs_err(p->vm, "apples to apples please");
      }
      break;
    default:
      return mjs_err(p->vm, "Unknown op: %c", op);
      break;
  }
  return MJS_SUCCESS;
}

static err_t parse_literal(struct parser *p, int prev_op) {
  err_t res = MJS_SUCCESS;
  val_t v = 0;
  (void) prev_op;
  switch (p->tok.tok) {
    case TOK_NUM:
      v = mjs_tov(p->tok.num_value);
      break;
    case TOK_STR:
      v = mk_str(p->vm, p->tok.ptr, p->tok.len);
      break;
    case TOK_IDENT:
      res = lookup(p->mjs, p->tok.ptr, p->tok.len, &v);
      break;
    default:
      return mjs_err(p->vm, "Unexpected literal: [%.*s]", p->tok.len,
                     p->tok.ptr);
      break;
  }
  mjs_push(p->vm, v);
  pnext(p);
  return res;
}

static err_t parse_mul_div_rem(struct parser *p, int prev_op) {
  int ops[] = {'*', '/', '%', TOK_EOF};
  PARSE_LTR_BINOP(p, parse_literal, parse_mul_div_rem, ops, prev_op);
}

static err_t parse_plus_minus(struct parser *p, int prev_op) {
  int ops[] = {'+', '-', TOK_EOF};
  PARSE_LTR_BINOP(p, parse_mul_div_rem, parse_plus_minus, ops, prev_op);
}

static err_t parse_ternary(struct parser *p, int prev_op) {
  err_t res = MJS_SUCCESS;
  if ((res = parse_plus_minus(p, TOK_EOF)) != MJS_SUCCESS) return res;
  (void) prev_op;
  // if (prev_op != TOK_EOF) do_op(p, prev_op);
  if (p->tok.tok == '?') {
    pnext(p);
    if ((res = parse_ternary(p, TOK_EOF)) != MJS_SUCCESS) return res;
    if (p->tok.tok != ':') return mjs_err(p->vm, "expecting :");
    pnext(p);
    if ((res = parse_ternary(p, TOK_EOF)) != MJS_SUCCESS) return res;
  }
  return res;
}

// clang-format off
static int s_assign_ops[] = {
  '=', DT('+', '='), DT('-', '='),  DT('*', '='), DT('/', '='), DT('%', '='),
  TT('<', '<', '='), TT('>', '>', '='), QT('>', '>', '>', '='), DT('&', '='),
  DT('^', '='), DT('|', '='), TOK_EOF
};
// clang-format on
static err_t parse_assignment(struct parser *p, int prev_op) {
  PARSE_RTL_BINOP(p, parse_ternary, parse_assignment, s_assign_ops, prev_op);
}

static err_t parse_expr(struct parser *p) {
  return parse_assignment(p, TOK_EOF);
}

static err_t parse_let(struct parser *p) {
  err_t res = MJS_SUCCESS;
  pnext(p);
  LOG((DBGPREFIX "%s: %d %d\n", __func__, p->tok.tok, p->tok.tok == TOK_IDENT));
  for (;;) {
    struct tok tmp = p->tok;
    val_t obj, key, val = mkval(TYPE_UNDEFINED, 0);
    if (p->tok.tok != TOK_IDENT) return mjs_err(p->vm, "indent expected");
    pnext(p);
    if (p->tok.tok == '=') {
      pnext(p);
      if ((res = parse_expr(p)) != MJS_SUCCESS) return res;
      val = p->vm->data_stack[p->vm->sp - 1];
    }
    key = mk_str(p->vm, tmp.ptr, tmp.len);
    obj = p->vm->call_stack[p->vm->csp];
    if ((res = mjs_set(p->vm, obj, key, val)) != MJS_SUCCESS) return res;
    if (p->tok.tok == ',') {
      mjs_drop(p->vm);
      pnext(p);
    }
    if (p->tok.tok == ';' || p->tok.tok == TOK_EOF) break;
  }
  return res;
}

static err_t parse_statement(struct parser *p) {
  switch (p->tok.tok) {
    // clang-format off
    case ';': pnext(p); return MJS_SUCCESS;
    case TOK_LET: return parse_let(p);
#if 0
    case TOK_OPEN_CURLY: return parse_block(p, 1);
    case TOK_RETURN: return parse_return(p);
    case TOK_FOR: return parse_for(p);
    case TOK_WHILE: return parse_while(p);
    case TOK_BREAK: pnext1(p); return MJS_SUCCESS;
    case TOK_CONTINUE: pnext1(p); return MJS_SUCCESS;
    case TOK_IF: return parse_if(p);
#endif
    case TOK_CASE: case TOK_CATCH: case TOK_DELETE: case TOK_DO:
    case TOK_INSTANCEOF: case TOK_NEW: case TOK_SWITCH: case TOK_THROW:
    case TOK_TRY: case TOK_VAR: case TOK_VOID: case TOK_WITH:
      // clang-format on
      return mjs_err(p->vm, "[%.*s] is not implemented", p->tok.len,
                     p->tok.ptr);
    default: {
      err_t res = MJS_SUCCESS;
      for (;;) {
        if ((res = parse_expr(p)) != MJS_SUCCESS) return res;
        if (p->tok.tok != ',') break;
        pnext(p);
      }
      return res;
    }
  }
}

static err_t parse_statement_list(struct parser *p, int et) {
  err_t res = MJS_SUCCESS;
  pnext(p);
  while (res == MJS_SUCCESS && p->tok.tok != TOK_EOF && p->tok.tok != et) {
    mjs_drop(p->vm);  // Drop previous value from the stack
    res = parse_statement(p);
    while (p->tok.tok == ';') pnext(p);
  }
  return res;
}

/////////////////////////////// EXTERNAL API /////////////////////////////////

struct vm *mjs_create(void) {
  struct vm *vm = (struct mjs *) calloc(1, sizeof(*vm));
  vm->objs[0].flags = OBJ_ALLOCATED;
  vm->objs[0].props = -1;
  vm->call_stack[0] = mkval(TYPE_OBJECT, 0);
  printf("MJS CONTEXT: %lu bytes\n", sizeof(*vm));
  return vm;
};

void mjs_destroy(struct vm *vm) {
  free(vm);
}

err_t mjs_exec(struct vm *vm, const char *buf, val_t *v) {
  struct parser p;
  err_t e;
  vm->error_message[0] = '\0';
  pinit(buf, &p);
  p.vm = vm;
  e = parse_statement_list(&p, TOK_EOF);
  if (e == MJS_SUCCESS && v != NULL) *v = vm->data_stack[0];
  LOG((DBGPREFIX "%s: %s\n", __func__, vstr(vm->data_stack[0])));
  return e;
}

#ifdef MJS_MAIN

void mjs_printv(val_t v, struct vm *vm) {
  if (mjs_is_number(v)) {
    printf("%f", mjs_tof(v));
  } else if (mjs_is_string(v)) {
    printf("%s", mjs_get_string(mjs, v, NULL));
  } else {
    LOG((DBGPREFIX "%s: unknown value: %s\n", __func__, vstr(v)));
  }
}

int main(int argc, char *argv[]) {
  int i;
  struct vm *vm = mjs_create();
  err_t err = MJS_SUCCESS;
  for (i = 1; i < argc && argv[i][0] == '-' && err == MJS_SUCCESS; i++) {
    if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
      const char *code = argv[++i];
      err = mjs_exec(mjs, code, NULL);
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage: %s [-e js_expression]\n", argv[0]);
      return EXIT_SUCCESS;
    } else {
      fprintf(stderr, "Unknown flag: [%s]\n", argv[i]);
      return EXIT_FAILURE;
    }
  }
  if (err != MJS_SUCCESS || vm->sp <= 0 || vm->sp > 1) {
    printf("Error: [%s], sp=%d\n", vm->error_message, vm->sp);
  } else {
    mjs_printv(vm->data_stack[0], mjs);
    putchar('\n');
  }
  mjs_destroy(mjs);
  return EXIT_SUCCESS;
}
#endif  // MJS_MAIN

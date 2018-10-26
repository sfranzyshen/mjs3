// Copyright (c) 2013-2018 Cesanta Software Limited
// All rights reserved

#include "mjs.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_MSC_VER) || _MSC_VER >= 1700
#else
#define vsnprintf _vsnprintf
#endif

struct mjs {
  char error_message[50];
  mjs_val_t data_stack[50];
  mjs_val_t call_stack[20];
  int sp;                // points to the top of the data stack
  int csp;               // points to the top of the call stack
  char stringbuf[1024];  // contains strings
  int sblen;             // stringbuf length
};

// clang-format off
enum mjs_type {
  MJS_TYPE_UNDEFINED, MJS_TYPE_NULL, MJS_TYPE_TRUE, MJS_TYPE_FALSE,
  MJS_TYPE_NUMBER, MJS_TYPE_STRING, MJS_TYPE_OBJECT_GENERIC,
  MJS_TYPE_OBJECT_ARRAY, MJS_TYPE_OBJECT_FUNCTION,
};
// clang-format on

// 32bit floating-point number: 1 bit sign, 8 bits exponent, 23 bits mantissa
//      3        2        1       0
//  seeeeeee|emmmmmmm|mmmmmmmm|mmmmmmmm
//  11111111|1ttttvvv|vvvvvvvv|vvvvvvvv
//     NaN    |type| 19-bit placeholder for payload

#define MK_MJS_VAL(t, p) (((unsigned int) ~0 << 23) | ((t) << 19) | (p))
#define MJS_VTYPE(v) (((v) >> 19) & 0xf)
#define MJS_VPLAYLOAD(v) ((v) & ((unsigned int) ~0 >> (32 - 19)))

//////////////////////////////////// HELPERS /////////////////////////////////
static mjs_err_t mjs_set_err(struct mjs *mjs, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(mjs->error_message, sizeof(mjs->error_message), fmt, ap);
  va_end(ap);
  return MJS_ERROR;
}

static void mjs_tracev(const char *label, mjs_val_t v) {
  printf("%10s: v %x, t %d, p %d\n", label, v, MJS_VTYPE(v), MJS_VPLAYLOAD(v));
}

static mjs_val_t mjs_mkval(enum mjs_type t, unsigned int payload) {
  mjs_val_t v = MK_MJS_VAL(t, payload);
  mjs_tracev("MKVAL", v);
  return v;
}

static mjs_val_t mjs_tov(float f) {
  union {
    mjs_val_t v;
    float f;
  } u;
  u.f = f;
  return u.v;
}

static float mjs_tof(mjs_val_t v) {
  union {
    mjs_val_t v;
    float f;
  } u;
  u.v = v;
  return u.f;
}

////////////////////////////////////// VM ////////////////////////////////////
int mjs_is_string(mjs_val_t v) {
  return MJS_VTYPE(v) == MJS_TYPE_STRING;
}

int mjs_is_number(mjs_val_t v) {
  return !isnan(mjs_tof(v));
}

static void mjs_push(struct mjs *mjs, mjs_val_t v) {
  if (mjs->sp < (int) (sizeof(mjs->data_stack) / sizeof(mjs->data_stack[0]))) {
    mjs->data_stack[mjs->sp] = v;
    mjs->sp++;
  } else {
    mjs_set_err(mjs, "stack overflow");
  }
}

static mjs_val_t mjs_mkstr(struct mjs *mjs, int len) {
  if (len > (int) sizeof(mjs->stringbuf) + 3 - mjs->sblen) {
    mjs_set_err(mjs, "stringbuf too small");
    return mjs_mkval(MJS_TYPE_UNDEFINED, 0);
  } else {
    mjs_val_t v = mjs_mkval(MJS_TYPE_STRING, mjs->sblen);
    mjs->stringbuf[mjs->sblen++] = (len >> 8) & 0xf;  // save
    mjs->stringbuf[mjs->sblen++] = len & 0xf;         // length
    mjs->sblen += len;                                // data will be here
    mjs->stringbuf[mjs->sblen++] = '\0';              // nul-terminate
    return v;
  }
}

float mjs_get_number(mjs_val_t v) {
  return mjs_tof(v);
}

char *mjs_get_string(struct mjs *mjs, mjs_val_t v, int *len) {
  char *p = mjs->stringbuf + MJS_VPLAYLOAD(v);
  // mjs_tracev("GETSTR", v);
  if (len != NULL) *len = (unsigned char) p[0] << 8 | (unsigned char) p[1];
  return p + 2;
}

mjs_val_t mjs_concat_strings(struct mjs *mjs, mjs_val_t v1, mjs_val_t v2) {
  int n1, n2;
  char *p1 = mjs_get_string(mjs, v1, &n1), *p2 = mjs_get_string(mjs, v2, &n2);
  mjs_val_t v = mjs_mkstr(mjs, n1 + n2);
  char *p = mjs_get_string(mjs, v, NULL);
  memmove(p, p1, n1);
  memmove(p + n1, p2, n2);
  return v;
}

////////////////////////////////// TOKENIZER /////////////////////////////////

struct tok {
  int tok, len;
  const char *ptr;
  float num_value;
};

struct pstate {
  const char *file_name;  // Source code file name
  const char *buf;        // Nul-terminated source code buffer
  const char *pos;        // Current position
  int line_no;            // Line number
  int prev_tok;           // Previous token, for prefix increment / decrement
  struct tok tok;         // Parsed token
  struct mjs *mjs;
};

#define DOUBLE_TOK(a, b) ((a) << 8 | (b))
#define TRIPLE_TOK(a, b, c) ((a) << 16 | (b) << 8 | (c))
#define QUAD_TOK(a, b, c, d) ((a) << 24 | (b) << 16 | (c) << 8 | (d))

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

static void pinit(const char *buf, struct pstate *p) {
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
static int longtok(struct pstate *p, const char *first_chars,
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
static int longtok3(struct pstate *p, char a, char b, char c) {
  if (p->pos[0] == a && p->pos[1] == b && p->pos[2] == c) {
    p->tok.len += 2;
    p->pos += 2;
    return p->pos[-2] << 16 | p->pos[-1] << 8 | p->pos[0];
  }
  return TOK_EOF;
}

// Try to parse a token that takes exactly 4 chars.
static int longtok4(struct pstate *p, char a, char b, char c, char d) {
  if (p->pos[0] == a && p->pos[1] == b && p->pos[2] == c && p->pos[3] == d) {
    p->tok.len += 3;
    p->pos += 3;
    return p->pos[-3] << 24 | p->pos[-2] << 16 | p->pos[-1] << 8 | p->pos[0];
  }
  return TOK_EOF;
}

static int getnum(struct pstate *p) {
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

static int getident(struct pstate *p) {
  while (mjs_is_ident(p->pos[0]) || mjs_is_digit(p->pos[0])) p->pos++;
  p->tok.len = p->pos - p->tok.ptr;
  p->pos--;
  return TOK_IDENT;
}

static int getstr(struct pstate *p) {
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

static void skip_spaces_and_comments(struct pstate *p) {
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

static int pnext(struct pstate *p) {
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

#define PARSE_LTR_BINOP(p, f1, f2, ops)                    \
  do {                                                     \
    mjs_err_t res = MJS_SUCCESS;                           \
    if ((res = f1(p)) != MJS_SUCCESS) return res;          \
    if (findtok(ops, p->tok.tok) != TOK_EOF) {             \
      int op = p->tok.tok;                                 \
      pnext(p);                                            \
      if ((res = f2(p)) != MJS_SUCCESS) return res;        \
      if ((res = do_op(p, op)) != MJS_SUCCESS) return res; \
    }                                                      \
    return res;                                            \
  } while (0)

static int findtok(int *toks, int tok) {
  int i = 0;
  while (tok != toks[i] && toks[i] != TOK_EOF) i++;
  return toks[i];
}

static mjs_val_t do_arith_op(float f1, float f2, int op) {
  float res;
  // clang-format off
  switch (op) {
    case '+': res = f1 + f2; break;
    case '-': res = f1 - f2; break;
    case '*': res = f1 * f2; break;
    case '/': res = f1 / f2; break;
    case '%': res = (mjs_val_t) f1 % (mjs_val_t) f2; break;
  }
  // clang-format on
  return mjs_tov(res);
}

static mjs_err_t do_op(struct pstate *p, int op) {
  mjs_val_t *top = &p->mjs->data_stack[p->mjs->sp];
  printf("DOING OP %c, p %p\n", op, p);
  switch (op) {
    case '+':
      if (mjs_is_string(top[-2]) && mjs_is_string(top[-1])) {
        top[-2] = mjs_concat_strings(p->mjs, top[-2], top[-1]);
        p->mjs->sp--;
        break;
      }
    // Fallthrough
    case '-':
    case '*':
    case '/':
    case '%':
      if (mjs_is_number(top[-2]) && mjs_is_number(top[-1])) {
        top[-2] = do_arith_op(mjs_tof(top[-2]), mjs_tof(top[-1]), op);
        p->mjs->sp--;
      } else {
        return mjs_set_err(p->mjs, "apples to apples please");
      }
      break;
    default:
      return mjs_set_err(p->mjs, "Unknown op: %c", op);
      break;
  }
  return MJS_SUCCESS;
}

static mjs_err_t parse_literal(struct pstate *p) {
  mjs_val_t v = 0;
  if (p->tok.tok == TOK_NUM) {
    printf("PARSING NUM [%.*s]\n", p->tok.len, p->tok.ptr);
    v = mjs_tov(p->tok.num_value);
  } else if (p->tok.tok == TOK_STR) {
    printf("PARSING STR [%.*s]\n", p->tok.len, p->tok.ptr);
    v = mjs_mkstr(p->mjs, p->tok.len);
    memmove(&p->mjs->stringbuf[p->mjs->sblen - p->tok.len - 1], p->tok.ptr,
            p->tok.len);
  } else {
    return mjs_set_err(p->mjs, "Unexpected literal: [%.*s]", p->tok.len,
                       p->tok.ptr);
  }
  mjs_push(p->mjs, v);
  pnext(p);
  return MJS_SUCCESS;
}

static mjs_err_t parse_mul_div_rem(struct pstate *p) {
  int ops[] = {'*', '/', '%', TOK_EOF};
  PARSE_LTR_BINOP(p, parse_literal, parse_mul_div_rem, ops);
}

static mjs_err_t parse_plus_minus(struct pstate *p) {
  int ops[] = {'+', '-', TOK_EOF};
  PARSE_LTR_BINOP(p, parse_mul_div_rem, parse_plus_minus, ops);
}

// static mjs_err_t parse_assignment(struct pstate *p, int prev_op) {
//  PARSE_RTL_BINOP(p, parse_ternary, parse_assignment, s_assign_ops, prev_op);
//}

static mjs_err_t parse_expr(struct pstate *p) {
  // return parse_assignment(p, TOK_EOF);
  return parse_plus_minus(p);
}

static mjs_err_t parse_statement(struct pstate *p) {
  switch (p->tok.tok) {
    // clang-format off
    case ';': pnext(p); return MJS_SUCCESS;
#if 0
    case TOK_LET: return parse_let(p);
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
      return mjs_set_err(p->mjs, "[%.*s] is not implemented", p->tok.len,
                         p->tok.ptr);
    default: {
      mjs_err_t res = MJS_SUCCESS;
      for (;;) {
        if ((res = parse_expr(p)) != MJS_SUCCESS) return res;
        if (p->tok.tok != ',') break;
        pnext(p);
      }
      return res;
    }
  }
}

static mjs_err_t parse_statement_list(struct pstate *p, int et) {
  mjs_err_t res = MJS_SUCCESS;
  pnext(p);
  while (res == MJS_SUCCESS && p->tok.tok != TOK_EOF && p->tok.tok != et) {
    res = parse_statement(p);
    while (p->tok.tok == ';') pnext(p);
  }
  return res;
}

/////////////////////////////// EXTERNAL API /////////////////////////////////

struct mjs *mjs_create(void) {
  struct mjs *mjs = (struct mjs *) calloc(1, sizeof(*mjs));
  return mjs;
};

void mjs_destroy(struct mjs *mjs) {
  free(mjs);
}

mjs_err_t mjs_exec(struct mjs *mjs, const char *buf, mjs_val_t *v) {
  struct pstate p;
  mjs_err_t e;
  mjs->error_message[0] = '\0';
  pinit(buf, &p);
  p.mjs = mjs;
  e = parse_statement_list(&p, TOK_EOF);
  if (e == MJS_SUCCESS && v != NULL) *v = mjs->data_stack[0];
  mjs_tracev("EXEC", mjs->data_stack[0]);
  return e;
}

#ifdef MJS_MAIN

void mjs_printv(mjs_val_t v, struct mjs *mjs) {
  if (mjs_is_number(v)) {
    printf("%f", mjs_tof(v));
  } else if (mjs_is_string(v)) {
    printf("%s", mjs_get_string(mjs, v, NULL));
  } else {
    mjs_tracev("Unknown value", v);
  }
}

int main(int argc, char *argv[]) {
  int i;
  struct mjs *mjs = mjs_create();
  mjs_err_t err = MJS_SUCCESS;
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
  if (err != MJS_SUCCESS || mjs->sp <= 0 || mjs->sp > 1) {
    printf("Error: [%s], sp=%d\n", mjs->error_message, mjs->sp);
  } else {
    mjs_printv(mjs->data_stack[0], mjs);
    putchar('\n');
  }
  mjs_destroy(mjs);
  return EXIT_SUCCESS;
}
#endif  // MJS_MAIN

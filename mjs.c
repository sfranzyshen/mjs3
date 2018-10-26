// Copyright (c) 2013-2018 Cesanta Software Limited
// All rights reserved

#include "mjs.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_MSC_VER) || _MSC_VER >= 1700
#include <stdint.h>
#else
typedef unsigned int uint32_t;
#endif

//////////////////////////////////// HELPERS /////////////////////////////////
static mjs_err_t mjs_set_err(struct mjs *mjs, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(mjs->err_msg, sizeof(mjs->err_msg), fmt, ap);
  va_end(ap);
  return MJS_ERR;
}

////////////////////////////////////// VM ////////////////////////////////////
static void mjs_push(struct mjs *mjs, mjs_val_t v) {
  if (mjs->sp < (int) (sizeof(mjs->stack) / sizeof(mjs->stack[0]))) {
    mjs->stack[mjs->sp] = v;
    mjs->sp++;
  } else {
    mjs_set_err(mjs, "stack overflow");
  }
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
    p->tok.num_value = strtoul(p->pos + 2, (char **) &p->pos, 16);
  } else {
    p->tok.num_value = strtof(p->pos, (char **) &p->pos);
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

#define PARSE_LTR_BINOP(p, f1, f2, ops)        \
  do {                                         \
    mjs_err_t res = MJS_OK;                    \
    if ((res = f1(p)) != MJS_OK) return res;   \
    if (findtok(ops, p->tok.tok) != TOK_EOF) { \
      int op = p->tok.tok;                     \
      pnext(p);                                \
      if ((res = f2(p)) != MJS_OK) return res; \
      do_op(p, op);                            \
    }                                          \
    return res;                                \
  } while (0)

static int findtok(int *toks, int tok) {
  int i = 0;
  while (tok != toks[i] && toks[i] != TOK_EOF) i++;
  return toks[i];
}

static void do_op(struct pstate *p, int op) {
  // assert(op >= 0 && op <= 255);
  printf("DOING OP %c, p %p\n", op, p);
  mjs_val_t *top = &p->mjs->stack[p->mjs->sp];
  switch (op) {
    case '+':
      top[-2] = top[-2] + top[-1];
      p->mjs->sp--;
      break;
    case '-':
      top[-2] = top[-2] - top[-1];
      p->mjs->sp--;
      break;
    case '*':
      top[-2] = top[-2] * top[-1];
      p->mjs->sp--;
      break;
    case '/':
      top[-2] = top[-2] / top[-1];
      p->mjs->sp--;
      break;
    case '%':
      top[-2] = (unsigned long) top[-2] % (unsigned long) top[-1];
      p->mjs->sp--;
      break;
    default:
      mjs_set_err(p->mjs, "Unknown op: %c", op);
      break;
  }
}

static mjs_err_t parse_literal(struct pstate *p) {
  if (p->tok.tok != TOK_NUM) return mjs_set_err(p->mjs, "Expectedd number");
  printf("PARSING NUM [%.*s]\n", p->tok.len, p->tok.ptr);
  mjs_push(p->mjs, p->tok.num_value);
  pnext(p);
  return MJS_OK;
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
    case ';': pnext(p); return MJS_OK;
#if 0
    case TOK_LET: return parse_let(p);
    case TOK_OPEN_CURLY: return parse_block(p, 1);
    case TOK_RETURN: return parse_return(p);
    case TOK_FOR: return parse_for(p);
    case TOK_WHILE: return parse_while(p);
    case TOK_BREAK: pnext1(p); return MJS_OK;
    case TOK_CONTINUE: pnext1(p); return MJS_OK;
    case TOK_IF: return parse_if(p);
#endif
    case TOK_CASE: case TOK_CATCH: case TOK_DELETE: case TOK_DO:
    case TOK_INSTANCEOF: case TOK_NEW: case TOK_SWITCH: case TOK_THROW:
    case TOK_TRY: case TOK_VAR: case TOK_VOID: case TOK_WITH:
      // clang-format on
      return mjs_set_err(p->mjs, "[%.*s] is not implemented", p->tok.len,
                         p->tok.ptr);
    default: {
      mjs_err_t res = MJS_OK;
      for (;;) {
        if ((res = parse_expr(p)) != MJS_OK) return res;
        if (p->tok.tok != ',') break;
        pnext(p);
      }
      return res;
    }
  }
}

static mjs_err_t parse_statement_list(struct pstate *p, int et) {
  mjs_err_t res = MJS_OK;
  pnext(p);
  while (res == MJS_OK && p->tok.tok != TOK_EOF && p->tok.tok != et) {
    res = parse_statement(p);
    while (p->tok.tok == ';') pnext(p);
  }
  return res;
}

////////////////////////////////// MAIN /////////////////////////////////

mjs_err_t mjs_exec(struct mjs *mjs, const char *buf) {
  struct pstate p;
  mjs->err_msg[0] = '\0';
  pinit(buf, &p);
  p.mjs = mjs;
  return parse_statement_list(&p, TOK_EOF);
}

#ifdef MJS_MAIN
int main(int argc, char *argv[]) {
  int i;
  struct mjs mjs;
  mjs_err_t err = MJS_OK;
  memset(&mjs, 0, sizeof(mjs));
  for (i = 1; i < argc && argv[i][0] == '-' && err == MJS_OK; i++) {
    if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
      const char *code = argv[++i];
      err = mjs_exec(&mjs, code);
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage: %s [-e js_expression]\n", argv[0]);
      return EXIT_SUCCESS;
    } else {
      fprintf(stderr, "Unknown flag: [%s]\n", argv[i]);
      return EXIT_FAILURE;
    }
  }
  if (err != MJS_OK || mjs.sp <= 0 || mjs.sp > 1) {
    printf("Error: [%s], sp=%d\n", mjs.err_msg, mjs.sp);
  } else {
    printf("%f\n", (float) mjs.stack[0]);
  }
  return EXIT_SUCCESS;
}
#endif  // MJS_MAIN*

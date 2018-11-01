#include "mjs.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int check_num(mjs_val_t v, float expected) {
  return mjs_type(v) == MJS_NUMBER &&
         fabs(mjs_get_number(v) - expected) < 0.0001;
}

static int check_str(struct mjs *mjs, mjs_val_t v, const char *expected) {
  mjs_len_t len;
  const char *p = mjs_get_string(mjs, v, &len);
  printf("%d %u [%.*s] [%s] %lu\n", mjs_type(v), len, len, p, expected,
         (unsigned long) strlen(expected));
  return mjs_type(v) == MJS_STRING && len == strlen(expected) &&
         memcmp(p, expected, len) == 0;
}

static int numexpr(struct mjs *mjs, const char *code, float expected) {
  mjs_val_t v;
  if (mjs_exec(mjs, code, strlen(code), &v) != MJS_SUCCESS) return 0;
  return check_num(v, expected);
}

static int strexpr(struct mjs *mjs, const char *code, const char *expected) {
  mjs_val_t v;
  if (mjs_exec(mjs, code, strlen(code), &v) != MJS_SUCCESS) return 0;
  return check_str(mjs, v, expected);
}

static int typeexpr(struct mjs *mjs, const char *code, mjs_type_t t) {
  mjs_val_t v;
  return mjs_exec(mjs, code, strlen(code), &v) == MJS_SUCCESS &&
         mjs_type(v) == t;
}

static void test_expr(void) {
  struct mjs *mjs = mjs_create();

  assert(typeexpr(mjs, "let a", MJS_UNDEFINED));
  assert(mjs_exec(mjs, "let a", 5, NULL) == MJS_FAILURE);
  assert(typeexpr(mjs, "let ax, bx = function(x){}", MJS_FUNCTION));
  assert(typeexpr(mjs, "let ay, by = function(x){}, c", MJS_UNDEFINED));

  assert(numexpr(mjs, "let aq = 1;", 1.0f));
  assert(numexpr(mjs, "let aw = 1, be = 2;", 2.0f));
  assert(numexpr(mjs, "123", 123.0f));
  assert(numexpr(mjs, "123;", 123.0f));
  assert(numexpr(mjs, "{123}", 123.0f));
  assert(numexpr(mjs, "1 + 2 * 3.7 - 7 % 3", 7.4f));
  assert(numexpr(mjs, "let ag = 1.23, bg = 5.3;", 5.3f));
  assert(numexpr(mjs, "ag;", 1.23f));
  assert(numexpr(mjs, "ag - 2 * 3.1;", -4.97f));
  assert(numexpr(mjs,
                 "let az = 1.23; az + 1; let fz = function(a) "
                 "{ return az + 1; }; 1;",
                 1));
  assert(numexpr(mjs, "2 * (1 + 2)", 6.0f));
  assert(numexpr(mjs, "let at = 9; while (at) at--;", 0.0f));
  assert(numexpr(mjs, "let a2 = 9, b2 = 0; while (a2) { a2--; } ", 0.0f));
  assert(numexpr(mjs, "let a3 = 9, b3 = 0; while (a3) a3--; b3++; ", 1.0f));
  assert(numexpr(mjs, "let a4 = 9, b4 = 7; while (a4){a4--;b4++;} b4", 16.0f));

  mjs_destroy(mjs);
}

static void test_strings(void) {
  struct mjs *mjs = mjs_create();
  assert(strexpr(mjs, "'a' + 'b'", "ab"));
  assert(strexpr(mjs, "'vb'", "vb"));
  assert(strexpr(mjs, "let a, b = function(x){}, c = 'aa'", "aa"));
  assert(strexpr(mjs, "let a2, b2 = function(){}, cv = 'aa'", "aa"));
  mjs_destroy(mjs);
}

int main(void) {
  test_strings();
  test_expr();
  printf("TEST PASSED\n");
  return 0;
}

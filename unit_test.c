#include "mjs.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int check_num(mjs_val_t v, float expected) {
  return mjs_type(v) == MJS_TYPE_NUMBER &&
         fabs(mjs_to_float(v) - expected) < 0.0001;
}

static int check_str(struct mjs *mjs, mjs_val_t v, const char *expected) {
  mjs_len_t len;
  const char *p = mjs_to_str(mjs, v, &len);
  return mjs_type(v) == MJS_TYPE_STRING && len == strlen(expected) &&
         memcmp(p, expected, len) == 0;
}

static int numexpr(struct mjs *mjs, const char *code, float expected) {
  mjs_val_t v = mjs_eval(mjs, code, strlen(code));
  return mjs_type(v) != MJS_TYPE_NUMBER ? 0 : check_num(v, expected);
}

static int strexpr(struct mjs *mjs, const char *code, const char *expected) {
  mjs_val_t v = mjs_eval(mjs, code, strlen(code));
  return mjs_type(v) != MJS_TYPE_STRING ? 0 : check_str(mjs, v, expected);
}

static int typeexpr(struct mjs *mjs, const char *code, mjs_type_t t) {
  mjs_val_t v = mjs_eval(mjs, code, strlen(code));
  return mjs_type(v) == t;
}

static void test_expr(void) {
  struct mjs *mjs = mjs_create();

  assert(mjs_eval(mjs, "let a", -1) == MJS_UNDEFINED);
  assert(mjs_eval(mjs, "let a", -1) == MJS_ERROR);
  assert(typeexpr(mjs, "let ax, bx = function(x){}", MJS_TYPE_FUNCTION));
  assert(typeexpr(mjs, "let ay, by = function(x){}, c", MJS_TYPE_UNDEFINED));

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
  assert(strexpr(mjs, "'a'", "a"));
  assert(mjs->stringbuf_len == 3);
  assert(strexpr(mjs, "'b'", "b"));
  assert(mjs->stringbuf_len == 3);
  assert(numexpr(mjs, "1", 1.0f));
  assert(mjs->stringbuf_len == 0);
  assert(numexpr(mjs, "{let a = 1;}", 1.0f));
  assert(mjs->stringbuf_len == 0);
  assert(numexpr(mjs, "{let a = 'abc';} 1;", 1.0f));
  assert(mjs->stringbuf_len == 0);
  assert(strexpr(mjs, "'a' + 'b'", "ab"));
  assert(strexpr(mjs, "'vb'", "vb"));
  assert(strexpr(mjs, "let a, b = function(x){}, c = 'aa'", "aa"));
  assert(strexpr(mjs, "let a2, b2 = function(){}, cv = 'aa'", "aa"));
  // printf("--> %d\n", (int) mjs->stringbuf_len);
  mjs_destroy(mjs);
}

static float pi(void) { return 3.1415926f; }          // Return value of PI
static float sub(float a, float b) { return a - b; }  // Subtract two numbers
static char *fmt(const char *fmt, float f) {          // Format float value
  static char buf[20];
  snprintf(buf, sizeof(buf), fmt, f);
  return buf;
}

static void test_ffi(void) {
  struct mjs *mjs = mjs_create();
  mjs_inject_0(mjs, "pi", (mjs_cfunc_t) pi, CT_FLOAT);
  mjs_inject_2(mjs, "sub", (mjs_cfunc_t) sub, CT_FLOAT, CT_FLOAT, CT_FLOAT);
  mjs_inject_2(mjs, "fmt", (mjs_cfunc_t) fmt, CT_CHAR_PTR, CT_CHAR_PTR,
               CT_FLOAT);
  assert(numexpr(mjs, "sub(1,3);", -2));
  assert(numexpr(mjs, "sub(0, 0xff);", -255));
  assert(numexpr(mjs, "sub(0xffffff, 0);", 0xffffff));
  assert(numexpr(mjs, "sub(pi(), 0);", 3.1415926f));
  assert(strexpr(mjs, "fmt('%.2f', pi());", "3.14"));
  mjs_destroy(mjs);
}

static void test_scopes(void) {
  struct mjs *mjs = mjs_create();
  assert(numexpr(mjs, "1.23", 1.23f));
  assert(mjs->csp == 1);
  assert(mjs->objs[0].flags & OBJ_ALLOCATED);
  assert(!(mjs->objs[1].flags & OBJ_ALLOCATED));
  assert(!(mjs->props[0].flags & PROP_ALLOCATED));
  assert(numexpr(mjs, "{let a = 1.23;}", 1.23f));
  assert(!(mjs->objs[1].flags & OBJ_ALLOCATED));
  assert(!(mjs->props[0].flags & PROP_ALLOCATED));
  mjs_destroy(mjs);
}

int main(void) {
  test_strings();
  test_expr();
  test_ffi();
  test_scopes();
  printf("TEST PASSED\n");
  return 0;
}

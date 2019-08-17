#include "mjs.c"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK_NUMERIC(x, y) assert(numexpr(mjs, (x), (y)))

static int check_num(mjs_val_t v, float expected) {
  return mjs_type(v) == MJS_TYPE_NUMBER &&
         fabs(mjs_to_float(v) - expected) < 0.0001;
  // printf("%s: %g %g %d\n", __func__, mjs_to_float(v), expected, res);
  // return res;
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
  assert(numexpr(mjs, "let a3 = 9, b3 = 0; while (a3) a3--; b3++; ", 0.0f));
  assert(numexpr(mjs, "b3", 1.0f));
  assert(numexpr(mjs, "let a4 = 9, b4 = 7; while (a4){a4--;b4++;} b4", 16.0f));

  assert(numexpr(mjs, "let q = 1; q++;", 1.0f));
  assert(numexpr(mjs, "q;", 2.0f));
  assert(numexpr(mjs, "q--;", 2.0f));
  assert(numexpr(mjs, "q;", 1.0f));

  assert(numexpr(mjs, "0x64", 100));
  assert(numexpr(mjs, "0x7fffffff", 0x7fffffff));
  assert(numexpr(mjs, "0xffffffff", 0xffffffff));
  assert(numexpr(mjs, "123.4", 123.4));
  assert(numexpr(mjs, "200+50", 250));
  assert(numexpr(mjs, "1-2*3", -5));
  assert(numexpr(mjs, "1-2+3", 2));
  assert(numexpr(mjs, "200-50", 150));
  assert(numexpr(mjs, "200*50", 10000));
  assert(numexpr(mjs, "200/50", 4));
  assert(numexpr(mjs, "200 % 21", 11));
  // ASSERT_EXEC_OK(mjs_exec(mjs, "200 % 0.999", &res));
  // ASSERT(isnan(mjs_get_double(mjs, res)));
  assert(numexpr(mjs, "5 % 2", 1));
  assert(numexpr(mjs, "5 % -2", 1));
  // assert(numexpr(mjs, "-5 % 2", -1));
  // assert(numexpr(mjs, "-5 % -2", -1));
  assert(numexpr(mjs, "100 << 3", 800));
  assert(numexpr(mjs, "(0-14) >> 2", -4));
  assert(numexpr(mjs, "(0-14) >>> 2", 1073741820));
  assert(numexpr(mjs, "6 & 3", 2));
  assert(numexpr(mjs, "6 | 3", 7));
  assert(numexpr(mjs, "6 ^ 3", 5));

  assert(numexpr(mjs, "0.1 + 0.2", 0.3));
  assert(numexpr(mjs, "123.4 + 0.1", 123.5));

  // printf("--> %s\n", mjs_stringify(mjs, mjs_eval(mjs, "~10", -1)));
  assert(numexpr(mjs, "{let a = 200; a += 50; a}", 250));
  assert(numexpr(mjs, "{let a = 200; a -= 50; a}", 150));
  assert(numexpr(mjs, "{let a = 200; a *= 50; a}", 10000));
  assert(numexpr(mjs, "{let a = 200; a /= 50; a}", 4));
  assert(numexpr(mjs, "{let a = 200; a %= 21; a}", 11));
  assert(numexpr(mjs, "{let a = 100; a <<= 3; a}", 800));
  assert(numexpr(mjs, "{let a = 0-14; a >>= 2; a}", -4));
  assert(numexpr(mjs, "{let a = 0-14; a >>>= 2; a}", 1073741820));
  assert(numexpr(mjs, "{let a = 6; a &= 3; a}", 2));
  assert(numexpr(mjs, "{let a = 6; a |= 3; a}", 7));
  assert(numexpr(mjs, "{let a = 6; a ^= 3; a}", 5));

  assert(mjs_eval(mjs, "!0", -1) == MJS_TRUE);
  assert(mjs_eval(mjs, "!1", -1) == MJS_FALSE);
  assert(mjs_eval(mjs, "!''", -1) == MJS_TRUE);
  assert(mjs_eval(mjs, "!false", -1) == MJS_TRUE);
  assert(numexpr(mjs, "~10", -11));
  assert(numexpr(mjs, "-100", -100));
  assert(numexpr(mjs, "+100", 100));

  assert(numexpr(mjs, "2 * (3 + 4)", 14));

  // ASSERT_EXEC_OK(mjs_exec(mjs, "NaN", &res));
  // ASSERT_EQ(!!isnan(mjs_get_double(mjs, res)), 1);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "NaN === NaN", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 0);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "NaN !== NaN", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 1);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "NaN === 0", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 0);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "NaN === 1", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 0);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "NaN === null", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 0);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "NaN === undefined", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 0);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "NaN === ''", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 0);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "NaN === {}", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 0);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "NaN === []", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 0);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "isNaN(NaN)", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 1);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "isNaN(NaN * 10)", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 1);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "isNaN(0)", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 0);

  // ASSERT_EXEC_OK(mjs_exec(mjs, "isNaN('')", &res));
  // ASSERT_EQ(mjs_get_bool(mjs, res), 0);

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

static float pi(void) {
  return 3.1415926f;
}  // Return value of PI
static float sub(float a, float b) {
  return a - b;
}  // Subtract two numbers
static char *fmt(const char *fmt, float f) {  // Format float value
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
  CHECK_NUMERIC("if (1) 2", 2);
  assert(mjs_eval(mjs, "if (0) 2;", -1) == MJS_UNDEFINED);
  CHECK_NUMERIC("{let a = 42; }", 42);
  CHECK_NUMERIC("let a = 1, b = 2; { let a = 3; b += a; } b;", 5);
  assert(mjs_eval(mjs, "{}", -1) == MJS_UNDEFINED);
  mjs_destroy(mjs);
}

static void test_if(void) {
  struct mjs *mjs = mjs_create();
  // printf("---> %s\n", mjs_stringify(mjs, mjs_eval(mjs, "if (true) 1", -1)));
  assert(numexpr(mjs, "if (true) 1;", 1.0f));
  assert(mjs_eval(mjs, "if (0) 1;", -1) == MJS_UNDEFINED);
  assert(mjs_eval(mjs, "true", -1) == MJS_TRUE);
  assert(mjs_eval(mjs, "false", -1) == MJS_FALSE);
  assert(mjs_eval(mjs, "null", -1) == MJS_NULL);
  assert(mjs_eval(mjs, "undefined", -1) == MJS_UNDEFINED);
  mjs_destroy(mjs);
}

static void test_function(void) {
  struct mjs *mjs = mjs_create();
  CHECK_NUMERIC("let f = function(){ 1; }; 1;", 1);
  CHECK_NUMERIC("let fx = function(a){ return a; }; 1;", 1);
  CHECK_NUMERIC("let fy = function(a){ return a; }; fy(5);", 5);
  CHECK_NUMERIC("(function(a){ return a; })(5);", 5);
  CHECK_NUMERIC("let f1 = function(a){ 1; }; 1;", 1);
  CHECK_NUMERIC("let f2 = function(a,b){ 1; }; 1;", 1);
  CHECK_NUMERIC("let f3 = function(a,b){ return a; }; f3(7,2);", 7);
  CHECK_NUMERIC("let f4 = function(a,b){ return b; }; f4(1,2);", 2);
  CHECK_NUMERIC("let f5 = function(a,b){ return b; }; f5(1,2);", 2);
  assert(mjs_eval(mjs, "(function(a,b){return b;})(1);", -1) == MJS_UNDEFINED);
  mjs_destroy(mjs);
}

static void test_objects(void) {
  struct mjs *mjs = mjs_create();
  assert(typeexpr(mjs, "let o = {}; o", MJS_TYPE_OBJECT));
  assert(typeexpr(mjs, "let o2 = {a:1}; o2", MJS_TYPE_OBJECT));
  // assert(mjs_eval(mjs, "let o = {}; o.b", -1) == MJS_UNDEFINED);
  mjs_destroy(mjs);
}

int main(void) {
  test_if();
  test_strings();
  test_expr();
  test_ffi();
  test_scopes();
  test_function();
  test_objects();
  printf("TEST PASSED\n");
  return 0;
}

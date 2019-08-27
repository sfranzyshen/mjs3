#include "elk.c"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// int _ASSERT_streq(const char *actual, const char *expected);
// int _ASSERT_streq_nz(const char *actual, const char *expected);
// void _strfail(const char *a, const char *e, int len);

#define FAIL(str, line)                                              \
  do {                                                               \
    printf("%s:%d:1 [%s] (in %s)\n", __FILE__, line, str, __func__); \
    return str;                                                      \
  } while (0)

#define ASSERT(expr)                    \
  do {                                  \
    g_num_checks++;                     \
    if (!(expr)) FAIL(#expr, __LINE__); \
  } while (0)

#define RUN_TEST(fn)                                             \
  do {                                                           \
    clock_t started = clock();                                   \
    int n = g_num_checks;                                        \
    const char *msg = fn();                                      \
    double took = (double) (clock() - started) / CLOCKS_PER_SEC; \
    printf("  [%.3f %3d] %s\n", took, g_num_checks - n, #fn);    \
    fflush(stdout);                                              \
    if (msg) return msg;                                         \
  } while (0)

#define CHECK_NUMERIC(x, y) ASSERT(numexpr(vm, (x), (y)))

static int g_num_checks;

static bool check_num(struct elk *vm, jsval_t v, float expected) {
  // printf("%s: %g %g\n", __func__, tof(v), expected);
  if (js_type(v) == MJS_TYPE_ERROR) printf("ERROR: %s\n", vm->error_message);
  return js_type(v) == MJS_TYPE_NUMBER &&
         fabs(js_to_float(v) - expected) < 0.0001;
}

static int check_str(struct elk *vm, jsval_t v, const char *expected) {
  jslen_t len;
  const char *p = js_to_str(vm, v, &len);
  return js_type(v) == MJS_TYPE_STRING && len == strlen(expected) &&
         memcmp(p, expected, len) == 0;
}

static bool numexpr(struct elk *vm, const char *code, float expected) {
  return check_num(vm, js_eval(vm, code, strlen(code)), expected);
}

static int strexpr(struct elk *vm, const char *code, const char *expected) {
  jsval_t v = js_eval(vm, code, strlen(code));
  // printf("%s: %s\n", __func__, tostr(vm, v));
  return js_type(v) != MJS_TYPE_STRING ? 0 : check_str(vm, v, expected);
}

static int typeexpr(struct elk *vm, const char *code, js_type_t t) {
  jsval_t v = js_eval(vm, code, strlen(code));
  return js_type(v) == t;
}

static const char *test_expr(void) {
  struct elk *vm = js_create();

  ASSERT(js_eval(vm, ";;;", -1) == MJS_UNDEFINED);
  ASSERT(js_eval(vm, "let a", -1) == MJS_UNDEFINED);
  ASSERT(js_eval(vm, "let a", -1) == MJS_ERROR);
  ASSERT(typeexpr(vm, "let ax, bx = function(x){}", MJS_TYPE_FUNCTION));
  ASSERT(typeexpr(vm, "let ay, by = function(x){}, c", MJS_TYPE_UNDEFINED));

  ASSERT(numexpr(vm, "let aq = 1;", 1.0f));
  ASSERT(numexpr(vm, "let aw = 1, be = 2;", 2.0f));
  ASSERT(numexpr(vm, "123", 123.0f));
  ASSERT(numexpr(vm, "123;", 123.0f));
  ASSERT(numexpr(vm, "{123}", 123.0f));
  ASSERT(numexpr(vm, "1 + 2 * 3.7 - 7 % 3", 7.4f));
  ASSERT(numexpr(vm, "let ag = 1.23, bg = 5.3;", 5.3f));
  ASSERT(numexpr(vm, "ag;", 1.23f));
  ASSERT(numexpr(vm, "ag - 2 * 3.1;", -4.97f));
  ASSERT(numexpr(vm,
                 "let az = 1.23; az + 1; let fz = function(a) "
                 "{ return az + 1; }; 1;",
                 1));
  ASSERT(numexpr(vm, "2 * (1 + 2)", 6.0f));
  ASSERT(numexpr(vm, "let at = 9; while (at) at--;", 0.0f));
  ASSERT(numexpr(vm, "let a2 = 9, b2 = 0; while (a2) { a2--; } ", 0.0f));
  ASSERT(numexpr(vm, "let a3 = 9, b3 = 0; while (a3) a3--; b3++; ", 0.0f));
  ASSERT(numexpr(vm, "b3", 1.0f));
  ASSERT(numexpr(vm, "let a4 = 9, b4 = 7; while (a4){a4--;b4++;} b4", 16.0f));

  ASSERT(numexpr(vm, "let q = 1; q++;", 1.0f));
  ASSERT(numexpr(vm, "q;", 2.0f));
  ASSERT(numexpr(vm, "q--;", 2.0f));
  ASSERT(numexpr(vm, "q;", 1.0f));
  ASSERT(strexpr(vm, "typeof q", "number"));
  ASSERT(strexpr(vm, "typeof(q)", "number"));
  ASSERT(strexpr(vm, "typeof('aa')", "string"));
  ASSERT(strexpr(vm, "typeof(bx)", "function"));

  ASSERT(numexpr(vm, "0x64", 100));
  ASSERT(numexpr(vm, "0x7fffffff", 0x7fffffff));
  ASSERT(numexpr(vm, "0xffffffff", 0xffffffff));
  ASSERT(numexpr(vm, "123.4", 123.4));
  ASSERT(numexpr(vm, "200+50", 250));
  ASSERT(numexpr(vm, "1-2*3", -5));
  ASSERT(numexpr(vm, "1-2+3", 2));
  ASSERT(numexpr(vm, "200-50", 150));
  ASSERT(numexpr(vm, "200*50", 10000));
  ASSERT(numexpr(vm, "200/50", 4));
  ASSERT(numexpr(vm, "200 % 21", 11));
  // ASSERT_EXEC_OK(js_exec(vm, "200 % 0.999", &res));
  // ASSERT(isnan(js_get_double(vm, res)));
  ASSERT(numexpr(vm, "5 % 2", 1));
  ASSERT(numexpr(vm, "5 % -2", 1));
  // ASSERT(numexpr(vm, "-5 % 2", -1));
  // ASSERT(numexpr(vm, "-5 % -2", -1));
  ASSERT(numexpr(vm, "100 << 3", 800));
  ASSERT(numexpr(vm, "(0-14) >> 2", -4));
  ASSERT(numexpr(vm, "(0-14) >>> 2", 1073741820));
  ASSERT(numexpr(vm, "6 & 3", 2));
  ASSERT(numexpr(vm, "6 | 3", 7));
  ASSERT(numexpr(vm, "6 ^ 3", 5));

  ASSERT(numexpr(vm, "0.1 + 0.2", 0.3));
  ASSERT(numexpr(vm, "123.4 + 0.1", 123.5));

  // printf("--> %s\n", js_stringify(vm, js_eval(vm, "~10", -1)));
  ASSERT(numexpr(vm, "{let a = 200; a += 50; a}", 250));
  ASSERT(numexpr(vm, "{let a = 200; a -= 50; a}", 150));
  ASSERT(numexpr(vm, "{let a = 200; a *= 50; a}", 10000));
  ASSERT(numexpr(vm, "{let a = 200; a /= 50; a}", 4));
  ASSERT(numexpr(vm, "{let a = 200; a %= 21; a}", 11));
  ASSERT(numexpr(vm, "{let a = 100; a <<= 3; a}", 800));
  ASSERT(numexpr(vm, "{let a = 0-14; a >>= 2; a}", -4));
  ASSERT(numexpr(vm, "{let a = 0-14; a >>>= 2; a}", 1073741820));
  ASSERT(numexpr(vm, "{let a = 6; a &= 3; a}", 2));
  ASSERT(numexpr(vm, "{let a = 6; a |= 3; a}", 7));
  ASSERT(numexpr(vm, "{let a = 6; a ^= 3; a}", 5));

  ASSERT(js_eval(vm, "!0", -1) == MJS_TRUE);
  ASSERT(js_eval(vm, "!1", -1) == MJS_FALSE);
  ASSERT(js_eval(vm, "!''", -1) == MJS_TRUE);
  ASSERT(js_eval(vm, "!false", -1) == MJS_TRUE);
  ASSERT(numexpr(vm, "~10", -11));
  ASSERT(numexpr(vm, "-100", -100));
  ASSERT(numexpr(vm, "+100", 100));

  ASSERT(numexpr(vm, "2 * (3 + 4)", 14));
  ASSERT(numexpr(vm, "2 * (3 + 4 / 2 * 3)", 18));

  CHECK_NUMERIC("false ? 4 : 5;", 5);
  CHECK_NUMERIC("false ? 4 : '' ? 6 : 7;", 7);
  CHECK_NUMERIC("77 ? 4 : '' ? 6 : 7;", 4);

  // TODO
  // CHECK_NUMERIC("1, 2;", 2);

  // ASSERT_EXEC_OK(js_exec(vm, "NaN", &res));
  // ASSERT_EQ(!!isnan(js_get_double(vm, res)), 1);

  // ASSERT_EXEC_OK(js_exec(vm, "NaN === NaN", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 0);

  // ASSERT_EXEC_OK(js_exec(vm, "NaN !== NaN", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 1);

  // ASSERT_EXEC_OK(js_exec(vm, "NaN === 0", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 0);

  // ASSERT_EXEC_OK(js_exec(vm, "NaN === 1", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 0);

  // ASSERT_EXEC_OK(js_exec(vm, "NaN === null", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 0);

  // ASSERT_EXEC_OK(js_exec(vm, "NaN === undefined", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 0);

  // ASSERT_EXEC_OK(js_exec(vm, "NaN === ''", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 0);

  // ASSERT_EXEC_OK(js_exec(vm, "NaN === {}", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 0);

  // ASSERT_EXEC_OK(js_exec(vm, "NaN === []", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 0);

  // ASSERT_EXEC_OK(js_exec(vm, "isNaN(NaN)", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 1);

  // ASSERT_EXEC_OK(js_exec(vm, "isNaN(NaN * 10)", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 1);

  // ASSERT_EXEC_OK(js_exec(vm, "isNaN(0)", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 0);

  // ASSERT_EXEC_OK(js_exec(vm, "isNaN('')", &res));
  // ASSERT_EQ(js_get_bool(vm, res), 0);

  js_destroy(vm);
  return NULL;
}

static const char *test_strings(void) {
  struct elk *vm = js_create();
  ASSERT(strexpr(vm, "'a'", "a"));
  ASSERT(vm->stringbuf_len == 3);
  ASSERT(strexpr(vm, "'b'", "b"));
  ASSERT(vm->stringbuf_len == 3);
  ASSERT(numexpr(vm, "1", 1.0f));
  ASSERT(vm->stringbuf_len == 0);
  ASSERT(numexpr(vm, "{let a = 1;}", 1.0f));
  ASSERT(vm->stringbuf_len == 0);
  ASSERT(numexpr(vm, "{let a = 'abc';} 1;", 1.0f));
  ASSERT(vm->stringbuf_len == 0);
  ASSERT(strexpr(vm, "'a' + 'b'", "ab"));
  ASSERT(strexpr(vm, "'vb'", "vb"));

  // Make sure strings are GC-ed
  CHECK_NUMERIC("1;", 1);
  ASSERT(vm->stringbuf_len == 0);

  ASSERT(strexpr(vm, "let a, b = function(x){}, c = 'aa'", "aa"));
  ASSERT(strexpr(vm, "let a2, b2 = function(){}, cv = 'aa'", "aa"));
  CHECK_NUMERIC("'abc'.length", 3);
  CHECK_NUMERIC("('abc' + 'xy').length", 5);
  CHECK_NUMERIC("'ы'.length", 2);
  CHECK_NUMERIC("('ы').length", 2);
  js_destroy(vm);
  return NULL;
}

static const char *test_scopes(void) {
  struct elk *vm = js_create();
  ASSERT(numexpr(vm, "1.23", 1.23f));
  ASSERT(vm->csp == 1);
  ASSERT(vm->objs[0].flags & OBJ_ALLOCATED);
  ASSERT(!(vm->objs[1].flags & OBJ_ALLOCATED));
  ASSERT(!(vm->props[0].flags & PROP_ALLOCATED));
  ASSERT(numexpr(vm, "{let a = 1.23;}", 1.23f));
  ASSERT(!(vm->objs[1].flags & OBJ_ALLOCATED));
  ASSERT(!(vm->props[0].flags & PROP_ALLOCATED));
  CHECK_NUMERIC("if (1) 2", 2);
  ASSERT(js_eval(vm, "if (0) 2;", -1) == MJS_UNDEFINED);
  CHECK_NUMERIC("{let a = 42; }", 42);
  CHECK_NUMERIC("let a = 1, b = 2; { let a = 3; b += a; } b;", 5);
  ASSERT(js_eval(vm, "{}", -1) == MJS_UNDEFINED);
  js_destroy(vm);
  return NULL;
}

static const char *test_if(void) {
  struct elk *vm = js_create();
  // printf("---> %s\n", js_stringify(vm, js_eval(vm, "if (true) 1", -1)));
  ASSERT(numexpr(vm, "if (true) 1;", 1.0f));
  ASSERT(js_eval(vm, "if (0) 1;", -1) == MJS_UNDEFINED);
  ASSERT(js_eval(vm, "true", -1) == MJS_TRUE);
  ASSERT(js_eval(vm, "false", -1) == MJS_FALSE);
  ASSERT(js_eval(vm, "null", -1) == MJS_NULL);
  ASSERT(js_eval(vm, "undefined", -1) == MJS_UNDEFINED);
  CHECK_NUMERIC("if (1) {2;}", 2);
  js_destroy(vm);
  return NULL;
}

static const char *test_function(void) {
  ind_t len;
  struct elk *vm = js_create();
  ASSERT(js_eval(vm, "let a = function(x){ return; }; a();", -1) ==
         MJS_UNDEFINED);
  CHECK_NUMERIC("let f = function(){ 1; }; 1;", 1);
  CHECK_NUMERIC("let fx = function(a){ return a; }; 1;", 1);
  CHECK_NUMERIC("let fy = function(a){ return a; }; fy(5);", 5);
  // TODO
  // CHECK_NUMERIC("(function(a){ return a; })(5);", 5);
  CHECK_NUMERIC("let f1 = function(a){ 1; }; 1;", 1);
  CHECK_NUMERIC("let f2 = function(a,b){ 1; }; 1;", 1);
  CHECK_NUMERIC("let f3 = function(a,b){ return a; }; f3(7,2);", 7);
  CHECK_NUMERIC("let f4 = function(a,b){ return b; }; f4(1,2);", 2);
  CHECK_NUMERIC("let f5 = function(a,b){ return b; }; f5(1,2);", 2);
  // TODO
  // ASSERT(js_eval(vm, "(function(a,b){return b;})(1);", -1) ==
  // MJS_UNDEFINED);
  ASSERT(strexpr(vm, "let f6 = function(x){return typeof(x);}; f6(f5);",
                 "function"));

  // Test that the function's string args get garbage collected
  js_eval(vm, "let f7 = function(s){return s.length;};", -1);
  len = vm->stringbuf_len;
  CHECK_NUMERIC("f7('abc')", 3);
  ASSERT(vm->stringbuf_len == len);

  // Test that the function's function args get garbage collected
  js_eval(vm, "let f8 = function(s){return s()};", -1);
  len = vm->stringbuf_len;
  CHECK_NUMERIC("f8(function(){return 3;})", 3);
  // ASSERT(vm->stringbuf_len == len);

  js_destroy(vm);
  return NULL;
}

static const char *test_objects(void) {
  struct elk *vm = js_create();
  ASSERT(typeexpr(vm, "let o = {}; o", MJS_TYPE_OBJECT));
  ASSERT(typeexpr(vm, "let o2 = {a:1}; o2", MJS_TYPE_OBJECT));
  ASSERT(js_eval(vm, "let o3 = {}; o3.b", -1) == MJS_UNDEFINED);
  CHECK_NUMERIC("let o4 = {a:1,b:2}; o4.a", 1);
  js_destroy(vm);
  return NULL;
}

static float pi(void) {
  return 3.1415926f;
}

static float sub(float a, float b) {
  return a - b;
}

static char *fmt(const char *fmt, float f) {  // Format float value
  static char buf[20];
  snprintf(buf, sizeof(buf), fmt, f);
  return buf;
}

static double mul(double a, double b) {
  // printf("%s %g %g\n", __func__, a, b);
  return a * b;
}

static int callcb(int (*cb)(int, int, void *), void *arg) {
  // printf("calling %p, arg %p\n", cb, arg);
  return cb(2, 3, arg);
}

static bool fbd(double x) {
  return x > 3.14;
}

static bool fbiiiii(int n1, int n2, int n3, int n4, int n5) {
  return n1 + n2 + n3 + n4 + n5;
}

static bool fb(void) {
  return true;
}

struct foo {
  int n;
  unsigned char x;
  char *data;
  int len;
};

static int gi(void *base, int offset) {
  return *(int *) ((char *) base + offset);
}

static void *gp(void *base, int offset) {
  return *(void **) ((char *) base + offset);
}

static int gu8(void *base, int offset) {
  // printf("%s --> %p %d\n", __func__, base, offset);
  return *((unsigned char *) base + offset);
}

static int cb1(int (*cb)(struct foo *, void *), void *arg) {
  struct foo foo = {1, 4, (char *) "some data", 4};
  // printf("%s --> %p\n", __func__, foo.data);
  return cb(&foo, arg);
}

static void jslog(const char *s) {
  // printf("%s\n", s);
  (void) s;
}

static bool xx(bool arg) {
  return !arg;
}

static const char *test_ffi(void) {
  struct elk *vm = js_create();

  js_ffi(vm, tostr, "smj");
  js_ffi(vm, xx, "bb");
  CHECK_NUMERIC("xx(true) ? 2 : 3;", 3);
  CHECK_NUMERIC("xx(false) ? 2 : 3;", 2);

  {
    struct elk *vm = js_create();
    js_ffi(vm, xx, "bl");
    ASSERT(js_eval(vm, "xx(0);", -1) == MJS_ERROR);
    js_destroy(vm);
  }
  {
    struct elk *vm = js_create();
    js_ffi(vm, xx, "lb");
    ASSERT(js_eval(vm, "xx(0);", -1) == MJS_ERROR);
    js_destroy(vm);
  }

  js_ffi(vm, jslog, "vs");
  ASSERT(js_eval(vm, "jslog('ffi js/c ok');", -1) == MJS_UNDEFINED);

  js_ffi(vm, gi, "ipi");
  js_ffi(vm, gu8, "ipi");
  js_ffi(vm, gp, "ppi");
  js_ffi(vm, cb1, "i[ipu]u");
  CHECK_NUMERIC(
      "cb1(function(a,b){"
      "let p = gp(a,0); return gi(a,0) + gu8(a,4);},0);",
      5);
  CHECK_NUMERIC(
      "cb1(function(a){let x = gp(a,8); "
      "return gi(a,0) + gu8(a,4) + gu8(x, 0); },0)",
      120);

  js_ffi(vm, fb, "b");
  ASSERT(js_eval(vm, "fb();", -1) == MJS_TRUE);

  js_ffi(vm, fbiiiii, "biiiii");
  ASSERT(js_eval(vm, "fbiiiii(1,1,1,1,1);", -1) == MJS_TRUE);
  ASSERT(js_eval(vm, "fbiiiii(1,-1,1,-1,0);", -1) == MJS_FALSE);

  js_ffi(vm, fbd, "bd");
  ASSERT(js_eval(vm, "fbd(3.15);", -1) == MJS_TRUE);
  ASSERT(js_eval(vm, "fbd(3.13);", -1) == MJS_FALSE);

  js_ffi(vm, pi, "f");
  ASSERT(numexpr(vm, "pi() * 2;", 6.2831852));

  js_ffi(vm, sub, "fff");
  ASSERT(numexpr(vm, "sub(1.17,3.12);", -1.95));
  ASSERT(numexpr(vm, "sub(0, 0xff);", -255));
  ASSERT(numexpr(vm, "sub(0xffffff, 0);", 0xffffff));
  ASSERT(numexpr(vm, "sub(pi(), 0);", 3.1415926f));

  js_ffi(vm, fmt, "ssf");
  ASSERT(strexpr(vm, "fmt('%.2f', pi());", "3.14"));

  js_ffi(vm, mul, "ddd");
  ASSERT(numexpr(vm, "mul(1.323, 7.321)", 9.685683f));

  js_ffi(vm, callcb, "i[iiiu]u");
  ASSERT(numexpr(vm, "callcb(function(a,b,c){return a+b;}, 123);", 5));

  js_ffi(vm, strlen, "is");
  ASSERT(numexpr(vm, "strlen('abc')", 3));

  js_destroy(vm);
  return NULL;
}

static const char *test_subscript(void) {
  struct elk *vm = js_create();
  ASSERT(js_eval(vm, "123[0]", -1) == MJS_ERROR);
  ASSERT(js_eval(vm, "'abc'[-1]", -1) == MJS_UNDEFINED);
  ASSERT(js_eval(vm, "'abc'[3]", -1) == MJS_UNDEFINED);
  ASSERT(strexpr(vm, "'abc'[0]", "a"));
  ASSERT(strexpr(vm, "'abc'[1]", "b"));
  ASSERT(strexpr(vm, "'abc'[2]", "c"));
  js_destroy(vm);
  return NULL;
}

static const char *test_notsupported(void) {
  struct elk *vm = js_create();
  ASSERT(js_eval(vm, "void", -1) == MJS_ERROR);
  js_destroy(vm);
  return NULL;
}

static const char *test_comments(void) {
  struct elk *vm = js_create();
  CHECK_NUMERIC("// hi there!!\n/*\n\n fooo */\n\n   \t\t1", 1);
  CHECK_NUMERIC("1 /* foo */ + /* 3 bar */ 2", 3);
  js_destroy(vm);
  return NULL;
}

static const char *test_stringify(void) {
  struct elk *vm = js_create();
  const char *expected;
  js_ffi(vm, tostr, "smj");
  expected = "{\"a\":1,\"b\":3.14}";
  ASSERT(strexpr(vm, "tostr(0,{a:1,b:3.14});", expected));
  expected = "{\"a\":true,\"b\":false}";
  ASSERT(strexpr(vm, "tostr(0,{a:true,b:false});", expected));
  expected = "{\"a\":\"function(){}\"}";
  ASSERT(strexpr(vm, "tostr(0,{a:function(){}});", expected));
  expected = "{\"a\":cfunc}";
  ASSERT(strexpr(vm, "tostr(0,{a:tostr});", expected));
  expected = "{\"a\":null}";
  ASSERT(strexpr(vm, "tostr(0,{a:null});", expected));
  expected = "{\"a\":undefined}";
  ASSERT(strexpr(vm, "tostr(0,{a:undefined});", expected));
  expected = "{\"a\":\"b\"}";
  ASSERT(strexpr(vm, "tostr(0,{a:'b'});", expected));
  js_destroy(vm);
  return NULL;
}

static const char *run_all_tests(void) {
  RUN_TEST(test_stringify);
  RUN_TEST(test_if);
  RUN_TEST(test_strings);
  RUN_TEST(test_expr);
  RUN_TEST(test_ffi);
  RUN_TEST(test_subscript);
  RUN_TEST(test_scopes);
  RUN_TEST(test_function);
  RUN_TEST(test_objects);
  RUN_TEST(test_notsupported);
  RUN_TEST(test_comments);
  return NULL;
}

int main(void) {
  clock_t started = clock();
  const char *fail_msg = run_all_tests();
  printf("%s, ran %d tests in %.3lfs\n", fail_msg ? "FAIL" : "PASS",
         g_num_checks, (double) (clock() - started) / CLOCKS_PER_SEC);
  return 0;
}

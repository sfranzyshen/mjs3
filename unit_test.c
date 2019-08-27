#include "mjs.c"

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
    const char *msg = fn();                                      \
    double took = (double) (clock() - started) / CLOCKS_PER_SEC; \
    printf("  [%.3f] %s\n", took, #fn);                          \
    fflush(stdout);                                              \
    if (msg) return msg;                                         \
  } while (0)

#define CHECK_NUMERIC(x, y) ASSERT(numexpr(mjs, (x), (y)))

static int g_num_checks;

static bool check_num(struct mjs *mjs, mjs_val_t v, float expected) {
  // printf("%s: %g %g\n", __func__, tof(v), expected);
  if (mjs_type(v) == MJS_TYPE_ERROR) printf("ERROR: %s\n", mjs->error_message);
  return mjs_type(v) == MJS_TYPE_NUMBER &&
         fabs(mjs_to_float(v) - expected) < 0.0001;
}

static int check_str(struct mjs *mjs, mjs_val_t v, const char *expected) {
  mjs_len_t len;
  const char *p = mjs_to_str(mjs, v, &len);
  return mjs_type(v) == MJS_TYPE_STRING && len == strlen(expected) &&
         memcmp(p, expected, len) == 0;
}

static bool numexpr(struct mjs *mjs, const char *code, float expected) {
  return check_num(mjs, mjs_eval(mjs, code, strlen(code)), expected);
}

static int strexpr(struct mjs *mjs, const char *code, const char *expected) {
  mjs_val_t v = mjs_eval(mjs, code, strlen(code));
  // printf("%s: %s\n", __func__, tostr(mjs, v));
  return mjs_type(v) != MJS_TYPE_STRING ? 0 : check_str(mjs, v, expected);
}

static int typeexpr(struct mjs *mjs, const char *code, mjs_type_t t) {
  mjs_val_t v = mjs_eval(mjs, code, strlen(code));
  return mjs_type(v) == t;
}

static const char *test_expr(void) {
  struct mjs *mjs = mjs_create();

  ASSERT(mjs_eval(mjs, ";;;", -1) == MJS_UNDEFINED);
  ASSERT(mjs_eval(mjs, "let a", -1) == MJS_UNDEFINED);
  ASSERT(mjs_eval(mjs, "let a", -1) == MJS_ERROR);
  ASSERT(typeexpr(mjs, "let ax, bx = function(x){}", MJS_TYPE_FUNCTION));
  ASSERT(typeexpr(mjs, "let ay, by = function(x){}, c", MJS_TYPE_UNDEFINED));

  ASSERT(numexpr(mjs, "let aq = 1;", 1.0f));
  ASSERT(numexpr(mjs, "let aw = 1, be = 2;", 2.0f));
  ASSERT(numexpr(mjs, "123", 123.0f));
  ASSERT(numexpr(mjs, "123;", 123.0f));
  ASSERT(numexpr(mjs, "{123}", 123.0f));
  ASSERT(numexpr(mjs, "1 + 2 * 3.7 - 7 % 3", 7.4f));
  ASSERT(numexpr(mjs, "let ag = 1.23, bg = 5.3;", 5.3f));
  ASSERT(numexpr(mjs, "ag;", 1.23f));
  ASSERT(numexpr(mjs, "ag - 2 * 3.1;", -4.97f));
  ASSERT(numexpr(mjs,
                 "let az = 1.23; az + 1; let fz = function(a) "
                 "{ return az + 1; }; 1;",
                 1));
  ASSERT(numexpr(mjs, "2 * (1 + 2)", 6.0f));
  ASSERT(numexpr(mjs, "let at = 9; while (at) at--;", 0.0f));
  ASSERT(numexpr(mjs, "let a2 = 9, b2 = 0; while (a2) { a2--; } ", 0.0f));
  ASSERT(numexpr(mjs, "let a3 = 9, b3 = 0; while (a3) a3--; b3++; ", 0.0f));
  ASSERT(numexpr(mjs, "b3", 1.0f));
  ASSERT(numexpr(mjs, "let a4 = 9, b4 = 7; while (a4){a4--;b4++;} b4", 16.0f));

  ASSERT(numexpr(mjs, "let q = 1; q++;", 1.0f));
  ASSERT(numexpr(mjs, "q;", 2.0f));
  ASSERT(numexpr(mjs, "q--;", 2.0f));
  ASSERT(numexpr(mjs, "q;", 1.0f));
  ASSERT(strexpr(mjs, "typeof q", "number"));
  ASSERT(strexpr(mjs, "typeof(q)", "number"));
  ASSERT(strexpr(mjs, "typeof('aa')", "string"));
  ASSERT(strexpr(mjs, "typeof(bx)", "function"));

  ASSERT(numexpr(mjs, "0x64", 100));
  ASSERT(numexpr(mjs, "0x7fffffff", 0x7fffffff));
  ASSERT(numexpr(mjs, "0xffffffff", 0xffffffff));
  ASSERT(numexpr(mjs, "123.4", 123.4));
  ASSERT(numexpr(mjs, "200+50", 250));
  ASSERT(numexpr(mjs, "1-2*3", -5));
  ASSERT(numexpr(mjs, "1-2+3", 2));
  ASSERT(numexpr(mjs, "200-50", 150));
  ASSERT(numexpr(mjs, "200*50", 10000));
  ASSERT(numexpr(mjs, "200/50", 4));
  ASSERT(numexpr(mjs, "200 % 21", 11));
  // ASSERT_EXEC_OK(mjs_exec(mjs, "200 % 0.999", &res));
  // ASSERT(isnan(mjs_get_double(mjs, res)));
  ASSERT(numexpr(mjs, "5 % 2", 1));
  ASSERT(numexpr(mjs, "5 % -2", 1));
  // ASSERT(numexpr(mjs, "-5 % 2", -1));
  // ASSERT(numexpr(mjs, "-5 % -2", -1));
  ASSERT(numexpr(mjs, "100 << 3", 800));
  ASSERT(numexpr(mjs, "(0-14) >> 2", -4));
  ASSERT(numexpr(mjs, "(0-14) >>> 2", 1073741820));
  ASSERT(numexpr(mjs, "6 & 3", 2));
  ASSERT(numexpr(mjs, "6 | 3", 7));
  ASSERT(numexpr(mjs, "6 ^ 3", 5));

  ASSERT(numexpr(mjs, "0.1 + 0.2", 0.3));
  ASSERT(numexpr(mjs, "123.4 + 0.1", 123.5));

  // printf("--> %s\n", mjs_stringify(mjs, mjs_eval(mjs, "~10", -1)));
  ASSERT(numexpr(mjs, "{let a = 200; a += 50; a}", 250));
  ASSERT(numexpr(mjs, "{let a = 200; a -= 50; a}", 150));
  ASSERT(numexpr(mjs, "{let a = 200; a *= 50; a}", 10000));
  ASSERT(numexpr(mjs, "{let a = 200; a /= 50; a}", 4));
  ASSERT(numexpr(mjs, "{let a = 200; a %= 21; a}", 11));
  ASSERT(numexpr(mjs, "{let a = 100; a <<= 3; a}", 800));
  ASSERT(numexpr(mjs, "{let a = 0-14; a >>= 2; a}", -4));
  ASSERT(numexpr(mjs, "{let a = 0-14; a >>>= 2; a}", 1073741820));
  ASSERT(numexpr(mjs, "{let a = 6; a &= 3; a}", 2));
  ASSERT(numexpr(mjs, "{let a = 6; a |= 3; a}", 7));
  ASSERT(numexpr(mjs, "{let a = 6; a ^= 3; a}", 5));

  ASSERT(mjs_eval(mjs, "!0", -1) == MJS_TRUE);
  ASSERT(mjs_eval(mjs, "!1", -1) == MJS_FALSE);
  ASSERT(mjs_eval(mjs, "!''", -1) == MJS_TRUE);
  ASSERT(mjs_eval(mjs, "!false", -1) == MJS_TRUE);
  ASSERT(numexpr(mjs, "~10", -11));
  ASSERT(numexpr(mjs, "-100", -100));
  ASSERT(numexpr(mjs, "+100", 100));

  ASSERT(numexpr(mjs, "2 * (3 + 4)", 14));
  ASSERT(numexpr(mjs, "2 * (3 + 4 / 2 * 3)", 18));

  CHECK_NUMERIC("false ? 4 : 5;", 5);
  CHECK_NUMERIC("false ? 4 : '' ? 6 : 7;", 7);
  CHECK_NUMERIC("77 ? 4 : '' ? 6 : 7;", 4);

  // TODO
  // CHECK_NUMERIC("1, 2;", 2);

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
  return NULL;
}

static const char *test_strings(void) {
  struct mjs *mjs = mjs_create();
  ASSERT(strexpr(mjs, "'a'", "a"));
  ASSERT(mjs->stringbuf_len == 3);
  ASSERT(strexpr(mjs, "'b'", "b"));
  ASSERT(mjs->stringbuf_len == 3);
  ASSERT(numexpr(mjs, "1", 1.0f));
  ASSERT(mjs->stringbuf_len == 0);
  ASSERT(numexpr(mjs, "{let a = 1;}", 1.0f));
  ASSERT(mjs->stringbuf_len == 0);
  ASSERT(numexpr(mjs, "{let a = 'abc';} 1;", 1.0f));
  ASSERT(mjs->stringbuf_len == 0);
  ASSERT(strexpr(mjs, "'a' + 'b'", "ab"));
  ASSERT(strexpr(mjs, "'vb'", "vb"));

  // Make sure strings are GC-ed
  CHECK_NUMERIC("1;", 1);
  ASSERT(mjs->stringbuf_len == 0);

  ASSERT(strexpr(mjs, "let a, b = function(x){}, c = 'aa'", "aa"));
  ASSERT(strexpr(mjs, "let a2, b2 = function(){}, cv = 'aa'", "aa"));
  CHECK_NUMERIC("'abc'.length", 3);
  CHECK_NUMERIC("('abc' + 'xy').length", 5);
  CHECK_NUMERIC("'ы'.length", 2);
  CHECK_NUMERIC("('ы').length", 2);
  mjs_destroy(mjs);
  return NULL;
}

static const char *test_scopes(void) {
  struct mjs *mjs = mjs_create();
  ASSERT(numexpr(mjs, "1.23", 1.23f));
  ASSERT(mjs->csp == 1);
  ASSERT(mjs->objs[0].flags & OBJ_ALLOCATED);
  ASSERT(!(mjs->objs[1].flags & OBJ_ALLOCATED));
  ASSERT(!(mjs->props[0].flags & PROP_ALLOCATED));
  ASSERT(numexpr(mjs, "{let a = 1.23;}", 1.23f));
  ASSERT(!(mjs->objs[1].flags & OBJ_ALLOCATED));
  ASSERT(!(mjs->props[0].flags & PROP_ALLOCATED));
  CHECK_NUMERIC("if (1) 2", 2);
  ASSERT(mjs_eval(mjs, "if (0) 2;", -1) == MJS_UNDEFINED);
  CHECK_NUMERIC("{let a = 42; }", 42);
  CHECK_NUMERIC("let a = 1, b = 2; { let a = 3; b += a; } b;", 5);
  ASSERT(mjs_eval(mjs, "{}", -1) == MJS_UNDEFINED);
  mjs_destroy(mjs);
  return NULL;
}

static const char *test_if(void) {
  struct mjs *mjs = mjs_create();
  // printf("---> %s\n", mjs_stringify(mjs, mjs_eval(mjs, "if (true) 1", -1)));
  ASSERT(numexpr(mjs, "if (true) 1;", 1.0f));
  ASSERT(mjs_eval(mjs, "if (0) 1;", -1) == MJS_UNDEFINED);
  ASSERT(mjs_eval(mjs, "true", -1) == MJS_TRUE);
  ASSERT(mjs_eval(mjs, "false", -1) == MJS_FALSE);
  ASSERT(mjs_eval(mjs, "null", -1) == MJS_NULL);
  ASSERT(mjs_eval(mjs, "undefined", -1) == MJS_UNDEFINED);
  CHECK_NUMERIC("if (1) {2;}", 2);
  mjs_destroy(mjs);
  return NULL;
}

static const char *test_function(void) {
  ind_t len;
  struct mjs *mjs = mjs_create();
  ASSERT(mjs_eval(mjs, "let a = function(x){ return; }; a();", -1) ==
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
  // ASSERT(mjs_eval(mjs, "(function(a,b){return b;})(1);", -1) ==
  // MJS_UNDEFINED);
  ASSERT(strexpr(mjs, "let f6 = function(x){return typeof(x);}; f6(f5);",
                 "function"));

  // Test that the function's string args get garbage collected
  mjs_eval(mjs, "let f7 = function(s){return s.length;};", -1);
  len = mjs->stringbuf_len;
  CHECK_NUMERIC("f7('abc')", 3);
  ASSERT(mjs->stringbuf_len == len);

  // Test that the function's function args get garbage collected
  mjs_eval(mjs, "let f8 = function(s){return s()};", -1);
  len = mjs->stringbuf_len;
  CHECK_NUMERIC("f8(function(){return 3;})", 3);
  // ASSERT(mjs->stringbuf_len == len);

  mjs_destroy(mjs);
  return NULL;
}

static const char *test_objects(void) {
  struct mjs *mjs = mjs_create();
  ASSERT(typeexpr(mjs, "let o = {}; o", MJS_TYPE_OBJECT));
  ASSERT(typeexpr(mjs, "let o2 = {a:1}; o2", MJS_TYPE_OBJECT));
  ASSERT(mjs_eval(mjs, "let o3 = {}; o3.b", -1) == MJS_UNDEFINED);
  CHECK_NUMERIC("let o4 = {a:1,b:2}; o4.a", 1);
  mjs_destroy(mjs);
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

static bool fb(void) {
  return true;
}

static bool fbd(double x) {
  // printf("x: %g\n", x);
  return x > 3.14;
}

static bool fbiiiii(int n1, int n2, int n3, int n4, int n5) {
  return n1 + n2 + n3 + n4 + n5;
}

static void jslog(const char *s) {
  (void) s;
}

struct foo {
  int n;
  unsigned char x;
  char *data;
  int len;
};

#if 0
struct sdef {
  const char *name;
  size_t offset;
};

static struct sdef *foodef(void) {
  static struct sdef def[] = {{"n", offsetof(struct foo, n)}, {NULL, 0}};
  return def;
}
#endif

static int getint(void *base, int offset) {
  return *(int *) ((char *) base + offset);
}

static int getu8(void *base, int offset) {
  return *((unsigned char *) base + offset);
}

static int cb1(int (*cb)(struct foo *, void *), void *arg) {
  struct foo foo = {1, 4, (char *) "some data", 4};
  return cb(&foo, arg);
}

static bool xx(bool arg) {
  return !arg;
}

static const char *test_ffi(void) {
  struct mjs *mjs = mjs_create();

  ASSERT(mjs_ffi(mjs, "xx", (cfn_t) xx, "bb") == MJS_TRUE);
  CHECK_NUMERIC("xx(true) ? 2 : 3;", 3);
  CHECK_NUMERIC("xx(false) ? 2 : 3;", 2);

  {
    struct mjs *vm = mjs_create();
    ASSERT(mjs_ffi(vm, "a", (cfn_t) xx, "bl") == MJS_TRUE);
    ASSERT(mjs_eval(mjs, "a(0);", -1) == MJS_ERROR);
    ASSERT(mjs_ffi(vm, "b", (cfn_t) xx, "lb") == MJS_TRUE);
    ASSERT(mjs_eval(mjs, "f(0);", -1) == MJS_ERROR);
    mjs_destroy(vm);
  }

  ASSERT(mjs_ffi(mjs, "log", (cfn_t) jslog, "vs") == MJS_TRUE);
  ASSERT(mjs_eval(mjs, "log('ffi js/c ok');", -1) == MJS_UNDEFINED);

  ASSERT(mjs_ffi(mjs, "gi", (cfn_t) getint, "ipi") == MJS_TRUE);
  ASSERT(mjs_ffi(mjs, "gu8", (cfn_t) getu8, "ipi") == MJS_TRUE);

  ASSERT(mjs_ffi(mjs, "f2", (cfn_t) fb, "b") == MJS_TRUE);
  ASSERT(mjs_eval(mjs, "f2();", -1) == MJS_TRUE);

  ASSERT(mjs_ffi(mjs, "f3", (cfn_t) fbiiiii, "biiiii") == MJS_TRUE);
  ASSERT(mjs_eval(mjs, "f3(1,1,1,1,1);", -1) == MJS_TRUE);
  ASSERT(mjs_eval(mjs, "f3(1,-1,1,-1,0);", -1) == MJS_FALSE);

  ASSERT(mjs_ffi(mjs, "f4", (cfn_t) fbd, "bd") == MJS_TRUE);
  ASSERT(mjs_eval(mjs, "f4(3.15);", -1) == MJS_TRUE);
  ASSERT(mjs_eval(mjs, "f4(3.13);", -1) == MJS_FALSE);

  ASSERT(mjs_ffi(mjs, "f1", (cfn_t) cb1, "i[ipu]u") == MJS_TRUE);
  ASSERT(numexpr(mjs, "f1(function(a,b){return gi(a,0) + gu8(a,4);},0);", 5));

  ASSERT(mjs_ffi(mjs, "pi", (cfn_t) pi, "f") == MJS_TRUE);
  ASSERT(numexpr(mjs, "pi() * 2;", 6.2831852));

  ASSERT(mjs_ffi(mjs, "sub", (cfn_t) sub, "fff") == MJS_TRUE);
  ASSERT(numexpr(mjs, "sub(1.17,3.12);", -1.95));
  ASSERT(numexpr(mjs, "sub(0, 0xff);", -255));
  ASSERT(numexpr(mjs, "sub(0xffffff, 0);", 0xffffff));
  ASSERT(numexpr(mjs, "sub(pi(), 0);", 3.1415926f));

  ASSERT(mjs_ffi(mjs, "fmt", (cfn_t) fmt, "ssf") == MJS_TRUE);
  ASSERT(strexpr(mjs, "fmt('%.2f', pi());", "3.14"));

  ASSERT(mjs_ffi(mjs, "mul", (cfn_t) mul, "ddd") == MJS_TRUE);
  ASSERT(numexpr(mjs, "mul(1.323, 7.321)", 9.685683f));

  mjs_ffi(mjs, "ccb", (cfn_t) callcb, "i[iiiu]u");
  ASSERT(numexpr(mjs, "ccb(function(a,b,c){return a+b;}, 123);", 5));

  mjs_ffi(mjs, "strlen", (cfn_t) strlen, "is");
  ASSERT(numexpr(mjs, "strlen('abc')", 3));

  mjs_destroy(mjs);
  return NULL;
}

static const char *test_subscript(void) {
  struct mjs *mjs = mjs_create();
  ASSERT(mjs_eval(mjs, "123[0]", -1) == MJS_ERROR);
  ASSERT(mjs_eval(mjs, "'abc'[-1]", -1) == MJS_UNDEFINED);
  ASSERT(mjs_eval(mjs, "'abc'[3]", -1) == MJS_UNDEFINED);
  ASSERT(strexpr(mjs, "'abc'[0]", "a"));
  ASSERT(strexpr(mjs, "'abc'[1]", "b"));
  ASSERT(strexpr(mjs, "'abc'[2]", "c"));
  mjs_destroy(mjs);
  return NULL;
}

static const char *test_notsupported(void) {
  struct mjs *mjs = mjs_create();
  ASSERT(mjs_eval(mjs, "void", -1) == MJS_ERROR);
  mjs_destroy(mjs);
  return NULL;
}

static const char *test_comments(void) {
  struct mjs *mjs = mjs_create();
  CHECK_NUMERIC("// hi there!!\n/*\n\n fooo */\n\n   \t\t1", 1);
  mjs_destroy(mjs);
  return NULL;
}

static const char *test_stringify(void) {
  struct mjs *mjs = mjs_create();
  const char *expected;
  mjs_ffi(mjs, "str", (cfn_t) tostr, "smj");
  expected = "{\"a\":1,\"b\":3.14}";
  ASSERT(strexpr(mjs, "str(0,{a:1,b:3.14});", expected));
  expected = "{\"a\":true,\"b\":false}";
  ASSERT(strexpr(mjs, "str(0,{a:true,b:false});", expected));
  expected = "{\"a\":function(){}}";
  ASSERT(strexpr(mjs, "str(0,{a:function(){}});", expected));
  expected = "{\"a\":cfunc}";
  ASSERT(strexpr(mjs, "str(0,{a:str});", expected));
  expected = "{\"a\":null}";
  ASSERT(strexpr(mjs, "str(0,{a:null});", expected));
  expected = "{\"a\":undefined}";
  ASSERT(strexpr(mjs, "str(0,{a:undefined});", expected));
  mjs_destroy(mjs);
  return NULL;
}

static const char *run_all_tests(void) {
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
  RUN_TEST(test_stringify);
  return NULL;
}

int main(void) {
  clock_t started = clock();
  const char *fail_msg = run_all_tests();
  printf("%s, ran %d tests in %.3lfs\n", fail_msg ? "FAIL" : "PASS",
         g_num_checks, (double) (clock() - started) / CLOCKS_PER_SEC);
  return 0;
}

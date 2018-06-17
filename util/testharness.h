
#ifndef MYCC_TEST_TESTHARNESS_H_
#define MYCC_TEST_TESTHARNESS_H_

#include <assert.h>
#include <errno.h>
#include <execinfo.h> // linux
#include <stdio.h>
#include <stdlib.h>
#include <sstream>
#include "types_util.h"

namespace mycc
{
namespace test
{

/// Print error utils

#define STR_ERRORNO() (errno == 0 ? "None" : strerror(errno))

#define PRINT_INFO(fmt, ...)                                \
  fprintf(stderr, "INFO [%s:%d](%s) " fmt,                  \
          __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
  fflush(stderr)

#define PRINT_WARN(fmt, ...)                                                      \
  fprintf(stderr, "WARN [%s:%d](%s) errno: %d %s, " fmt,                          \
          __FILE__, __LINE__, __FUNCTION__, errno, STR_ERRORNO(), ##__VA_ARGS__); \
  fflush(stderr)

#define PRINT_ERROR(fmt, ...)                                                     \
  fprintf(stderr, "ERROR [%s:%d](%s) errno: %d %s, " fmt,                         \
          __FILE__, __LINE__, __FUNCTION__, errno, STR_ERRORNO(), ##__VA_ARGS__); \
  fflush(stderr)

#define PRINT_FATAL(fmt, ...)                                                     \
  fprintf(stderr, "FATAL [%s:%d](%s) errno: %d %s, " fmt,                         \
          __FILE__, __LINE__, __FUNCTION__, errno, STR_ERRORNO(), ##__VA_ARGS__); \
  fflush(stderr)                                                                  \
      abort()

// panic utils
#define PANIC(fmt, ...)                                                           \
  fprintf(stderr, "PANIC [%s:%d](%s) errno: %d %s, " fmt,                         \
          __FILE__, __LINE__, __FUNCTION__, errno, STR_ERRORNO(), ##__VA_ARGS__); \
  fflush(stderr);                                                                 \
  abort()

#define PANIC_ENFORCE(c, fmt, ...)                 \
  if (!(c))                                        \
  {                                                \
    PANIC("%s is False, " fmt, #c, ##__VA_ARGS__); \
  }

#define PANIC_TRUE(c)               \
  if (!(c))                         \
  {                                 \
    PANIC("%s is not TRUE \n", #c); \
  }

#define PANIC_FALSE(c)               \
  if (c)                             \
  {                                  \
    PANIC("%s is not FALSE \n", #c); \
  }

#define PANIC_EQ(c, val)                   \
  if ((c) != (val))                        \
  {                                        \
    PANIC("%s is not EQ %s \n", #c, #val); \
  }

#define PANIC_NE(c, val)                   \
  if ((c) == (val))                        \
  {                                        \
    PANIC("%s is not NE %s \n", #c, #val); \
  }

#define PANIC_GE(c, val)                   \
  if ((c) < (val))                         \
  {                                        \
    PANIC("%s is not GE %s \n", #c, #val); \
  }

#define PANIC_GT(c, val)                   \
  if ((c) <= (val))                        \
  {                                        \
    PANIC("%s is not GT %s \n", #c, #val); \
  }

#define PANIC_LE(c, val)                   \
  if ((c) > (val))                         \
  {                                        \
    PANIC("%s is not LE %s \n", #c, #val); \
  }

#define PANIC_LT(c, val)                   \
  if ((c) >= (val))                        \
  {                                        \
    PANIC("%s is not LT %s \n", #c, #val); \
  }

// Run some of the tests registered by the TEST() macro.
// E.g., suppose the tests are:
//    TEST(Foo, Hello) { ... }
//    TEST(Foo, World) { ... }
//
// Returns 0 if all tests pass.
// Dies or returns a non-zero value if some test fails.
int32_t RunAllTests();

// An instance of Tester is allocated to hold temporary state during
// the execution of an assertion.
class Tester
{
private:
  bool ok_;
  const char *fname_;
  int32_t line_;
  std::stringstream ss_;

public:
  Tester(const char *f, int l)
      : ok_(true), fname_(f), line_(l)
  {
  }

  ~Tester()
  {
    if (!ok_)
    {
      PRINT_INFO("%s:%d:%s\n", fname_, line_, ss_.str().c_str());
      exit(1);
    }
  }

  Tester &Is(bool b, const char *msg)
  {
    if (!b)
    {
      ss_ << " Assertion failure " << msg;
      ok_ = false;
    }
    return *this;
  }

#define BINARY_OP(name, op)                          \
  template <class X, class Y>                        \
  Tester &name(const X &x, const Y &y)               \
  {                                                  \
    if (!(x op y))                                   \
    {                                                \
      ss_ << " failed: " << x << (" " #op " ") << y; \
      ok_ = false;                                   \
    }                                                \
    return *this;                                    \
  }

  BINARY_OP(IsEq, ==)
  BINARY_OP(IsNe, !=)
  BINARY_OP(IsGe, >=)
  BINARY_OP(IsGt, >)
  BINARY_OP(IsLe, <=)
  BINARY_OP(IsLt, <)
#undef BINARY_OP

  // Attach the specified value to the error message if an error has occurred
  template <class V>
  Tester &operator<<(const V &value)
  {
    if (!ok_)
    {
      ss_ << " " << value;
    }
    return *this;
  }
};

#define ASSERT_TRUE(c) ::mycc::test::Tester(__FILE__, __LINE__).Is((c), #c)
#define ASSERT_EQ(a, b) ::mycc::test::Tester(__FILE__, __LINE__).IsEq((a), (b))
#define ASSERT_NE(a, b) ::mycc::test::Tester(__FILE__, __LINE__).IsNe((a), (b))
#define ASSERT_GE(a, b) ::mycc::test::Tester(__FILE__, __LINE__).IsGe((a), (b))
#define ASSERT_GT(a, b) ::mycc::test::Tester(__FILE__, __LINE__).IsGt((a), (b))
#define ASSERT_LE(a, b) ::mycc::test::Tester(__FILE__, __LINE__).IsLe((a), (b))
#define ASSERT_LT(a, b) ::mycc::test::Tester(__FILE__, __LINE__).IsLt((a), (b))

#define TCONCAT(a, b) TCONCAT1(a, b)
#define TCONCAT1(a, b) a##b

#define TEST(base, name)                                                        \
  class TCONCAT(_Test_, name) : public base                                     \
  {                                                                             \
  public:                                                                       \
    void _Run();                                                                \
    static void _RunIt()                                                        \
    {                                                                           \
      TCONCAT(_Test_, name)                                                     \
      t;                                                                        \
      t._Run();                                                                 \
    }                                                                           \
  };                                                                            \
  bool TCONCAT(_Test_ignored_, name) =                                          \
      ::mycc::test::RegisterTest(#base, #name, &TCONCAT(_Test_, name)::_RunIt); \
  void TCONCAT(_Test_, name)::_Run()

#define NO_TEST(base, name)                                                     \
  class TCONCAT(_Test_, name) : public base                                     \
  {                                                                             \
  public:                                                                       \
    void _NotRun();                                                             \
    void _Run() { PRINT_INFO("Pass Test\n"); }                                  \
    static void _RunIt()                                                        \
    {                                                                           \
      TCONCAT(_Test_, name)                                                     \
      t;                                                                        \
      t._Run();                                                                 \
    }                                                                           \
  };                                                                            \
  bool TCONCAT(_Test_ignored_, name) =                                          \
      ::mycc::test::RegisterTest(#base, #name, &TCONCAT(_Test_, name)::_RunIt); \
  void TCONCAT(_Test_, name)::_NotRun()

// Register the specified test.  Typically not used directly, but
// invoked via the macro expansion of TEST.
bool RegisterTest(const char *base, const char *name, void (*func)());

class Timer
{
public:
  Timer() : start_(0) {}
  void start();
  uint64_t elapsedNanos(bool reset = false);

private:
  uint64_t start_;
};

} // namespace test
} // namespace mycc

#endif // MYCC_TEST_TESTHARNESS_H_

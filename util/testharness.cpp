
#include "testharness.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <chrono>
#include <string>
#include <vector>
#include "env_util.h"

namespace mycc
{
namespace test
{

namespace
{ // anonymous namespace
struct Test
{
  const char *base;
  const char *name;
  void (*func)();
};
std::vector<Test> *tests;
} // namespace

bool RegisterTest(const char *base, const char *name, void (*func)())
{
  if (tests == nullptr)
  {
    tests = new std::vector<Test>;
  }
  Test t;
  t.base = base;
  t.name = name;
  t.func = func;
  tests->push_back(t);
  return true;
}

int32_t RunAllTests()
{
  const char *matcher = getenv("TESTS");

  int32_t num = 0;
  if (tests != nullptr)
  {
    for (uint64_t i = 0; i < tests->size(); i++)
    {
      const Test &t = (*tests)[i];
      if (matcher != nullptr)
      {
        string name = t.base;
        name.push_back('.');
        name.append(t.name);
        if (strstr(name.c_str(), matcher) == nullptr)
        {
          continue;
        }
      }
      PRINT_INFO("==== Test %s.%s\n", t.base, t.name);
      (*t.func)();
      ++num;
    }
  }
  PRINT_INFO("==== PASSED %d tests\n", num);
  return 0;
}

void Timer::start()
{
  start_ = util::Env::NowNanos();
}

uint64_t Timer::elapsedNanos(bool reset)
{
  uint64_t now = util::Env::NowNanos();
  uint64_t elapsed = now - start_;
  if (reset)
  {
    start_ = now;
  }
  return elapsed;
}

} // namespace test
} // namespace mycc

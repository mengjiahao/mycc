
#include "error_util.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include "time_util.h"

namespace mycc
{
namespace util
{

void vreportf(const char *prefix, const char *err, va_list params)
{
  char msg[4096];
  char *p;

  vsnprintf(msg, sizeof(msg), err, params);
  for (p = msg; *p; p++)
  {
    if (iscntrl(*p) && *p != '\t' && *p != '\n')
      *p = '?';
  }
  fprintf(stderr, "%s%s\n", prefix, msg);
}

static NORETURN void usage_builtin(const char *err, va_list params)
{
  vreportf("usage: ", err, params);
  exit(129);
}

static NORETURN void panic_builtin(const char *err, va_list params)
{
  vreportf("fatal: ", err, params);
  exit(128);
}

static void error_builtin(const char *err, va_list params)
{
  vreportf("error: ", err, params);
}

static void warn_builtin(const char *warn, va_list params)
{
  vreportf("warning: ", warn, params);
}

void NORETURN usagef(const char *err, ...)
{
  va_list params;

  va_start(params, err);
  usage_builtin(err, params);
  va_end(params);
}

void NORETURN panicf(const char *err, ...)
{
  va_list params;

  va_start(params, err);
  panic_builtin(err, params);
  va_end(params);
}

int errorf(const char *err, ...)
{
  va_list params;

  va_start(params, err);
  error_builtin(err, params);
  va_end(params);
  return -1;
}

void warningf(const char *warn, ...)
{
  va_list params;

  va_start(params, warn);
  warn_builtin(warn, params);
  va_end(params);
}

string CurrentTestTimeString()
{
  return CurrentSystimeString();
}

Exception::Exception(const string &buffer)
    : _buffer(buffer), _code(0)
{
  // getBacktrace();
}

Exception::Exception(const string &buffer, int err)
{
  _buffer = buffer + " :" + strerror(err);
  _code = err;
  // getBacktrace();
}

Exception::~Exception() throw()
{
}

const char *Exception::what() const throw()
{
  return _buffer.c_str();
}

void Exception::getBacktrace()
{
  void *array[64];
  int nSize = ::backtrace(array, 64);
  char **symbols = ::backtrace_symbols(array, nSize);

  for (int i = 0; i < nSize; i++)
  {
    _buffer += symbols[i];
    _buffer += "\n";
  }
  free(symbols);
}

} // namespace util
} // namespace mycc
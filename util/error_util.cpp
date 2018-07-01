
#include "error_util.h"
#include "time_util.h"

namespace mycc
{
namespace util
{

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
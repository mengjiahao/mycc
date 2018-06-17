
#include "string_scanner.h"

namespace mycc
{
namespace util
{

void Scanner::scanUntilImpl(char end_ch, bool escaped)
{
  for (;;)
  {
    if (cur_.empty())
    {
      error();
      return;
    }
    const char ch = cur_[0];
    if (ch == end_ch)
    {
      return;
    }

    cur_.remove_prefix(1);
    if (escaped && ch == '\\')
    {
      // Escape character, skip next character.
      if (cur_.empty())
      {
        error();
        return;
      }
      cur_.remove_prefix(1);
    }
  }
}

bool Scanner::getResult(StringPiece *remaining, StringPiece *capture)
{
  if (error_)
  {
    return false;
  }
  if (remaining != nullptr)
  {
    *remaining = cur_;
  }
  if (capture != nullptr)
  {
    const char *end = capture_end_ == nullptr ? cur_.data() : capture_end_;
    *capture = StringPiece(capture_start_, end - capture_start_);
  }
  return true;
}

} // namespace util
} // namespace mycc
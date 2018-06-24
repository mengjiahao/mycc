
#include "status.h"
#include <stdio.h>

namespace mycc
{
namespace util
{

Status::Status(int32_t code)
{
  assert(code != kOk);
  state_ = std::unique_ptr<State>(new State);
  state_->code = code;
}

Status::Status(int32_t code, const StringPiece &msg)
{
  assert(code != kOk);
  state_ = std::unique_ptr<State>(new State);
  state_->code = code;
  state_->msg = msg.toString(); // copy
}

Status::Status(int32_t code, const StringPiece &msg, const StringPiece &msg2)
{
  assert(code != kOk);
  const uint64_t len1 = msg.size();
  const uint64_t len2 = msg2.size();
  const uint64_t size = len1 + (len2 ? (2 + len2) : 0);
  char result[1024]; // +1 for null terminator
  memcpy(result, msg.data(), len1);
  if (len2)
  {
    result[len1] = ':';
    result[len1 + 1] = ' ';
    memcpy(result + len1 + 2, msg2.data(), len2);
  }
  result[size] = '\0'; // null terminator for C style string
  state_ = std::unique_ptr<State>(new State);
  state_->code = code;
  state_->msg.assign(result, size); // copy
}

void Status::update(const Status &new_status)
{
  if (ok())
  {
    *this = new_status;
  }
}

void Status::slowCopyFrom(const State *src)
{
  if (src == nullptr)
  {
    state_ = nullptr;
  }
  else
  {
    state_ = std::unique_ptr<State>(new State(*src));
  }
}

const string &Status::empty_string()
{
  static string *empty = new string();
  return *empty;
}

string Status::toString() const
{
  if (state_ == nullptr)
  {
    return "OK";
  }
  else
  {
    char tmp[30];
    const char *type;
    switch (code())
    {
    case kError:
      type = "Error";
      break;
    case kIOError:
      type = "IOError";
      break;
    case kNotSupported:
      type = "NotSupported";
      break;
    default:
      snprintf(tmp, sizeof(tmp), "UserError(%d)",
               static_cast<int32_t>(code()));
      type = tmp;
      break;
    }
    string result(type);
    result += ": ";
    result += state_->msg;
    return result;
  }
}

} // namespace util
} // namespace mycc

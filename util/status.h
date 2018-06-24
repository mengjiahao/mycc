
// A Status encapsulates the result of an operation.  It may indicate success,
// or it may indicate an error with an associated error message.
//
// Multiple threads can invoke const methods on a Status without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Status must use
// external synchronization.

#ifndef MYCC_UTIL_STATUS_H_
#define MYCC_UTIL_STATUS_H_

#include <memory>
#include "status.h"
#include "stringpiece.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

/// Denotes success or failure of a call
class Status
{
public:
  static Status OK() { return Status(); }
  static Status Error(const StringPiece &msg, const StringPiece &msg2 = StringPiece())
  {
    return Status(kError, msg, msg2);
  }
  static Status IOError(const StringPiece &msg, const StringPiece &msg2 = StringPiece())
  {
    return Status(kIOError, msg, msg2);
  }
  static Status NotSupported(const StringPiece &msg, const StringPiece &msg2 = StringPiece())
  {
    return Status(kNotSupported, msg, msg2);
  }

  enum Code
  {
    kOk = 0,
    kError,
    kIOError,
    kNotSupported,
    kUserError, // kUserError < is user defined error
  };

  /// Create a success status.
  Status() : state_(nullptr) {}

  /// \brief Create a status with the specified error code and msg as a
  /// human-readable string containing more detailed information.
  Status(int32_t code);
  Status(int32_t code, const StringPiece &msg);
  Status(int32_t code, const StringPiece &msg, const StringPiece &msg2);

  /// Copy the specified status.
  Status(const Status &s);
  void operator=(const Status &s);

  /// Returns true iff the status indicates success.
  bool ok() const { return (state_ == NULL); }

  int32_t code() const
  {
    return ok() ? kOk : state_->code;
  }

  const string &error_message() const
  {
    return ok() ? empty_string() : state_->msg;
  }

  bool operator==(const Status &x) const;
  bool operator!=(const Status &x) const;

  /// \brief If `ok()`, stores `new_status` into `*this`.  If `!ok()`,
  /// preserves the current status, but may augment with additional
  /// information about `new_status`.
  ///
  /// Convenient way of keeping track of the first error encountered.
  /// Instead of:
  ///   `if (overall_status.ok()) overall_status = new_status`
  /// Use:
  ///   `overall_status.Update(new_status);`
  void update(const Status &new_status);

  /// \brief Return a string representation of this status suitable for
  /// printing. Returns the string `"OK"` for success.
  string toString() const;

private:
  static const string &empty_string();
  struct State
  {
    int32_t code;
    string msg;
  };
  // OK status has a `NULL` state_.  Otherwise, `state_` points to
  // a `State` structure containing the error code and message(s)
  std::unique_ptr<State> state_;

  void slowCopyFrom(const State *src);
};

inline Status::Status(const Status &s)
    : state_((s.state_ == NULL) ? NULL : new State(*s.state_)) {}

inline void Status::operator=(const Status &s)
{
  // The following condition catches both aliasing (when this == &s),
  // and the common case where both s and *this are ok.
  if (state_ != s.state_)
  {
    slowCopyFrom(s.state_.get());
  }
}

inline bool Status::operator==(const Status &x) const
{
  return (this->state_ == x.state_) || (toString() == x.toString());
}

inline bool Status::operator!=(const Status &x) const { return !(*this == x); }

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_STATUS_H_

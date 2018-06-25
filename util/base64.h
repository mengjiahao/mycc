
#ifndef MYCC_UTIL_BASE64_H_
#define MYCC_UTIL_BASE64_H_

#include <string>
#include "status.h"

namespace mycc
{
namespace util
{
/// \brief Converts data into web-safe base64 encoding.
///
/// See https://en.wikipedia.org/wiki/Base64
Status Base64Encode(StringPiece data, bool with_padding, string *encoded);
Status Base64Encode(StringPiece data, string *encoded); // with_padding=false.

/// \brief Converts data from web-safe base64 encoding.
///
/// See https://en.wikipedia.org/wiki/Base64
Status Base64Decode(StringPiece data, string *decoded);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_BASE64_H_
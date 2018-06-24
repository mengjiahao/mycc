#ifndef MYCC_UTIL_FILE_UTIL_H_
#define MYCC_UTIL_FILE_UTIL_H_

#include "types_util.h"

namespace mycc
{
namespace util
{

// Make file descriptor |fd| non-blocking
// Returns 0 on success, -1 otherwise and errno is set (by fcntl)
int make_non_blocking(int fd);

// Make file descriptor |fd| blocking
// Returns 0 on success, -1 otherwise and errno is set (by fcntl)
int make_blocking(int fd);

// Make file descriptor |fd| automatically closed during exec()
// Returns 0 on success, -1 when error and errno is set (by fcntl)
int make_close_on_exec(int fd);

// Disable nagling on file descriptor |socket|.
// Returns 0 on success, -1 when error and errno is set (by setsockopt)
int make_no_delay(int socket);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_FILE_UTIL_H_
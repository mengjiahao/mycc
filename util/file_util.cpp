
#include "file_util.h"
#include <fcntl.h>
#include <netinet/in.h>  // IPPROTO_TCP
#include <netinet/tcp.h> // TCP_NODELAY
#include <sys/types.h>
#include <sys/socket.h> // setsockopt

namespace mycc
{
namespace util
{

int make_non_blocking(int fd)
{
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
  {
    return flags;
  }
  if (flags & O_NONBLOCK)
  {
    return 0;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int make_blocking(int fd)
{
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
  {
    return flags;
  }
  if (flags & O_NONBLOCK)
  {
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
  }
  return 0;
}

int make_close_on_exec(int fd)
{
  return fcntl(fd, F_SETFD, FD_CLOEXEC);
}

int make_no_delay(int socket)
{
  int flag = 1;
  return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));
}

} // namespace util
} // namespace mycc

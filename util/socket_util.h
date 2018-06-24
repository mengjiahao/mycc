
#ifndef MYCC_UTIL_SOCKET_UTIL_H_
#define MYCC_UTIL_SOCKET_UTIL_H_

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <unistd.h>
#include <map>
#include <vector>
#include "locks_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

#define SOCKET_ERRNO(error) error

#define socket_errno errno
#define socket_strerror strerror

#define socket_close close

#define socket_inet_ntop inet_ntop
#define socket_inet_pton inet_pton

#define SOCKET int
#define INVALID_SOCKET -1

#define IS_NOBLOCK_CONNECT_ERRNO(err) ((err) == SOCKET_ERRNO(EINPROGRESS))

#define IS_NOBLOCK_SEND_ERRNO(err) IS_NOBLOCK_WRITE_ERRNO(err)
#define IS_NOBLOCK_RECV_ERRNO(err) IS_NOBLOCK_READ_ERRNO(err)

#define IS_NOBLOCK_WRITE_ERRNO(err) ((err) == SOCKET_ERRNO(EAGAIN) || (err) == SOCKET_ERRNO(EWOULDBLOCK))
#define IS_NOBLOCK_READ_ERRNO(err) ((err) == SOCKET_ERRNO(EAGAIN) || (err) == SOCKET_ERRNO(EWOULDBLOCK))

int socket_set_nobio(SOCKET fd);
int socket_set_tcp_mss(SOCKET sockfd, int size);
int socket_get_tcp_mss(SOCKET sockfd, int *size);
int socket_fix_tcp_mss(SOCKET sockfd); // make mss=mss-40
int socket_disable_nagle(SOCKET sock, int nagle);
int socket_error(SOCKET sock);
int socket_reuseaddr(SOCKET sock, int optval);
/*
 https://msdn.microsoft.com/zh-cn/library/windows/desktop/bb513665(v=vs.85).aspx
 Dual-Stack Sockets for IPv6 Winsock Applications
 By default, an IPv6 socket created on Windows Vista and later only operates over the IPv6 protocol. In order to make an IPv6 socket into a dual-stack socket, the setsockopt function must be called with the IPV6_V6ONLY socket option to set this value to zero before the socket is bound to an IP address. When the IPV6_V6ONLY socket option is set to zero, a socket created for the AF_INET6 address family can be used to send and receive packets to and from an IPv6 address or an IPv4 mapped address.
 */
int socket_ipv6only(SOCKET _sock, int _only);

int socket_isnonetwork(int error);

class SocketBreaker
{
public:
  SocketBreaker();
  ~SocketBreaker();

  bool IsCreateSuc() const;
  bool ReCreate();
  void Close();

  bool Break();
  bool Clear();

  bool IsBreak() const;
  int BreakerFD() const;

private:
  int pipes_[2];
  bool create_success_;
  bool broken_;
  Mutex mutex_;

  DISALLOW_COPY_AND_ASSIGN(SocketBreaker);
};

struct PollEvent
{
  friend class SocketPoll;

public:
  PollEvent();

  bool Readable() const;
  bool Writealbe() const;
  bool HangUp() const;
  bool Error() const;
  bool Invalid() const;

  void *UserData();
  SOCKET FD() const;

private:
  pollfd poll_event_;
  void *user_data_;
};

class SocketPoll
{
public:
  SocketPoll(SocketBreaker &_breaker, bool _autoclear = false);
  virtual ~SocketPoll();

  bool Consign(SocketPoll &_consignor, bool _recover = false);
  void AddEvent(SOCKET _fd, bool _read, bool _write, void *_user_data);
  void ReadEvent(SOCKET _fd, bool _active);
  void WriteEvent(SOCKET _fd, bool _active);
  void NullEvent(SOCKET _fd);
  void DelEvent(SOCKET _fd);
  void ClearEvent();

  virtual int Poll();
  virtual int Poll(int _msec);

  int Ret() const;
  int Errno() const;
  bool BreakerIsBreak() const;
  bool BreakerIsError() const;

  bool ConsignReport(SocketPoll &_consignor, int64_t _timeout) const;
  const std::vector<PollEvent> &TriggeredEvents() const;

  SocketBreaker &Breaker();

private:
  DISALLOW_COPY_AND_ASSIGN(SocketPoll);

protected:
  SocketBreaker &breaker_;
  const bool autoclear_;

  std::vector<pollfd> events_;
  std::map<SOCKET, void *> events_user_data_;
  std::vector<PollEvent> triggered_events_;

  int ret_;
  int errno_;
};

class SocketSelect
{
public:
  SocketSelect(SocketBreaker &_breaker, bool _autoclear = false);
  virtual ~SocketSelect();

  void PreSelect();
  void Read_FD_SET(SOCKET _socket);
  void Write_FD_SET(SOCKET _socket);
  void Exception_FD_SET(SOCKET _socket);

  virtual int Select();
  virtual int Select(int _msec);

  int Ret() const;
  int Errno() const;

  int Read_FD_ISSET(SOCKET _socket) const;
  int Write_FD_ISSET(SOCKET _socket) const;
  int Exception_FD_ISSET(SOCKET _socket) const;

  bool IsBreak() const;
  bool IsException() const;

  SocketBreaker &Breaker();

  SocketPoll &Poll();

private:
  SocketSelect(const SocketSelect &);
  SocketSelect &operator=(const SocketSelect &);

protected:
  SocketPoll socket_poll_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SOCKET_UTIL_H_
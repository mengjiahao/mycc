
#ifndef MYCC_UTIL_SOCKET_UTIL_H_
#define MYCC_UTIL_SOCKET_UTIL_H_

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <net/if.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <unistd.h>
#include "locks_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

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
  SocketBreaker(const SocketBreaker &);
  SocketBreaker &operator=(const SocketBreaker &);

private:
  int pipes_[2];
  bool create_success_;
  bool broken_;
  Mutex mutex_;
};

class PinkServerSocket
{
public:
  static int Setnonblocking(int sockfd);

  explicit PinkServerSocket(int port, bool is_block = false);
  virtual ~PinkServerSocket();

  /*
   * Listen to a specific ip addr on a multi eth machine
   * Return 0 if Listen success, <0 other wise
   */
  int Listen(const string &bind_ip = string());

  void Close();

  /*
   * The get and set functions
   */
  void set_port(int port)
  {
    port_ = port;
  }

  int port()
  {
    return port_;
  }

  void set_keep_alive(bool keep_alive)
  {
    keep_alive_ = keep_alive;
  }
  bool keep_alive() const
  {
    return keep_alive_;
  }

  void set_send_timeout(int send_timeout)
  {
    send_timeout_ = send_timeout;
  }
  int send_timeout() const
  {
    return send_timeout_;
  }

  void set_recv_timeout(int recv_timeout)
  {
    recv_timeout_ = recv_timeout;
  }

  int recv_timeout() const
  {
    return recv_timeout_;
  }

  int sockfd() const
  {
    return sockfd_;
  }

  void set_sockfd(int sockfd)
  {
    sockfd_ = sockfd;
  }

private:
  int SetNonBlock();
  /*
   * The tcp server port and address
   */
  int port_;
  int flags_;
  int send_timeout_;
  int recv_timeout_;
  int accept_timeout_;
  int accept_backlog_;
  int tcp_send_buffer_;
  int tcp_recv_buffer_;
  bool keep_alive_;
  bool listening_;
  bool is_block_;

  struct sockaddr_in servaddr_;
  int sockfd_;

  /*
   * No allowed copy and copy assign operator
   */

  PinkServerSocket(const PinkServerSocket &);
  void operator=(const PinkServerSocket &);
};

struct PinkFiredEvent
{
  int fd;
  int mask;
};

class PinkEpoll
{
public:
  static const int kPinkMaxClients = 10240;
  static const int PINK_MAX_CLIENTS = 10240;

  PinkEpoll();
  ~PinkEpoll();
  int PinkAddEvent(const int fd, const int mask);
  int PinkDelEvent(const int fd);
  int PinkModEvent(const int fd, const int old_mask, const int mask);

  int PinkPoll(const int timeout);

  PinkFiredEvent *firedevent() const { return firedevent_; }

private:
  int epfd_;
  struct epoll_event *events_;
  //int timeout_;
  PinkFiredEvent *firedevent_;
};

// Usage like:
// PinkCli* cli = NewPbCli();
// Status s = cli->Connect(ip, port);
// myproto::Ping msg; msg.set_address("127.00000"); msg.set_port(2222);
// s = cli->Send((void *)&msg);
// myproto::PingRes req;
// s = cli->Recv((void *)&req);
// cli->Close();
// DeletePinkCli(&cli);

class PinkCli
{
public:
  explicit PinkCli(const string &ip = "", const int port = 0);
  virtual ~PinkCli();

  bool Connect(const string &bind_ip = "");
  bool Connect(const string &peer_ip, const int peer_port,
               const string &bind_ip = "");
  // Compress and write the message
  virtual bool Send(void *msg) = 0;

  // Read, parse and store the reply
  virtual bool Recv(void *result = NULL) = 0;

  void Close();

  // TODO(baotiao): delete after redis_cli use RecvRaw
  int fd() const;

  bool Available() const;

  // default connect timeout is 1000ms
  int set_send_timeout(int send_timeout);
  int set_recv_timeout(int recv_timeout);
  void set_connect_timeout(int connect_timeout);

protected:
  bool SendRaw(void *buf, size_t len);
  bool RecvRaw(void *buf, size_t *len);

private:
  struct Rep;
  Rep *rep_;
  int set_tcp_nodelay();

  PinkCli(const PinkCli &);
  void operator=(const PinkCli &);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SOCKET_UTIL_H_
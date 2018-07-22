#include "socket_util.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mycc
{
namespace util
{

SocketBreaker::SocketBreaker()
    : create_success_(true),
      broken_(false)
{
  ReCreate();
}

SocketBreaker::~SocketBreaker()
{
  Close();
}

bool SocketBreaker::IsCreateSuc() const
{
  return create_success_;
}

bool SocketBreaker::ReCreate()
{
  pipes_[0] = -1;
  pipes_[1] = -1;

  int Ret;
  Ret = pipe(pipes_);
  assert(-1 != Ret);

  if (Ret == -1)
  {
    pipes_[0] = -1;
    pipes_[1] = -1;
    create_success_ = false;
    return create_success_;
  }

  long flags0 = fcntl(pipes_[0], F_GETFL, 0);
  long flags1 = fcntl(pipes_[1], F_GETFL, 0);

  if (flags0 < 0 || flags1 < 0)
  {
    //xerror2(TSF"get old flags error");
    close(pipes_[0]);
    close(pipes_[1]);
    pipes_[0] = -1;
    pipes_[1] = -1;
    create_success_ = false;
    return create_success_;
  }

  flags0 |= O_NONBLOCK;
  flags1 |= O_NONBLOCK;
  int ret0 = fcntl(pipes_[0], F_SETFL, flags0);
  int ret1 = fcntl(pipes_[1], F_SETFL, flags1);

  if ((-1 == ret1) || (-1 == ret0))
  {
    //xerror2(TSF"fcntl error");
    close(pipes_[0]);
    close(pipes_[1]);
    pipes_[0] = -1;
    pipes_[1] = -1;
    create_success_ = false;
    return create_success_;
  }

  create_success_ = true;
  return create_success_;
}

bool SocketBreaker::Break()
{
  MutexLock lock(&mutex_);

  if (broken_)
    return true;

  const char dummy = '1';
  int ret = (int)write(pipes_[1], &dummy, sizeof(dummy));
  broken_ = true;
  if (ret < 0 || ret != (int)sizeof(dummy))
  {
    //xerror2(TSF"Ret:%_, errno:(%_, %_)", ret, errno, strerror(errno));
    broken_ = false;
  }

  return broken_;
}

bool SocketBreaker::Clear()
{
  MutexLock lock(&mutex_);

  char dummy[128];
  int ret = (int)read(pipes_[0], dummy, sizeof(dummy));
  if (ret < 0)
  {
    //xverbose2(TSF"Ret=%0", ret);
    return false;
  }

  broken_ = false;
  return true;
}

void SocketBreaker::Close()
{
  broken_ = true;
  if (pipes_[1] >= 0)
    close(pipes_[1]);
  if (pipes_[0] >= 0)
    close(pipes_[0]);
}

int SocketBreaker::BreakerFD() const
{
  return pipes_[0];
}

bool SocketBreaker::IsBreak() const
{
  return broken_;
}

int PinkServerSocket::Setnonblocking(int sockfd)
{
  int flags;
  if ((flags = fcntl(sockfd, F_GETFL, 0)) < 0)
  {
    close(sockfd);
    return -1;
  }
  flags |= O_NONBLOCK;
  if (fcntl(sockfd, F_SETFL, flags) < 0)
  {
    close(sockfd);
    return -1;
  }
  return flags;
}

PinkServerSocket::PinkServerSocket(int port, bool is_block)
    : port_(port),
      send_timeout_(0),
      recv_timeout_(0),
      accept_timeout_(0),
      accept_backlog_(1024),
      tcp_send_buffer_(0),
      tcp_recv_buffer_(0),
      keep_alive_(false),
      listening_(false),
      is_block_(is_block)
{
}

PinkServerSocket::~PinkServerSocket()
{
  Close();
}

/*
 * Listen to a specific ip addr on a multi eth machine
 * Return 0 if Listen success, other wise
 */
int PinkServerSocket::Listen(const string &bind_ip)
{
  int ret = 0;
  sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
  memset(&servaddr_, 0, sizeof(servaddr_));

  int yes = 1;
  ret = setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  if (ret < 0)
  {
    return -1;
  }

  servaddr_.sin_family = AF_INET;
  if (bind_ip.empty())
  {
    servaddr_.sin_addr.s_addr = htonl(INADDR_ANY);
  }
  else
  {
    servaddr_.sin_addr.s_addr = inet_addr(bind_ip.c_str());
  }
  servaddr_.sin_port = htons(port_);

  fcntl(sockfd_, F_SETFD, fcntl(sockfd_, F_GETFD) | FD_CLOEXEC);

  ret = bind(sockfd_, (struct sockaddr *)&servaddr_, sizeof(servaddr_));
  if (ret < 0)
  {
    return -2;
  }
  ret = listen(sockfd_, accept_backlog_);
  if (ret < 0)
  {
    return -3;
  }
  listening_ = true;

  if (is_block_ == false)
  {
    SetNonBlock();
  }
  return 0;
}

int PinkServerSocket::SetNonBlock()
{
  flags_ = Setnonblocking(sockfd());
  if (flags_ == -1)
  {
    return -1;
  }
  return 0;
}

void PinkServerSocket::Close()
{
  close(sockfd_);
}

PinkEpoll::PinkEpoll()
{
  epfd_ = epoll_create(1024);
  fcntl(epfd_, F_SETFD, fcntl(epfd_, F_GETFD) | FD_CLOEXEC);

  if (epfd_ < 0)
  {
    perror("epoll create fail\n");
    exit(1);
  }
  events_ = (struct epoll_event *)malloc(
      sizeof(struct epoll_event) * kPinkMaxClients);

  firedevent_ = reinterpret_cast<PinkFiredEvent *>(malloc(
      sizeof(PinkFiredEvent) * kPinkMaxClients));
}

PinkEpoll::~PinkEpoll()
{
  free(firedevent_);
  free(events_);
  close(epfd_);
}

int PinkEpoll::PinkAddEvent(const int fd, const int mask)
{
  struct epoll_event ee;
  ee.data.fd = fd;
  ee.events = mask;
  return epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ee);
}

int PinkEpoll::PinkModEvent(const int fd, const int old_mask, const int mask)
{
  struct epoll_event ee;
  ee.data.fd = fd;
  ee.events = (old_mask | mask);
  return epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ee);
}

int PinkEpoll::PinkDelEvent(const int fd)
{
  /*
   * Kernel < 2.6.9 need a non null event point to EPOLL_CTL_DEL
   */
  struct epoll_event ee;
  ee.data.fd = fd;
  return epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, &ee);
}

int PinkEpoll::PinkPoll(const int timeout)
{
  int retval, numevents = 0;
  retval = epoll_wait(epfd_, events_, PINK_MAX_CLIENTS, timeout);
  if (retval > 0)
  {
    numevents = retval;
    for (int i = 0; i < numevents; i++)
    {
      int mask = 0;
      firedevent_[i].fd = (events_ + i)->data.fd;

      if ((events_ + i)->events & EPOLLIN)
      {
        mask |= EPOLLIN;
      }
      if ((events_ + i)->events & EPOLLOUT)
      {
        mask |= EPOLLOUT;
      }
      if ((events_ + i)->events & EPOLLERR)
      {
        mask |= EPOLLERR;
      }
      if ((events_ + i)->events & EPOLLHUP)
      {
        mask |= EPOLLHUP;
      }
      firedevent_[i].mask = mask;
    }
  }
  return numevents;
}

struct PinkCli::Rep
{
  string peer_ip;
  int peer_port;
  int send_timeout;
  int recv_timeout;
  int connect_timeout;
  bool keep_alive;
  bool is_block;
  int sockfd;
  bool available;

  Rep() : send_timeout(0),
          recv_timeout(0),
          connect_timeout(1000),
          keep_alive(0),
          is_block(true),
          available(false)
  {
  }

  Rep(const string &ip, int port)
      : peer_ip(ip),
        peer_port(port),
        send_timeout(0),
        recv_timeout(0),
        connect_timeout(1000),
        keep_alive(0),
        is_block(true),
        available(false)
  {
  }
};

PinkCli::PinkCli(const string &ip, const int port)
    : rep_(new Rep(ip, port))
{
}

PinkCli::~PinkCli()
{
  Close();
  delete rep_;
}

bool PinkCli::Available() const
{
  return rep_->available;
}

bool PinkCli::Connect(const string &bind_ip)
{
  return Connect(rep_->peer_ip, rep_->peer_port, bind_ip);
}

bool PinkCli::Connect(const string &ip, const int port,
                      const string &bind_ip)
{
  Rep *r = rep_;
  int rv;
  char cport[6];
  struct addrinfo hints, *servinfo, *p;
  snprintf(cport, sizeof(cport), "%d", port);
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  // We do not handle IPv6
  if ((rv = getaddrinfo(ip.c_str(), cport, &hints, &servinfo)) != 0)
  {
    //return Status::IOError("connect getaddrinfo error for ", ip);
    return false;
  }
  for (p = servinfo; p != NULL; p = p->ai_next)
  {
    if ((r->sockfd = socket(
             p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
    {
      continue;
    }

    // bind if needed
    if (!bind_ip.empty())
    {
      struct sockaddr_in localaddr;
      localaddr.sin_family = AF_INET;
      localaddr.sin_addr.s_addr = inet_addr(bind_ip.c_str());
      localaddr.sin_port = 0; // Any local port will do
      bind(r->sockfd, (struct sockaddr *)&localaddr, sizeof(localaddr));
    }

    int flags = fcntl(r->sockfd, F_GETFL, 0);
    fcntl(r->sockfd, F_SETFL, flags | O_NONBLOCK);

    if (connect(r->sockfd, p->ai_addr, p->ai_addrlen) == -1)
    {
      if (errno == EHOSTUNREACH)
      {
        close(r->sockfd);
        continue;
      }
      else if (errno == EINPROGRESS ||
               errno == EAGAIN ||
               errno == EWOULDBLOCK)
      {
        struct pollfd wfd[1];

        wfd[0].fd = r->sockfd;
        wfd[0].events = POLLOUT;

        int res;
        if ((res = poll(wfd, 1, r->connect_timeout)) == -1)
        {
          close(r->sockfd);
          freeaddrinfo(servinfo);
          //return Status::IOError("EHOSTUNREACH", "connect poll error");
          return false;
        }
        else if (res == 0)
        {
          close(r->sockfd);
          freeaddrinfo(servinfo);
          return false;
        }
        int val = 0;
        socklen_t lon = sizeof(int);

        if (getsockopt(r->sockfd, SOL_SOCKET, SO_ERROR, &val, &lon) == -1)
        {
          close(r->sockfd);
          freeaddrinfo(servinfo);
          //return Status::IOError("EHOSTUNREACH", "connect host getsockopt error");
          return false;
        }

        if (val)
        {
          close(r->sockfd);
          freeaddrinfo(servinfo);
          //return Status::IOError("EHOSTUNREACH", "connect host error");
          return false;
        }
      }
      else
      {
        close(r->sockfd);
        freeaddrinfo(servinfo);
        //return Status::IOError("EHOSTUNREACH", "The target host cannot be reached");
        return false;
      }
    }

    struct sockaddr_in laddr;
    socklen_t llen = sizeof(laddr);
    getsockname(r->sockfd, (struct sockaddr *)&laddr, &llen);
    string lip(inet_ntoa(laddr.sin_addr));
    int lport = ntohs(laddr.sin_port);
    if (ip == lip && port == lport)
    {
      //return Status::IOError("EHOSTUNREACH", "same ip port");
      return false;
    }

    flags = fcntl(r->sockfd, F_GETFL, 0);
    fcntl(r->sockfd, F_SETFL, flags & ~O_NONBLOCK);
    freeaddrinfo(servinfo);

    // connect ok
    rep_->available = true;
    return true;
  }
  if (p == NULL)
  {
    //s = Status::IOError(strerror(errno), "Can't create socket ");
    return false;
  }
  freeaddrinfo(servinfo);
  freeaddrinfo(p);
  set_tcp_nodelay();
  return true;
}

bool PinkCli::SendRaw(void *buf, size_t count)
{
  char *wbuf = reinterpret_cast<char *>(buf);
  size_t nleft = count;
  int pos = 0;
  ssize_t nwritten;

  while (nleft > 0)
  {
    if ((nwritten = write(rep_->sockfd, wbuf + pos, nleft)) < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      else if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        //return Status::Timeout("Send timeout");
        return false;
      }
      else
      {
        //return Status::IOError("write error " + string(strerror(errno)));
        return false;
      }
    }
    else if (nwritten == 0)
    {
      //return Status::IOError("write nothing");
      return false;
    }

    nleft -= nwritten;
    pos += nwritten;
  }

  return true;
}

bool PinkCli::RecvRaw(void *buf, size_t *count)
{
  Rep *r = rep_;
  char *rbuf = reinterpret_cast<char *>(buf);
  size_t nleft = *count;
  size_t pos = 0;
  ssize_t nread;

  while (nleft > 0)
  {
    if ((nread = read(r->sockfd, rbuf + pos, nleft)) < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      else if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        //return Status::Timeout("Send timeout");
        return false;
      }
      else
      {
        //return Status::IOError("read error " + string(strerror(errno)));
        return false;
      }
    }
    else if (nread == 0)
    {
      //return Status::EndFile("socket closed");
      return false;
    }
    nleft -= nread;
    pos += nread;
  }

  *count = pos;
  return true;
}

int PinkCli::fd() const
{
  return rep_->sockfd;
}

void PinkCli::Close()
{
  if (rep_->available)
  {
    close(rep_->sockfd);
    rep_->available = false;
  }
}

void PinkCli::set_connect_timeout(int connect_timeout)
{
  rep_->connect_timeout = connect_timeout;
}

int PinkCli::set_send_timeout(int send_timeout)
{
  Rep *r = rep_;
  int ret = 0;
  if (send_timeout > 0)
  {
    r->send_timeout = send_timeout;
    struct timeval timeout =
        {r->send_timeout / 1000, (r->send_timeout % 1000) * 1000};
    ret = setsockopt(
        r->sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  }
  return ret;
}

int PinkCli::set_recv_timeout(int recv_timeout)
{
  Rep *r = rep_;
  int ret = 0;
  if (recv_timeout > 0)
  {
    r->recv_timeout = recv_timeout;
    struct timeval timeout =
        {r->recv_timeout / 1000, (r->recv_timeout % 1000) * 1000};
    ret = setsockopt(
        r->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  }
  return ret;
}

int PinkCli::set_tcp_nodelay()
{
  Rep *r = rep_;
  int val = 1;
  int ret = 0;
  ret = setsockopt(r->sockfd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
  return ret;
}

void DeletePinkCli(PinkCli **cli)
{
  delete (*cli);
  *cli = nullptr;
}

} // namespace util
} // namespace mycc
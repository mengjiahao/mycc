#include "socket_util.h"
#include <fcntl.h>
#include <algorithm>

namespace mycc
{
namespace util
{

int socket_set_nobio(SOCKET fd)
{
  int ret = fcntl(fd, F_GETFL, 0);
  if (ret >= 0)
  {
    long flags = ret | O_NONBLOCK;
    ret = fcntl(fd, F_SETFL, flags);
  }

  return ret;
}

int socket_set_tcp_mss(SOCKET sockfd, int size)
{
  return setsockopt(sockfd, IPPROTO_TCP, TCP_MAXSEG, &size, sizeof(size));
}

int socket_get_tcp_mss(SOCKET sockfd, int *size)
{
  if (size == NULL)
    return -1;
  socklen_t len = sizeof(int);
  return getsockopt(sockfd, IPPROTO_TCP, TCP_MAXSEG, (void *)size, &len);
}

int socket_fix_tcp_mss(SOCKET sockfd)
{
  return socket_set_tcp_mss(sockfd, 1400);
}

int socket_disable_nagle(SOCKET sock, int nagle)
{
  return setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&nagle, sizeof(nagle));
}

int socket_error(SOCKET sock)
{
  int error = 0;
  socklen_t len = sizeof(error);
  if (0 != getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len))
  {
    error = socket_errno;
  }
  return error;
}

int socket_isnonetwork(int error)
{
  if (error == SOCKET_ERRNO(ENETDOWN))
    return 1;
  if (error == SOCKET_ERRNO(ENETUNREACH))
    return 1;

  return 0;
}

int socket_reuseaddr(SOCKET sock, int optval)
{
  return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                    (const char *)&optval, sizeof(int));
}

int socket_ipv6only(SOCKET _sock, int _only)
{
  return setsockopt(_sock, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&_only, sizeof(_only));
}

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

PollEvent::PollEvent() : poll_event_({0}), user_data_(NULL) {}
bool PollEvent::Readable() const { return poll_event_.revents & POLLIN; }
bool PollEvent::Writealbe() const { return poll_event_.revents & POLLOUT; }
bool PollEvent::HangUp() const { return poll_event_.revents & POLLHUP; }
bool PollEvent::Error() const { return poll_event_.revents & POLLERR; }
bool PollEvent::Invalid() const { return poll_event_.revents & POLLNVAL; }
void *PollEvent::UserData() { return user_data_; }
SOCKET PollEvent::FD() const { return poll_event_.fd; }

SocketPoll::SocketPoll(SocketBreaker &_breaker, bool _autoclear)
    : breaker_(_breaker), autoclear_(_autoclear), ret_(0), errno_(0)
{
  events_.push_back({breaker_.BreakerFD(), POLLIN, 0});
}

SocketPoll::~SocketPoll() {}

bool SocketPoll::Consign(SocketPoll &_consignor, bool _recover)
{
  auto it = std::find_if(events_.begin(), events_.end(), [&_consignor](const pollfd &_v) { return _v.fd == _consignor.events_[0].fd; });

  if (_recover)
  {
    if (it == events_.end())
      return false;
    //xassert2(it->events == _consignor.events_[0].events, TSF"%_ != %_", it->events, _consignor.events_[0].events);
    events_.erase(it, it + _consignor.events_.size());
  }
  else
  {
    //xassert2(it == events_.end());
    if (it != events_.end())
      return false;
    events_.insert(events_.end(), _consignor.events_.begin(), _consignor.events_.end());
  }

  return true;
}

void SocketPoll::AddEvent(SOCKET _fd, bool _read, bool _write, void *_user_data)
{

  auto it = std::find_if(events_.begin(), events_.end(), [&_fd](const pollfd &_v) { return _v.fd == _fd; });
  pollfd add_event = {_fd, static_cast<short>((_read ? POLLIN : 0) | (_write ? POLLOUT : 0)), 0};
  if (it == events_.end())
  {
    events_.push_back(add_event);
  }
  else
  {
    *it = add_event;
  }
  events_user_data_[_fd] = _user_data;
}

void SocketPoll::ReadEvent(SOCKET _fd, bool _active)
{
  auto find_it = std::find_if(events_.begin(), events_.end(), [&_fd](const pollfd &_v) { return _v.fd == _fd; });
  if (find_it == events_.end())
  {
    AddEvent(_fd, _active ? true : false, false, NULL);
    return;
  }

  if (_active)
    find_it->events |= POLLIN;
  else
    find_it->events &= ~POLLIN;
}

void SocketPoll::WriteEvent(SOCKET _fd, bool _active)
{
  auto find_it = std::find_if(events_.begin(), events_.end(), [&_fd](const pollfd &_v) { return _v.fd == _fd; });
  if (find_it == events_.end())
  {
    AddEvent(_fd, false, _active ? true : false, NULL);
    return;
  }

  if (_active)
    find_it->events |= POLLOUT;
  else
    find_it->events &= ~POLLOUT;
}

void SocketPoll::NullEvent(SOCKET _fd)
{
  auto find_it = std::find_if(events_.begin(), events_.end(), [&_fd](const pollfd &_v) { return _v.fd == _fd; });
  if (find_it == events_.end())
  {
    AddEvent(_fd, false, false, NULL);
  }
}

void SocketPoll::DelEvent(SOCKET _fd)
{
  auto find_it = std::find_if(events_.begin(), events_.end(), [&_fd](const pollfd &_v) { return _v.fd == _fd; });
  if (find_it != events_.end())
    events_.erase(find_it);
  events_user_data_.erase(_fd);
}

void SocketPoll::ClearEvent()
{
  events_.erase(events_.begin() + 1, events_.end());
  events_user_data_.clear();
}

int SocketPoll::Poll() { return Poll(-1); }

int SocketPoll::Poll(int _msec)
{
  assert(-1 <= _msec);
  if (-1 > _msec)
    _msec = 0;

  triggered_events_.clear();
  errno_ = 0;
  ret_ = 0;
  for (auto &i : events_)
  {
    i.revents = 0;
  }

  ret_ = ::poll(&events_[0], (nfds_t)events_.size(), _msec);

  do
  {
    if (0 > ret_)
    {
      errno_ = errno;
      break;
    }

    if (0 == ret_)
    {
      break;
    }

    for (size_t i = 1; i < events_.size(); ++i)
    {
      if (0 == events_[i].revents)
        continue;

      PollEvent traggered_event;
      traggered_event.poll_event_ = events_[i];
      traggered_event.user_data_ = events_user_data_[events_[i].fd];

      triggered_events_.push_back(traggered_event);
    }
  } while (false);

  if (autoclear_)
    Breaker().Clear();
  return ret_;
}

int SocketPoll::Ret() const { return ret_; }
int SocketPoll::Errno() const { return errno_; }

bool SocketPoll::BreakerIsError() const
{
  PollEvent logic_event;
  logic_event.poll_event_ = events_[0];
  return logic_event.Invalid() || logic_event.Error();
}

bool SocketPoll::BreakerIsBreak() const
{
  PollEvent logic_event;
  logic_event.poll_event_ = events_[0];
  ;
  return logic_event.Readable();
}

bool SocketPoll::ConsignReport(SocketPoll &_consignor, int64_t _timeout) const
{
  int32_t triggered_event_count = 0;
  auto find_it = std::find_if(events_.begin(), events_.end(), [&_consignor](const pollfd _v) { return _v.fd == _consignor.events_[0].fd; });

  assert(find_it != events_.end());
  assert(events_.end() - find_it >= (int)_consignor.events_.size());

  if (find_it == events_.end())
    return false;

  for (auto &i : _consignor.events_)
  {
    //xassert2(i.fd == find_it->fd && i.events == find_it->events,
    //         TSF"i(%_, %_), find_it(%_, %_)", i.fd, i.events, find_it->fd, find_it->events);
    if (0 != find_it->revents)
    {
      i.revents = find_it->revents;
      ++triggered_event_count;

      if (i.fd == _consignor.events_[0].fd)
      {
        assert(&i == &(_consignor.events_[0]));
        continue;
      }

      PollEvent traggered_event;
      traggered_event.poll_event_ = i;
      traggered_event.user_data_ = _consignor.events_user_data_[i.fd];

      _consignor.triggered_events_.push_back(traggered_event);
    }
    ++find_it;
  }

  if (0 > ret_)
  {
    _consignor.ret_ = ret_;
    _consignor.errno_ = errno_;
    if (_consignor.autoclear_)
      _consignor.Breaker().Clear();
    return true;
  }

  if (0 < triggered_event_count)
  {
    _consignor.ret_ = triggered_event_count;
    _consignor.errno_ = 0;
    if (_consignor.autoclear_)
      _consignor.Breaker().Clear();
    return true;
  }

  if (0 >= _timeout)
  {
    _consignor.ret_ = 0;
    _consignor.errno_ = errno_;
    if (_consignor.autoclear_)
      _consignor.Breaker().Clear();
    return true;
  }

  return false;
}

const std::vector<PollEvent> &SocketPoll::TriggeredEvents() const
{
  return triggered_events_;
}

SocketBreaker &SocketPoll::Breaker()
{
  return breaker_;
}

SocketSelect::SocketSelect(SocketBreaker &_breaker, bool _autoclear)
    : socket_poll_(_breaker, _autoclear)
{
}

SocketSelect::~SocketSelect() {}

void SocketSelect::PreSelect() { socket_poll_.ClearEvent(); }
int SocketSelect::Select() { return Select(-1); }
int SocketSelect::Select(int _msec) { return socket_poll_.Poll(_msec); }

void SocketSelect::Read_FD_SET(SOCKET _socket) { socket_poll_.ReadEvent(_socket, true); }
void SocketSelect::Write_FD_SET(SOCKET _socket) { socket_poll_.WriteEvent(_socket, true); }
void SocketSelect::Exception_FD_SET(SOCKET _socket) { socket_poll_.NullEvent(_socket); }

int SocketSelect::Read_FD_ISSET(SOCKET _socket) const
{
  const std::vector<PollEvent> &events = socket_poll_.TriggeredEvents();
  auto find_it = std::find_if(events.begin(), events.end(), [_socket](const PollEvent &_v) { return _v.FD() == _socket; });
  if (find_it == events.end())
    return 0;
  return find_it->Readable() || find_it->HangUp();
}

int SocketSelect::Write_FD_ISSET(SOCKET _socket) const
{
  const std::vector<PollEvent> &events = socket_poll_.TriggeredEvents();
  auto find_it = std::find_if(events.begin(), events.end(), [_socket](const PollEvent &_v) { return _v.FD() == _socket; });
  if (find_it == events.end())
  {
    return 0;
  }
  return find_it->Writealbe();
}

int SocketSelect::Exception_FD_ISSET(SOCKET _socket) const
{
  const std::vector<PollEvent> &events = socket_poll_.TriggeredEvents();
  auto find_it = std::find_if(events.begin(), events.end(), [_socket](const PollEvent &_v) { return _v.FD() == _socket; });
  if (find_it == events.end())
    return 0;
  return find_it->Error() || find_it->Invalid();
}

int SocketSelect::Ret() const { return socket_poll_.Ret(); }
int SocketSelect::Errno() const { return socket_poll_.Errno(); }
bool SocketSelect::IsException() const { return socket_poll_.BreakerIsError(); }
bool SocketSelect::IsBreak() const { return socket_poll_.BreakerIsBreak(); }

SocketBreaker &SocketSelect::Breaker() { return socket_poll_.Breaker(); }
SocketPoll &SocketSelect::Poll() { return socket_poll_; }

} // namespace util
} // namespace mycc

#include "ae_util.h"
#include <errno.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace mycc
{
namespace util
{

#ifndef zmalloc
#define zmalloc malloc
#endif
#ifndef zfree
#define zfree free
#endif
#ifndef zrealloc
#define zrealloc realloc
#endif

// Include the best multiplexing layer supported by this system

/////////////////////////// ae_select /////////////////////////////

// typedef struct aeApiState
// {
//   fd_set rfds, wfds;
//   /* We need to have a copy of the fd sets as it's not safe to reuse
//      * FD sets after select(). */
//   fd_set _rfds, _wfds;
// } aeApiState;

// static int aeApiCreate(aeEventLoop *eventLoop)
// {
//   aeApiState *state = (aeApiState *)zmalloc(sizeof(aeApiState));

//   if (!state)
//     return -1;
//   FD_ZERO(&state->rfds);
//   FD_ZERO(&state->wfds);
//   eventLoop->apidata = state;
//   return 0;
// }

// static int aeApiResize(aeEventLoop *eventLoop, int setsize)
// {
//   /* Just ensure we have enough room in the fd_set type. */
//   if (setsize >= FD_SETSIZE)
//     return -1;
//   return 0;
// }

// static void aeApiFree(aeEventLoop *eventLoop)
// {
//   zfree(eventLoop->apidata);
// }

// static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask)
// {
//   aeApiState *state = (aeApiState *)(eventLoop->apidata);

//   if (mask & AE_READABLE)
//     FD_SET(fd, &state->rfds);
//   if (mask & AE_WRITABLE)
//     FD_SET(fd, &state->wfds);
//   return 0;
// }

// static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask)
// {
//   aeApiState *state = (aeApiState *)(eventLoop->apidata);

//   if (mask & AE_READABLE)
//     FD_CLR(fd, &state->rfds);
//   if (mask & AE_WRITABLE)
//     FD_CLR(fd, &state->wfds);
// }

// static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp)
// {
//   aeApiState *state = (aeApiState *)(eventLoop->apidata);
//   int retval, j, numevents = 0;

//   memcpy(&state->_rfds, &state->rfds, sizeof(fd_set));
//   memcpy(&state->_wfds, &state->wfds, sizeof(fd_set));

//   retval = select(eventLoop->maxfd + 1,
//                   &state->_rfds, &state->_wfds, NULL, tvp);
//   if (retval > 0)
//   {
//     for (j = 0; j <= eventLoop->maxfd; j++)
//     {
//       int mask = 0;
//       aeFileEvent *fe = &eventLoop->events[j];

//       if (fe->mask == AE_NONE)
//         continue;
//       if (fe->mask & AE_READABLE && FD_ISSET(j, &state->_rfds))
//         mask |= AE_READABLE;
//       if (fe->mask & AE_WRITABLE && FD_ISSET(j, &state->_wfds))
//         mask |= AE_WRITABLE;
//       eventLoop->fired[numevents].fd = j;
//       eventLoop->fired[numevents].mask = mask;
//       numevents++;
//     }
//   }
//   return numevents;
// }

// static const char *aeApiName(void)
// {
//   return "select";
// }

////////////////////////// ae_epoll /////////////////////////////////

typedef struct aeApiState
{
  int epfd;
  struct epoll_event *events;
} aeApiState;

static int aeApiCreate(aeEventLoop *eventLoop)
{
  aeApiState *state = (aeApiState *)zmalloc(sizeof(aeApiState));

  if (!state)
    return -1;
  state->events = (epoll_event *)zmalloc(sizeof(struct epoll_event) * eventLoop->setsize);
  if (!state->events)
  {
    zfree(state);
    return -1;
  }
  state->epfd = epoll_create(1024); /* 1024 is just a hint for the kernel */
  if (state->epfd == -1)
  {
    zfree(state->events);
    zfree(state);
    return -1;
  }
  eventLoop->apidata = state;
  return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize)
{
  aeApiState *state = (aeApiState *)(eventLoop->apidata);

  state->events = (epoll_event *)zrealloc(state->events, sizeof(struct epoll_event) * setsize);
  return 0;
}

static void aeApiFree(aeEventLoop *eventLoop)
{
  aeApiState *state = (aeApiState *)(eventLoop->apidata);

  close(state->epfd);
  zfree(state->events);
  zfree(state);
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask)
{
  aeApiState *state = (aeApiState *)eventLoop->apidata;
  struct epoll_event ee = {0}; /* avoid valgrind warning */
  /* If the fd was already monitored for some event, we need a MOD
     * operation. Otherwise we need an ADD operation. */
  int op = eventLoop->events[fd].mask == AE_NONE ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

  ee.events = 0;
  mask |= eventLoop->events[fd].mask; /* Merge old events */
  if (mask & AE_READABLE)
    ee.events |= EPOLLIN;
  if (mask & AE_WRITABLE)
    ee.events |= EPOLLOUT;
  ee.data.fd = fd;
  if (epoll_ctl(state->epfd, op, fd, &ee) == -1)
    return -1;
  return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask)
{
  aeApiState *state = (aeApiState *)(eventLoop->apidata);
  struct epoll_event ee = {0}; /* avoid valgrind warning */
  int mask = eventLoop->events[fd].mask & (~delmask);

  ee.events = 0;
  if (mask & AE_READABLE)
    ee.events |= EPOLLIN;
  if (mask & AE_WRITABLE)
    ee.events |= EPOLLOUT;
  ee.data.fd = fd;
  if (mask != AE_NONE)
  {
    epoll_ctl(state->epfd, EPOLL_CTL_MOD, fd, &ee);
  }
  else
  {
    /* Note, Kernel < 2.6.9 requires a non null event pointer even for
         * EPOLL_CTL_DEL. */
    epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, &ee);
  }
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp)
{
  aeApiState *state = (aeApiState *)(eventLoop->apidata);
  int retval, numevents = 0;

  retval = epoll_wait(state->epfd, state->events, eventLoop->setsize,
                      tvp ? (tvp->tv_sec * 1000 + tvp->tv_usec / 1000) : -1);
  if (retval > 0)
  {
    int j;

    numevents = retval;
    for (j = 0; j < numevents; j++)
    {
      int mask = 0;
      struct epoll_event *e = state->events + j;

      if (e->events & EPOLLIN)
        mask |= AE_READABLE;
      if (e->events & EPOLLOUT)
        mask |= AE_WRITABLE;
      if (e->events & EPOLLERR)
        mask |= AE_WRITABLE;
      if (e->events & EPOLLHUP)
        mask |= AE_WRITABLE;
      eventLoop->fired[j].fd = e->data.fd;
      eventLoop->fired[j].mask = mask;
    }
  }
  return numevents;
}

static const char *aeApiName(void)
{
  return "epoll";
}

/////////////////////////////// ae /////////////////////////////

aeEventLoop *aeCreateEventLoop(int setsize)
{
  aeEventLoop *eventLoop;
  int i;

  if ((eventLoop = (aeEventLoop *)zmalloc(sizeof(*eventLoop))) == NULL)
    goto err;
  eventLoop->events = (aeFileEvent *)zmalloc(sizeof(aeFileEvent) * setsize);
  eventLoop->fired = (aeFiredEvent *)zmalloc(sizeof(aeFiredEvent) * setsize);
  if (eventLoop->events == NULL || eventLoop->fired == NULL)
    goto err;
  eventLoop->setsize = setsize;
  eventLoop->lastTime = time(NULL);
  eventLoop->timeEventHead = NULL;
  eventLoop->timeEventNextId = 0;
  eventLoop->stop = 0;
  eventLoop->maxfd = -1;
  eventLoop->beforesleep = NULL;
  if (aeApiCreate(eventLoop) == -1)
    goto err;
  /* Events with mask == AE_NONE are not set. So let's initialize the
     * vector with it. */
  for (i = 0; i < setsize; i++)
    eventLoop->events[i].mask = AE_NONE;
  return eventLoop;

err:
  if (eventLoop)
  {
    zfree(eventLoop->events);
    zfree(eventLoop->fired);
    zfree(eventLoop);
  }
  return NULL;
}

/* Return the current set size. */
int aeGetSetSize(aeEventLoop *eventLoop)
{
  return eventLoop->setsize;
}

/* Resize the maximum set size of the event loop.
 * If the requested set size is smaller than the current set size, but
 * there is already a file descriptor in use that is >= the requested
 * set size minus one, AE_ERR is returned and the operation is not
 * performed at all.
 *
 * Otherwise AE_OK is returned and the operation is successful. */
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize)
{
  int i;

  if (setsize == eventLoop->setsize)
    return AE_OK;
  if (eventLoop->maxfd >= setsize)
    return AE_ERR;
  if (aeApiResize(eventLoop, setsize) == -1)
    return AE_ERR;

  eventLoop->events = (aeFileEvent *)zrealloc(eventLoop->events, sizeof(aeFileEvent) * setsize);
  eventLoop->fired = (aeFiredEvent *)zrealloc(eventLoop->fired, sizeof(aeFiredEvent) * setsize);
  eventLoop->setsize = setsize;

  /* Make sure that if we created new slots, they are initialized with
     * an AE_NONE mask. */
  for (i = eventLoop->maxfd + 1; i < setsize; i++)
    eventLoop->events[i].mask = AE_NONE;
  return AE_OK;
}

void aeDeleteEventLoop(aeEventLoop *eventLoop)
{
  aeApiFree(eventLoop);
  zfree(eventLoop->events);
  zfree(eventLoop->fired);
  zfree(eventLoop);
}

void aeStop(aeEventLoop *eventLoop)
{
  eventLoop->stop = 1;
}

int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
                      aeFileProc *proc, void *clientData)
{
  if (fd >= eventLoop->setsize)
  {
    errno = ERANGE;
    return AE_ERR;
  }
  aeFileEvent *fe = &eventLoop->events[fd];

  if (aeApiAddEvent(eventLoop, fd, mask) == -1)
    return AE_ERR;
  fe->mask |= mask;
  if (mask & AE_READABLE)
    fe->rfileProc = proc;
  if (mask & AE_WRITABLE)
    fe->wfileProc = proc;
  fe->clientData = clientData;
  if (fd > eventLoop->maxfd)
    eventLoop->maxfd = fd;
  return AE_OK;
}

void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
  if (fd >= eventLoop->setsize)
    return;
  aeFileEvent *fe = &eventLoop->events[fd];
  if (fe->mask == AE_NONE)
    return;

  aeApiDelEvent(eventLoop, fd, mask);
  fe->mask = fe->mask & (~mask);
  if (fd == eventLoop->maxfd && fe->mask == AE_NONE)
  {
    /* Update the max fd */
    int j;

    for (j = eventLoop->maxfd - 1; j >= 0; j--)
      if (eventLoop->events[j].mask != AE_NONE)
        break;
    eventLoop->maxfd = j;
  }
}

int aeGetFileEvents(aeEventLoop *eventLoop, int fd)
{
  if (fd >= eventLoop->setsize)
    return 0;
  aeFileEvent *fe = &eventLoop->events[fd];

  return fe->mask;
}

static void aeGetTime(long *seconds, long *milliseconds)
{
  struct timeval tv;

  gettimeofday(&tv, NULL);
  *seconds = tv.tv_sec;
  *milliseconds = tv.tv_usec / 1000;
}

static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms)
{
  long cur_sec, cur_ms, when_sec, when_ms;

  aeGetTime(&cur_sec, &cur_ms);
  when_sec = cur_sec + milliseconds / 1000;
  when_ms = cur_ms + milliseconds % 1000;
  if (when_ms >= 1000)
  {
    when_sec++;
    when_ms -= 1000;
  }
  *sec = when_sec;
  *ms = when_ms;
}

long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
                            aeTimeProc *proc, void *clientData,
                            aeEventFinalizerProc *finalizerProc)
{
  long long id = eventLoop->timeEventNextId++;
  aeTimeEvent *te;

  te = (aeTimeEvent *)zmalloc(sizeof(*te));
  if (te == NULL)
    return AE_ERR;
  te->id = id;
  aeAddMillisecondsToNow(milliseconds, &te->when_sec, &te->when_ms);
  te->timeProc = proc;
  te->finalizerProc = finalizerProc;
  te->clientData = clientData;
  te->next = eventLoop->timeEventHead;
  eventLoop->timeEventHead = te;
  return id;
}

int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id)
{
  aeTimeEvent *te = eventLoop->timeEventHead;
  while (te)
  {
    if (te->id == id)
    {
      te->id = AE_DELETED_EVENT_ID;
      return AE_OK;
    }
    te = te->next;
  }
  return AE_ERR; /* NO event with the specified ID found */
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */
static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop)
{
  aeTimeEvent *te = eventLoop->timeEventHead;
  aeTimeEvent *nearest = NULL;

  while (te)
  {
    if (!nearest || te->when_sec < nearest->when_sec ||
        (te->when_sec == nearest->when_sec &&
         te->when_ms < nearest->when_ms))
      nearest = te;
    te = te->next;
  }
  return nearest;
}

/* Process time events */
static int processTimeEvents(aeEventLoop *eventLoop)
{
  int processed = 0;
  aeTimeEvent *te, *prev;
  long long maxId;
  time_t now = time(NULL);

  /* If the system clock is moved to the future, and then set back to the
     * right value, time events may be delayed in a random way. Often this
     * means that scheduled operations will not be performed soon enough.
     *
     * Here we try to detect system clock skews, and force all the time
     * events to be processed ASAP when this happens: the idea is that
     * processing events earlier is less dangerous than delaying them
     * indefinitely, and practice suggests it is. */
  if (now < eventLoop->lastTime)
  {
    te = eventLoop->timeEventHead;
    while (te)
    {
      te->when_sec = 0;
      te = te->next;
    }
  }
  eventLoop->lastTime = now;

  prev = NULL;
  te = eventLoop->timeEventHead;
  maxId = eventLoop->timeEventNextId - 1;
  while (te)
  {
    long now_sec, now_ms;
    long long id;

    /* Remove events scheduled for deletion. */
    if (te->id == AE_DELETED_EVENT_ID)
    {
      aeTimeEvent *next = te->next;
      if (prev == NULL)
        eventLoop->timeEventHead = te->next;
      else
        prev->next = te->next;
      if (te->finalizerProc)
        te->finalizerProc(eventLoop, te->clientData);
      zfree(te);
      te = next;
      continue;
    }

    /* Make sure we don't process time events created by time events in
         * this iteration. Note that this check is currently useless: we always
         * add new timers on the head, however if we change the implementation
         * detail, this check may be useful again: we keep it here for future
         * defense. */
    if (te->id > maxId)
    {
      te = te->next;
      continue;
    }
    aeGetTime(&now_sec, &now_ms);
    if (now_sec > te->when_sec ||
        (now_sec == te->when_sec && now_ms >= te->when_ms))
    {
      int retval;

      id = te->id;
      retval = te->timeProc(eventLoop, id, te->clientData);
      processed++;
      if (retval != AE_NOMORE)
      {
        aeAddMillisecondsToNow(retval, &te->when_sec, &te->when_ms);
      }
      else
      {
        te->id = AE_DELETED_EVENT_ID;
      }
    }
    prev = te;
    te = te->next;
  }
  return processed;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 *
 * The function returns the number of events processed. */
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
  int processed = 0, numevents;

  /* Nothing to do? return ASAP */
  if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS))
    return 0;

  /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
  if (eventLoop->maxfd != -1 ||
      ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT)))
  {
    int j;
    aeTimeEvent *shortest = NULL;
    struct timeval tv, *tvp;

    if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
      shortest = aeSearchNearestTimer(eventLoop);
    if (shortest)
    {
      long now_sec, now_ms;

      aeGetTime(&now_sec, &now_ms);
      tvp = &tv;

      /* How many milliseconds we need to wait for the next
             * time event to fire? */
      long long ms =
          (shortest->when_sec - now_sec) * 1000 +
          shortest->when_ms - now_ms;

      if (ms > 0)
      {
        tvp->tv_sec = ms / 1000;
        tvp->tv_usec = (ms % 1000) * 1000;
      }
      else
      {
        tvp->tv_sec = 0;
        tvp->tv_usec = 0;
      }
    }
    else
    {
      /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to set the timeout
             * to zero */
      if (flags & AE_DONT_WAIT)
      {
        tv.tv_sec = tv.tv_usec = 0;
        tvp = &tv;
      }
      else
      {
        /* Otherwise we can block */
        tvp = NULL; /* wait forever */
      }
    }

    numevents = aeApiPoll(eventLoop, tvp);
    for (j = 0; j < numevents; j++)
    {
      aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];
      int mask = eventLoop->fired[j].mask;
      int fd = eventLoop->fired[j].fd;
      int rfired = 0;

      /* note the fe->mask & mask & ... code: maybe an already processed
             * event removed an element that fired and we still didn't
             * processed, so we check if the event is still valid. */
      if (fe->mask & mask & AE_READABLE)
      {
        rfired = 1;
        fe->rfileProc(eventLoop, fd, fe->clientData, mask);
      }
      if (fe->mask & mask & AE_WRITABLE)
      {
        if (!rfired || fe->wfileProc != fe->rfileProc)
          fe->wfileProc(eventLoop, fd, fe->clientData, mask);
      }
      processed++;
    }
  }
  /* Check time events */
  if (flags & AE_TIME_EVENTS)
    processed += processTimeEvents(eventLoop);

  return processed; /* return the number of processed file/time events */
}

/* Wait for milliseconds until the given file descriptor becomes
 * writable/readable/exception */
int aeWait(int fd, int mask, long long milliseconds)
{
  struct pollfd pfd;
  int retmask = 0, retval;

  memset(&pfd, 0, sizeof(pfd));
  pfd.fd = fd;
  if (mask & AE_READABLE)
    pfd.events |= POLLIN;
  if (mask & AE_WRITABLE)
    pfd.events |= POLLOUT;

  if ((retval = poll(&pfd, 1, milliseconds)) == 1)
  {
    if (pfd.revents & POLLIN)
      retmask |= AE_READABLE;
    if (pfd.revents & POLLOUT)
      retmask |= AE_WRITABLE;
    if (pfd.revents & POLLERR)
      retmask |= AE_WRITABLE;
    if (pfd.revents & POLLHUP)
      retmask |= AE_WRITABLE;
    return retmask;
  }
  else
  {
    return retval;
  }
}

void aeMain(aeEventLoop *eventLoop)
{
  eventLoop->stop = 0;
  while (!eventLoop->stop)
  {
    if (eventLoop->beforesleep != NULL)
      eventLoop->beforesleep(eventLoop);
    aeProcessEvents(eventLoop, AE_ALL_EVENTS);
  }
}

const char *aeGetApiName(void)
{
  return aeApiName();
}

void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep)
{
  eventLoop->beforesleep = beforesleep;
}

} // namespace util
} // namespace mycc

#ifndef MYCC_UTIL_COROUTINE_UTIL_H_
#define MYCC_UTIL_COROUTINE_UTIL_H_

#include <ucontext.h>
#include "list_util.h"
#include "memory_pool.h"

namespace mycc
{
namespace util
{

namespace easy_uthread
{

typedef void(easy_uthread_start_pt)(void *args);
static const uint32_t EASY_UTHREAD_STACK = (65536 - sizeof(easy_pool::easy_pool_t));

struct easy_uthread_t
{
  list_head runqueue_node;
  list_head thread_list_node;
  easy_pool::easy_pool_t *pool;
  easy_uthread_start_pt *startfn;
  void *startargs;

  uint32_t id;
  int8_t exiting;
  int8_t ready;
  int8_t errcode;
  uint32_t stksize;
  unsigned char *stk;
  ucontext_t context;
};

struct easy_uthread_control_t
{
  int gid;
  int nswitch;
  int16_t stoped;
  int16_t thread_count;
  int exit_value;
  list_head runqueue;
  list_head thread_list;
  easy_uthread_t *running;
  ucontext_t context;
};

void easy_uthread_init(easy_uthread_control_t *control);
void easy_uthread_destroy();
easy_uthread_t *easy_uthread_create(easy_uthread_start_pt *start, void *args, int stack_size);
easy_uthread_t *easy_uthread_current();
int easy_uthread_yield();
int easy_uthread_scheduler();
void easy_uthread_stop();
void easy_uthread_ready(easy_uthread_t *t);
void easy_uthread_switch();
void easy_uthread_needstack(int n);
void easy_uthread_ready(easy_uthread_t *t);
void easy_uthread_print(int sig);
int easy_uthread_get_errcode();
void easy_uthread_set_errcode(easy_uthread_t *t, int errcode);

#define EASY_UTHREAD_RUN_MAIN(main_name)                                       \
  static int easy_uthread_stacksize = 0;                                       \
  static int easy_uthread_argc;                                                \
  static char **easy_uthread_argv;                                             \
  static void easy_uthread_mainstart(void *v)                                  \
  {                                                                            \
    main_name(easy_uthread_argc, easy_uthread_argv);                           \
  }                                                                            \
  int main(int argc, char **argv)                                              \
  {                                                                            \
    int ret;                                                                   \
    struct sigaction sa, osa;                                                  \
    easy_uthread_control_t control;                                            \
    memset(&sa, 0, sizeof sa);                                                 \
    sa.sa_handler = easy_uthread_print;                                        \
    sa.sa_flags = SA_RESTART;                                                  \
    sigaction(SIGQUIT, &sa, &osa);                                             \
    easy_uthread_argc = argc;                                                  \
    easy_uthread_argv = argv;                                                  \
    if (easy_uthread_stacksize == 0)                                           \
      easy_uthread_stacksize = 256 * 1024;                                     \
    easy_uthread_init(&control);                                               \
    easy_uthread_create(easy_uthread_mainstart, NULL, easy_uthread_stacksize); \
    ret = easy_uthread_scheduler();                                            \
    easy_uthread_destroy();                                                    \
    return ret;                                                                \
  }

} // namespace easy_uthread

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_COROUTINE_UTIL_H_

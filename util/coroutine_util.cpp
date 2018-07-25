
#include "coroutine_util.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "atomic_util.h"

namespace mycc
{
namespace util
{

struct SimpleCoroutine::schedule
{
  char stack[DEFAULT_STACK_SIZE];
  ucontext_t main;
  int nco;
  int cap;
  int running;
  coroutine **co;
};

struct SimpleCoroutine::coroutine
{
  coroutine_func func;
  void *ud;
  ucontext_t ctx;
  schedule *sch;
  ptrdiff_t cap;
  ptrdiff_t size;
  int status;
  char *stack;
};

void SimpleCoroutine::mainfunc(uint32_t low32, uint32_t hi32)
{
  uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32);
  schedule *S = (schedule *)ptr;
  int id = S->running;
  coroutine *C = S->co[id];
  C->func(S, C->ud);
  _co_delete(C);
  S->co[id] = NULL;
  --S->nco;
  S->running = -1;
}

void SimpleCoroutine::_save_stack(coroutine *C, char *top)
{
  char dummy = 0;
  assert(top - &dummy <= DEFAULT_STACK_SIZE);
  if (C->cap < top - &dummy)
  {
    free(C->stack);
    C->cap = top - &dummy;
    C->stack = (char *)malloc(C->cap);
  }
  C->size = top - &dummy;
  memcpy(C->stack, &dummy, C->size);
}

SimpleCoroutine::coroutine *SimpleCoroutine::_co_new(schedule *S, coroutine_func func, void *ud)
{
  coroutine *co = (coroutine *)malloc(sizeof(*co));
  co->func = func;
  co->ud = ud;
  co->sch = S;
  co->cap = 0;
  co->size = 0;
  co->status = COROUTINE_READY;
  co->stack = NULL;
  return co;
}

void SimpleCoroutine::_co_delete(coroutine *co)
{
  free(co->stack);
  free(co);
}

SimpleCoroutine::schedule *SimpleCoroutine::coroutine_open(void)
{
  schedule *S = (schedule *)malloc(sizeof(*S));
  S->nco = 0;
  S->cap = DEFAULT_COROUTINE;
  S->running = -1;
  S->co = (coroutine **)malloc(sizeof(coroutine *) * S->cap);
  memset(S->co, 0, sizeof(coroutine *) * S->cap);
  return S;
}

void SimpleCoroutine::coroutine_close(schedule *S)
{
  int i;
  for (i = 0; i < S->cap; i++)
  {
    coroutine *co = S->co[i];
    if (co)
    {
      _co_delete(co);
    }
  }
  free(S->co);
  S->co = NULL;
  free(S);
}

int SimpleCoroutine::coroutine_new(schedule *S, coroutine_func func, void *ud)
{
  coroutine *co = _co_new(S, func, ud);
  if (S->nco >= S->cap)
  {
    int id = S->cap;
    S->co = (coroutine **)realloc(S->co, S->cap * 2 * sizeof(coroutine *));
    memset(S->co + S->cap, 0, sizeof(coroutine *) * S->cap);
    S->co[S->cap] = co;
    S->cap *= 2;
    ++S->nco;
    return id;
  }
  else
  {
    int i;
    for (i = 0; i < S->cap; i++)
    {
      int id = (i + S->nco) % S->cap;
      if (S->co[id] == NULL)
      {
        S->co[id] = co;
        ++S->nco;
        return id;
      }
    }
  }
  assert(0);
  return -1;
}

void SimpleCoroutine::coroutine_resume(schedule *S, int id)
{
  assert(S->running == -1);
  assert(id >= 0 && id < S->cap);
  coroutine *C = S->co[id];
  if (C == NULL)
    return;
  int status = C->status;
  switch (status)
  {
  case COROUTINE_READY:
  {
    getcontext(&C->ctx);
    C->ctx.uc_stack.ss_sp = S->stack;
    C->ctx.uc_stack.ss_size = DEFAULT_STACK_SIZE;
    C->ctx.uc_link = &S->main;
    S->running = id;
    C->status = COROUTINE_RUNNING;
    uintptr_t ptr = (uintptr_t)S;
    makecontext(&C->ctx, (void (*)(void))mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr >> 32));
    swapcontext(&S->main, &C->ctx);
  }
  break;
  case COROUTINE_SUSPEND:
  {
    memcpy(S->stack + DEFAULT_STACK_SIZE - C->size, C->stack, C->size);
    S->running = id;
    C->status = COROUTINE_RUNNING;
    swapcontext(&S->main, &C->ctx);
  }
  break;
  default:
    assert(0);
  }
}

void SimpleCoroutine::coroutine_yield(schedule *S)
{
  int id = S->running;
  assert(id >= 0);
  coroutine *C = S->co[id];
  assert((char *)&C > S->stack);
  _save_stack(C, S->stack + DEFAULT_STACK_SIZE);
  C->status = COROUTINE_SUSPEND;
  S->running = -1;
  swapcontext(&C->ctx, &S->main);
}

int SimpleCoroutine::coroutine_status(schedule *S, int id)
{
  assert(id >= 0 && id < S->cap);
  if (S->co[id] == NULL)
  {
    return COROUTINE_DEAD;
  }
  return S->co[id]->status;
}

int SimpleCoroutine::coroutine_running(schedule *S)
{
  return S->running;
}

namespace easy_uthread
{

// user-thread
__thread easy_uthread_control_t *easy_uthread_var = NULL;

static easy_uthread_t *easy_uthread_alloc(easy_uthread_start_pt *fn, void *args, int stack_size);
static void easy_uthread_start(uint32_t y, uint32_t x);
static void easy_uthread_context_switch(ucontext_t *from, ucontext_t *to);

void easy_uthread_init(easy_uthread_control_t *control)
{
  if (easy_uthread_var == NULL)
  {
    easy_uthread_var = control;
    memset(easy_uthread_var, 0, sizeof(easy_uthread_control_t));
    list_init(&easy_uthread_var->runqueue);
    list_init(&easy_uthread_var->thread_list);
  }
}

void easy_uthread_destroy()
{
  easy_uthread_t *t, *t2;

  if (!easy_uthread_var)
    return;

  list_for_each_entry_safe(t, t2, &easy_uthread_var->thread_list, thread_list_node)
  {
    easy_pool::easy_pool_destroy(t->pool);
  }

  easy_uthread_var = NULL;
}

void easy_uthread_stop()
{
  if (easy_uthread_var)
  {
    easy_uthread_var->stoped = 1;
  }
}

easy_uthread_t *easy_uthread_create(easy_uthread_start_pt *start_routine, void *args, int stack_size)
{
  easy_uthread_t *t;

  if (!easy_uthread_var)
    return NULL;

  if ((t = easy_uthread_alloc(start_routine, args, stack_size)) == NULL)
    return NULL;

  easy_uthread_var->thread_count++;
  list_add_tail(&t->thread_list_node, &easy_uthread_var->thread_list);

  easy_uthread_ready(t);
  return t;
}

void easy_uthread_exit(int val)
{
  easy_uthread_var->exit_value = val;
  easy_uthread_var->running->exiting = 1;
  easy_uthread_switch();
}

void easy_uthread_switch()
{
  easy_uthread_var->running->errcode = 0;
  easy_uthread_needstack(0);
  easy_uthread_context_switch(&easy_uthread_var->running->context, &easy_uthread_var->context);
}

easy_uthread_t *easy_uthread_current()
{
  return (easy_uthread_var ? easy_uthread_var->running : NULL);
}

int easy_uthread_get_errcode()
{
  if (easy_uthread_var && easy_uthread_var->running)
    return easy_uthread_var->running->errcode;
  else
    return 0;
}

void easy_uthread_set_errcode(easy_uthread_t *t, int errcode)
{
  t->errcode = (errcode & 0xff);
}

void easy_uthread_needstack(int n)
{
  easy_uthread_t *t;

  t = easy_uthread_var->running;

  if ((char *)&t <= (char *)t->stk || (char *)&t - (char *)t->stk < 256 + n)
  {
    fprintf(stderr, "uthread stack overflow: &t=%p tstk=%p n=%d\n", &t, t->stk, 256 + n);
    abort();
  }
}

void easy_uthread_ready(easy_uthread_t *t)
{
  if (t)
  {
    t->ready = 1;
    list_add_tail(&t->runqueue_node, &easy_uthread_var->runqueue);
  }
}

int easy_uthread_yield()
{
  int n;

  n = easy_uthread_var->nswitch;
  easy_uthread_ready(easy_uthread_var->running);
  easy_uthread_switch();
  return easy_uthread_var->nswitch - n - 1;
}

int easy_uthread_scheduler()
{
  easy_uthread_t *t;

  while (easy_uthread_var->stoped == 0)
  {
    if (easy_uthread_var->thread_count == 0)
    {
      break;
    }

    if (list_empty(&easy_uthread_var->runqueue))
    {
      fprintf(stderr, "no runnable user thread! (%d)\n", easy_uthread_var->thread_count);
      easy_uthread_var->exit_value = 1;
      break;
    }

    // first entry
    t = list_entry(easy_uthread_var->runqueue.next, easy_uthread_t, runqueue_node);
    list_del(&t->runqueue_node);

    t->ready = 0;
    easy_uthread_var->running = t;
    easy_uthread_var->nswitch++;

    // switch back
    easy_uthread_context_switch(&easy_uthread_var->context, &t->context);
    easy_uthread_var->running = NULL;

    // exited
    if (t->exiting)
    {
      list_del(&t->thread_list_node);
      easy_uthread_var->thread_count--;
      easy_pool::easy_pool_destroy(t->pool);
    }
  }

  return easy_uthread_var->exit_value;
}

void easy_uthread_print(int sig)
{
  easy_uthread_t *t;
  const char *extra;

  fprintf(stderr, "uthread list:\n");
  list_for_each_entry(t, &easy_uthread_var->thread_list, thread_list_node)
  {
    if (t == easy_uthread_var->running)
      extra = " (running)";
    else if (t->ready)
      extra = " (ready)";
    else
      extra = "";

    fprintf(stderr, "%6d %s\n", t->id, extra);
  }
}

static easy_uthread_t *easy_uthread_alloc(easy_uthread_start_pt *fn, void *args, int stack_size)
{
  easy_uthread_t *t;
  easy_pool::easy_pool_t *pool;
  int size;
  sigset_t zero;
  uint32_t x, y;
  uint64_t z;

  // allocate one pool
  size = sizeof(easy_uthread_t) + stack_size;

  if ((pool = easy_pool::easy_pool_create(size)) == NULL)
    return NULL;

  if ((t = (easy_uthread_t *)easy_pool::easy_pool_alloc(pool, size)) == NULL)
    goto error_exit;

  // init
  memset(t, 0, sizeof(easy_uthread_t));
  t->pool = pool;
  t->stk = (unsigned char *)(t + 1);
  t->stksize = stack_size;
  t->id = ++easy_uthread_var->gid;
  t->startfn = fn;
  t->startargs = args;

  /* do a reasonable initialization */
  memset(&t->context, 0, sizeof(t->context));
  sigemptyset(&zero);
  sigprocmask(SIG_BLOCK, &zero, &t->context.uc_sigmask);

  /* must initialize with current context */
  if (getcontext(&t->context) < 0)
    goto error_exit;

  /* call makecontext to do the real work. */
  t->context.uc_stack.ss_sp = t->stk;
  t->context.uc_stack.ss_size = t->stksize;
  z = (unsigned long)t;
  y = (uint32_t)z;
  x = (uint32_t)(z >> 32);

  makecontext(&t->context, (void (*)())easy_uthread_start, 2, y, x);

  return t;
error_exit:

  if (pool)
    easy_pool::easy_pool_destroy(pool);

  return NULL;
}

static void easy_uthread_start(uint32_t y, uint32_t x)
{
  uint64_t z;

  z = x;
  z <<= 32;
  z |= y;
  easy_uthread_t *t = (easy_uthread_t *)(long)z;
  t->startfn(t->startargs);
  easy_uthread_exit(0);
}

static void easy_uthread_context_switch(ucontext_t *from, ucontext_t *to)
{
  if (swapcontext(from, to) < 0)
  {
    fprintf(stderr, "swapcontext failed.\n");
    abort();
  }
}

} // namespace easy_uthread

} // namespace util
} // namespace mycc

#include "lock_free_util.h"
#include <assert.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>

namespace mycc
{
namespace util
{

/////////////////////// Hazard Pointers ////////////////////////////////
int64_t HazardPtrEasy::_tidSeed = 0;
void *HazardPtrEasy::_haz_array[HAZ_MAX_COUNT];
HazardPtrEasy::free_t HazardPtrEasy::_free_list[HAZ_MAX_THREAD];

int64_t HazardPtrEasy::seq_thread_id()
{
  static __thread int64_t _tid = -1;
  if (_tid >= 0)
    return _tid;
  _tid = __sync_fetch_and_add(&_tidSeed, 1);
  _free_list[_tid].next = NULL;
  return _tid;
}

bool HazardPtrEasy::haz_confict(int64_t self, void *p)
{
  int64_t self_p = self * HAZ_MAX_COUNT_PER_THREAD;
  for (int64_t i = 0; i < HAZ_MAX_COUNT; ++i)
  {
    if (i >= self_p && i < self_p + HAZ_MAX_COUNT_PER_THREAD)
      continue; /* skip self */
    if (_haz_array[i] == p)
      return true;
  }
  return false;
}

void **HazardPtrEasy::haz_get(int64_t idx)
{
  int64_t tid = seq_thread_id();
  return &_haz_array[tid * HAZ_MAX_COUNT_PER_THREAD + idx];
}

void HazardPtrEasy::haz_defer_free(void *p)
{
  int64_t tid = seq_thread_id();
  free_t *f = (free_t *)malloc(sizeof(*f));
  f->p = p;
  f->next = _free_list[tid].next;
  _free_list[tid].next = f;
  haz_gc();
}

void HazardPtrEasy::haz_gc()
{
  int64_t tid = seq_thread_id();
  free_t *head = &_free_list[tid];
  free_t *pred = head, *next = head->next;
  while (next)
  {
    if (!haz_confict(tid, next->p))
    { /* safe to free */
      free_t *tmp = next->next;
      printf("hazard (%d) free ptr %p\n", (int)tid, next->p);
      free(next->p);
      pred->next = tmp;
      free(next);
      next = tmp;
    }
    else
    {
      pred = next;
      next = next->next;
    }
  }
  if (head->next == NULL)
  {
    printf("thread %d freed all ptrs\n", (int)tid);
  }
}

//////////////////////////// URCU //////////////////////////////
// User-Level Implementations of Read-Copy Update

namespace urcu_easy
{

static const int64_t RCU_GP_CTR_BOTTOM_BIT = 0x80000000;
static const int64_t RCU_GP_CTR_NEST_MASK = (RCU_GP_CTR_BOTTOM_BIT - 1);
static const int64_t NR_THREADS = 512;

static int64_t rcu_gp_ctr = 1;
static __thread int64_t thread_id = -1;
static int32_t rcu_thread_num = 0;
static pthread_mutex_t rcu_gp_lock;

struct PerThread
{
  int64_t v __attribute__((__aligned__(CACHE_LINE_SIZE)));
} rcu_reader_gp[NR_THREADS];

void rcu_init()
{
  pthread_mutex_init(&rcu_gp_lock, NULL);
}

static inline void rcu_thread_register()
{
  pthread_mutex_lock(&rcu_gp_lock);
  assert(rcu_thread_num < NR_THREADS);
  thread_id = rcu_thread_num;
  rcu_thread_num++;
  pthread_mutex_unlock(&rcu_gp_lock);
}

static inline int64_t *per_thread_val()
{
  if (UNLIKELY(thread_id == -1))
  {
    rcu_thread_register();
  }
  return &(rcu_reader_gp[thread_id].v);
}

static inline int64_t *per_thread_val(int32_t id)
{
  return &(rcu_reader_gp[id].v);
}

static inline int64_t rcu_old_gp_ongoing(int32_t t)
{
  int64_t v = ACCESS_ONCE(*per_thread_val(t));
  return (v & RCU_GP_CTR_NEST_MASK) &&
         ((v ^ rcu_gp_ctr) & ~RCU_GP_CTR_NEST_MASK);
}

static void flip_counter_and_wait()
{
  rcu_gp_ctr ^= RCU_GP_CTR_BOTTOM_BIT;
  for (int64_t t = 0; t < rcu_thread_num; ++t)
  {
    while (rcu_old_gp_ongoing(t))
    {
      //poll(NULL, 0, 1);
      CPU_RELAX();
      CompilerBarrier();
    }
  }
}

void rcu_read_lock()
{
  int64_t tmp;
  int64_t *rrgp;

  rrgp = per_thread_val();
  tmp = *rrgp;
  if ((tmp & RCU_GP_CTR_NEST_MASK) == 0)
  {
    *rrgp = ACCESS_ONCE(rcu_gp_ctr);
    SMP_MB();
  }
  else
  {
    *rrgp = tmp + 1;
  }
}

void rcu_read_unlock()
{
  SMP_MB();
  --(*per_thread_val());
}

void synchronize_rcu()
{
  SMP_MB();
  pthread_mutex_lock(&rcu_gp_lock);
  flip_counter_and_wait();
  flip_counter_and_wait();
  pthread_mutex_unlock(&rcu_gp_lock);
  SMP_MB();
}

} // namespace urcu_easy

} // namespace util
} // namespace mycc
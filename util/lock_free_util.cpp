
#include "lock_free_util.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

namespace mycc
{
namespace util
{

// Hazard Pointers

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

} // namespace util
} // namespace mycc
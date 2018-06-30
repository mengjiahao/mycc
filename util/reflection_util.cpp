
#include "reflection_util.h"
#include <pthread.h>

namespace mycc
{
namespace util
{

namespace internal
{
  
static pthread_rwlock_t s_rwlock = PTHREAD_RWLOCK_INITIALIZER;

TypeRegisterGuard::TypeRegisterGuard()
{
  pthread_rwlock_wrlock(&s_rwlock);
}

TypeRegisterGuard::~TypeRegisterGuard()
{
  pthread_rwlock_unlock(&s_rwlock);
}

TypeLookupGuard::TypeLookupGuard()
{
  pthread_rwlock_rdlock(&s_rwlock);
}

TypeLookupGuard::~TypeLookupGuard()
{
  pthread_rwlock_unlock(&s_rwlock);
}

} // namespace internal

} // namespace util
} // namespace mycc
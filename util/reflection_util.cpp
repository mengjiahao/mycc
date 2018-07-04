
#include "reflection_util.h"
#include <pthread.h>
#include <string.h>

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

///////////////////////// TCDYN_RuntimeClass ///////////////////////

TCDYN_RuntimeClass *TCDYN_RuntimeClass::pFirstClass = NULL;

TCDYN_Object *TCDYN_RuntimeClass::createObject()
{
  if (m_pfnCreateObject == NULL)
  {
    return NULL;
  }

  TCDYN_Object *pObject = (*m_pfnCreateObject)();
  {
    return pObject;
  }
}

TCDYN_RuntimeClass *TCDYN_RuntimeClass::load(const char *szClassName)
{
  TCDYN_RuntimeClass *pClass;

  for (pClass = pFirstClass; pClass != NULL; pClass = pClass->m_pNextClass)
  {
    if (strcmp(szClassName, pClass->m_lpszClassName) == 0)
      return pClass;
  }
  return NULL;
}

TCDYN_RuntimeClass TCDYN_Object::classTCDYN_Object =
    {(char *)"TCDYN_Object",
     sizeof(TCDYN_Object),
     NULL,
     NULL,
     NULL};

static TCDYN_Init _init_TCDYN_Object(&TCDYN_Object::classTCDYN_Object);

TCDYN_RuntimeClass *TCDYN_Object::GetRuntimeClass() const
{
  return &TCDYN_Object::classTCDYN_Object;
}

bool TCDYN_Object::isKindOf(const TCDYN_RuntimeClass *pClass) const
{
  TCDYN_RuntimeClass *pClassThis = GetRuntimeClass();
  while (pClassThis != NULL)
  {
    if (pClassThis == pClass)
    {
      return true;
    }
    pClassThis = pClassThis->m_pBaseClass;
  }
  return false;
}

} // namespace util
} // namespace mycc
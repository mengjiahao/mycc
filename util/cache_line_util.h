
#ifndef MYCC_UTIL_CACHE_LINE_UTIL_H_
#define MYCC_UTIL_CACHE_LINE_UTIL_H_

#include "macros_util.h"

namespace mycc
{
namespace util
{

// Cacheline related --------------------------------------

#ifndef CACHE_LINE_SIZE
#if defined(__s390__)
#define CACHE_LINE_SIZE 256U
#elif defined(__powerpc__) || defined(__aarch64__)
#define CACHE_LINE_SIZE 128U
#else
#define CACHE_LINE_SIZE 64U
#endif
#endif

#ifdef defined(COMPILER_MSVC)
#define CACHE_LINE_ALIGNMENT __declspec(align(CACHE_LINE_SIZE))
#elifdef defined(COMPILER_GCC)
#define CACHE_LINE_ALIGNMENT __attribute__((aligned(CACHE_LINE_SIZE)))
#else
#define CACHE_LINE_ALIGNMENT
#endif /* _MSC_VER */

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_CACHE_LINE_UTIL_H_
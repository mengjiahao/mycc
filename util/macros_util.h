
#ifndef MYCC_UTIL_MACROS_UTIL_H_
#define MYCC_UTIL_MACROS_UTIL_H_

#include <assert.h>
#include <inttypes.h> // PRId64
#include <stddef.h>   // For size_t.

/// Platform macros.

#if defined(__linux__)
#define OS_LINUX 1
#else
#error "Platform is not linux"
#endif

// For access to standard POSIXish features, use OS_POSIX instead of a
// more specific macro.
#if defined(OS_MACOSX) || defined(OS_LINUX) || defined(OS_FREEBSD) ||    \
    defined(OS_OPENBSD) || defined(OS_SOLARIS) || defined(OS_ANDROID) || \
    defined(OS_NACL) || defined(OS_QNX)
#define OS_POSIX 1
#endif

/// Compiler macros.

#if defined(__GNUC__)
#define COMPILER_GCC 1
#elif defined(_MSC_VER)
#define COMPILER_MSVC 1
#else
#error "Compiler unknown"
#endif

// check if g++ is before c++11
#if defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus >= 201103L
#define BASE_CXX11_ENABLED 1
#else
#pragma message("Will need g++-4.6 or higher to compile all," \ "compile without c++0x, some features may be disabled")
#endif

#if !defined(BASE_CXX11_ENABLED)
#define nullptr NULL
#endif

#ifndef LIKELY
#define LIKELY(x) (__builtin_expect(!!(x), 1))
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) (__builtin_expect(!!(x), 0))
#endif

#if defined(COMPILER_GCC)
#define BUILTIN_PREFETCH(addr, rw, locality) __builtin_prefetch(addr, rw, locality)
#else
#define BUILTIN_PREFETCH(addr, rw, locality)
#endif

// Return the byte alignment of the given type (available at compile time).  Use
// sizeof(type) prior to checking __alignof to workaround Visual C++ bug:
// http://goo.gl/isH0C
// Use like:
//   ALIGNOF(int32_t)  // this would be 4
#if defined(COMPILER_MSVC)
#define ALIGNOF(type) (sizeof(type) - sizeof(type) + __alignof(type))
#elif defined(COMPILER_GCC)
#define ALIGNOF(type) __alignof__(type)
#endif

#ifdef BASE_CXX11_ENABLED
#define STATIC_THREAD_LOCAL static thread_local
#define THREAD_LOCAL thread_local
#else
#define STATIC_THREAD_LOCAL static __thread // gcc
#define THREAD_LOCAL __thread
#endif

#ifndef BASE_TYPEOF
#if defined(BASE_CXX11_ENABLED)
#define BASE_TYPEOF decltype
#else
#define BASE_TYPEOF typeof
#endif
#endif // BASE_TYPEOF

// Control visiblity outside .so
#if defined(COMPILER_MSVC)
#ifdef COMPILE_LIBRARY
#define BASE_EXPORT __declspec(dllexport)
#define BASE_EXPORT_PRIVATE __declspec(dllexport)
#else
#define BASE_EXPORT __declspec(dllimport)
#define BASE_EXPORT_PRIVATE __declspec(dllimport)
#endif // COMPILE_LIBRARY
#else
#define BASE_EXPORT __attribute__((visibility("default")))
#define BASE_EXPORT_PRIVATE __attribute__((visibility("default")))
#endif // COMPILER_MSVC

// Annotate a variable or function indicating it's ok if the variable or function is not used.
// (Typically used to silence a compiler warning when the assignment
// is important for some other reason.)
// Use like:
//   int x ALLOW_UNUSED = ...;
//   int fool() ALLOW_UNUSED;
#if defined(COMPILER_GCC)
#define ALLOW_UNUSED __attribute__((unused))
#define ATTRIBUTE_UNUSED __attribute__((unused))
#else
#define ALLOW_UNUSED
#define ATTRIBUTE_UNUSED
#endif

// remove 'unused parameter' warning
#ifndef EXPR_UNUSED
#define EXPR_UNUSED(expr) \
  do                      \
  {                       \
    (void)(expr);         \
  } while (0)
#endif

#ifndef UNUSED_PARAM
#define UNUSED_PARAM(param) (void)(param)
#endif

/// thread-annotations

#if defined(__clang__) && (!defined(SWIG))
#define THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x) // no-op
#endif

// Document if a shared variable/field needs to be protected by a mutex.
// GUARDED_BY allows the user to specify a particular mutex that should be
// held when accessing the annotated variable.  GUARDED_VAR indicates that
// a shared variable is guarded by some unspecified mutex, for use in rare
// cases where a valid mutex expression cannot be specified.
#define GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))
#define GUARDED_VAR THREAD_ANNOTATION_ATTRIBUTE__(guarded)

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#ifdef BASE_CXX11_ENABLED
#define BASE_DELETE_FUNCTION(decl) decl = delete
#else
#define BASE_DELETE_FUNCTION(decl) decl
#endif

// Put this in the private: declarations for a class to be uncopyable.
#ifndef DISALLOW_COPY
#define DISALLOW_COPY(TypeName) \
  BASE_DELETE_FUNCTION(TypeName(const TypeName &))
#endif

// Put this in the private: declarations for a class to be unassignable.
#ifndef DISALLOW_ASSIGN
#define DISALLOW_ASSIGN(TypeName) \
  BASE_DELETE_FUNCTION(void operator=(const TypeName &))
#endif

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName)          \
  BASE_DELETE_FUNCTION(TypeName(const TypeName &)); \
  BASE_DELETE_FUNCTION(void operator=(const TypeName &))
#endif

#ifndef DECLARE_SINGLETON
#define DECLARE_SINGLETON(classname) \
public:                              \
  static classname *instance()       \
  {                                  \
    static classname instance;       \
    return &instance;                \
  }                                  \
                                     \
private:                             \
  classname();                       \
  DISALLOW_COPY_AND_ASSIGN(classname)
#endif // DECLARE_SINGLETON

#ifndef DECLARE_PROPERTY
#define DECLARE_PROPERTY(type, name)                   \
public:                                                \
  void set_##name(const type &val) { m_##name = val; } \
  const type &name() const { return m_##name; }        \
  type *mutable_##name() { return &m_##name; }         \
                                                       \
private:                                               \
  type m_##name;
#endif // #ifndef DECLARE_PROPERTY

/// Util macros.

// Concatenate numbers in c/c++ macros.
#ifndef BASE_CONCAT
#define BASE_CONCAT(a, b) BASE_CONCAT_HELPER(a, b)
#define BASE_CONCAT_HELPER(a, b) a##b
#endif

// This is not very useful as it does not expand defined symbols if
// called directly. Use its counterpart without the _NO_EXPANSION
// suffix, below.
#define STRINGIZE_NO_EXPANSION(x) #x
// Use this to quote the provided parameter, first expanding it if it
// is a preprocessor symbol.
//
// For example, if:
//   #define A FOO
//   #define B(x) myobj->FunctionCall(x)
//
// Then:
//   STRINGIZE(A) produces "FOO"
//   STRINGIZE(B(y)) produces "myobj->FunctionCall(y)"
#define STRINGIZE(x) STRINGIZE_NO_EXPANSION(x)

#define ARRAYSIZE_UNSAFE(a)     \
  ((sizeof(a) / sizeof(*(a))) / \
   static_cast<uint64_t>(!(sizeof(a) % sizeof(*(a)))))

// common macros and data structures
#ifndef FIELD_OFFSET
#define FIELD_OFFSET(s, field) (((size_t) & ((s *)(10))->field) - 10)
#endif

#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(address, type, field) \
  ((type *)((char *)(address)-FIELD_OFFSET(type, field)))
#endif

#define PRId64_FORMAT "%" PRId64
#define PRIu64_FORMAT "%" PRIu64

#define BASE_SAFE_DELETE(p) \
  do                        \
  {                         \
    delete (p);             \
    (p) = NULL;             \
  } while (0)

#define BASE_SAFE_DELETE_ARRAY(p) \
  do                              \
  {                               \
    if (p)                        \
    {                             \
      delete[](p);                \
      (p) = NULL;                 \
    }                             \
  } while (0)

#define BASE_SAFE_FREE(p) \
  do                      \
  {                       \
    ::free(p);            \
    (p) = NULL;           \
  } while (0)

// This provides a wrapper around system calls which may be interrupted by a
// signal and return EINTR. See man 7 signal.
// To prevent long-lasting loops (which would likely be a bug, such as a signal
// that should be masked) to go unnoticed, there is a limit after which the
// caller will nonetheless see an EINTR in Debug builds.
#define HANDLE_EINTR(x) ({                                \
  BASE_TYPEOF(x)                                          \
  eintr_wrapper_result;                                   \
  do                                                      \
  {                                                       \
    eintr_wrapper_result = (x);                           \
  } while (eintr_wrapper_result == -1 && errno == EINTR); \
  eintr_wrapper_result;                                   \
})

#endif // MYCC_UTIL_MACROS_UTIL_H_
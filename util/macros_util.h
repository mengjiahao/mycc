
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

/**
 * Macro for marking functions as having public visibility.
 * Ported from folly/CPortability.h
 */
#ifndef GNUC_PREREQ
#if defined __GNUC__ && defined __GNUC_MINOR__
#define GNUC_PREREQ(maj, min) \
  ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
#define GNUC_PREREQ(maj, min) 0
#endif
#endif

// Mark a branch likely or unlikely to be true.
// We can't remove the BAIDU_ prefix because the name is likely to conflict,
// namely kylin already has the macro.
#if defined(COMPILER_GCC)
#if defined(__cplusplus)
#define PREDICT_TRUE(x) (__builtin_expect((bool)(x), true))
#define PREDICT_FALSE(x) (__builtin_expect((bool)(x), false))
#define LIKELY(expr) (__builtin_expect((bool)(expr), true))
#define UNLIKELY(expr) (__builtin_expect((bool)(expr), false))
#else
#define PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#define PREDICT_FALSE(x) (__builtin_expect(x, 0))
#define LIKELY(expr) (__builtin_expect(!!(expr), 1))
#define UNLIKELY(expr) (__builtin_expect(!!(expr), 0))
#endif
#else
#define PREDICT_TRUE(x)
#define PREDICT_FALSE(x)
#define LIKELY(expr) (expr)
#define UNLIKELY(expr) (expr)
#endif

#if defined(COMPILER_GCC)
#define BUILTIN_PREFETCH(addr, rw, locality) __builtin_prefetch(addr, rw, locality)
#else
#define BUILTIN_PREFETCH(addr, rw, locality)
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

#if defined(COMPILER_GCC)
#define NORETURN __attribute__((noreturn))
#define NORETURN_PTR __attribute__((__noreturn__))
#else
#define NORETURN
#define NORETURN_PTR
#endif

// Annotate a function indicating it should not be inlined.
// Use like:
//   NOINLINE void DoStuff() { ... }
#if defined(COMPILER_GCC)
#define NOINLINE __attribute__((noinline))
#elif defined(COMPILER_MSVC)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE
#endif

#ifndef BASE_FORCE_INLINE
#if defined(COMPILER_MSVC)
#define BASE_FORCE_INLINE __forceinline
#else
#define BASE_FORCE_INLINE inline __attribute__((always_inline))
#endif
#endif // BASE_FORCE_INLINE

// Specify memory alignment for structs, classes, etc.
// Use like:
//   class ALIGNAS(16) MyClass { ... }
//   ALIGNAS(16) int array[4];
#ifndef ALIGNAS
#if defined(COMPILER_MSVC)
#define ALIGNAS(byte_alignment) __declspec(align(byte_alignment))
#elif defined(COMPILER_GCC)
#define ALIGNAS(byte_alignment) __attribute__((aligned(byte_alignment)))
#endif
#endif // ALIGNAS

// Return the byte alignment of the given type (available at compile time).  Use
// sizeof(type) prior to checking __alignof to workaround Visual C++ bug:
// http://goo.gl/isH0C
// Use like:
//   ALIGNOF(int32_t)  // this would be 4
#ifndef ALIGNOF
#if defined(COMPILER_MSVC)
#define ALIGNOF(type) (sizeof(type) - sizeof(type) + __alignof(type))
#elif defined(COMPILER_GCC)
#define ALIGNOF(type) __alignof__(type)
#endif
#endif // ALIGNOF

// Tell the compiler a function is using a printf-style format string.
// |format_param| is the one-based index of the format string parameter;
// |dots_param| is the one-based index of the "..." parameter.
// For v*printf functions (which take a va_list), pass 0 for dots_param.
// (This is undocumented but matches what the system C headers do.)
#ifndef PRINTF_ATTRIBUTE
#if defined(COMPILER_GCC)
#define PRINTF_ATTRIBUTE(format_param, dots_param) \
  __attribute__((format(printf, format_param, dots_param)))
#else
#define PRINTF_ATTRIBUTE(format_param, dots_param)
#endif
#endif // PRINTF_ATTRIBUTE

#ifndef SCANF_ATTRIBUTE
#if defined(COMPILER_GCC)
#define SCANF_ATTRIBUTE(string_index, first_to_check) \
  __attribute__((__format__(__scanf__, string_index, first_to_check)))
#else
#define SCANF_ATTRIBUTE(string_index, first_to_check)
#endif
#endif // SCANF_ATTRIBUTE

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

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
// Put this in the private: declarations for a class to be uncopyable.
#ifndef DISALLOW_COPY
#define DISALLOW_COPY(TypeName) \
  TypeName(const TypeName &)
#endif

// Put this in the private: declarations for a class to be unassignable.
#ifndef DISALLOW_ASSIGN
#define DISALLOW_ASSIGN(TypeName) \
  void operator=(const TypeName &)
#endif

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName &);              \
  void operator=(const TypeName &)
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

#define ASSERT(x) assert(x)

#define BASE_VERSION_CHECK(major, minor, patch) ((major << 16) | (minor << 8) | (patch))

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

// new callbacks based on C++11
#define BASE_CALLBACK_0(__selector__, __target__, ...) std::bind(&__selector__, __target__, ##__VA_ARGS__)
#define BASE_CALLBACK_1(__selector__, __target__, ...) std::bind(&__selector__, __target__, std::placeholders::_1, ##__VA_ARGS__)
#define BASE_CALLBACK_2(__selector__, __target__, ...) std::bind(&__selector__, __target__, std::placeholders::_1, std::placeholders::_2, ##__VA_ARGS__)
#define BASE_CALLBACK_3(__selector__, __target__, ...) std::bind(&__selector__, __target__, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, ##__VA_ARGS__)

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
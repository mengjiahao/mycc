
#ifndef MYCC_UTIL_MEM_UTIL_H_
#define MYCC_UTIL_MEM_UTIL_H_

#include "types_util.h"

namespace mycc
{
namespace util
{

// AlignedMemory is a POD type that gives you a portable way to specify static
// or local stack data of a given alignment and size. For example, if you need
// static storage for a class, but you want manual control over when the object
// is constructed and destructed (you don't want static initialization and
// destruction), use AlignedMemory:
//
//   static AlignedMemory<sizeof(MyClass), ALIGNOF(MyClass)> my_class;
//
//   // ... at runtime:
//   new(my_class.void_data()) MyClass();
//
//   // ... use it:
//   MyClass* mc = my_class.data_as<MyClass>();
//
//   // ... later, to destruct my_class:
//   my_class.data_as<MyClass>()->MyClass::~MyClass();
//
// Alternatively, a runtime sized aligned allocation can be created:
//
//   float* my_array = static_cast<float*>(AlignedAlloc(size, alignment));
//
//   // ... later, to release the memory:
//   AlignedFree(my_array);
//
// Or using scoped_ptr:
//
//   scoped_ptr<float, AlignedFreeDeleter> my_array(
//       static_cast<float*>(AlignedAlloc(size, alignment)));

// AlignedMemory is specialized for all supported alignments.
// Make sure we get a compiler error if someone uses an unsupported alignment.
// AlignedMemory is specialized for all supported alignments.
// Make sure we get a compiler error if someone uses an unsupported alignment.
template <size_t Size, size_t ByteAlignment>
struct AlignedMemory
{
};

#define BASE_DECL_ALIGNED_MEMORY(byte_alignment)                 \
  template <size_t Size>                                         \
  class AlignedMemory<Size, byte_alignment>                      \
  {                                                              \
  public:                                                        \
    ALIGNAS(byte_alignment)                                      \
    uint8_t data_[Size];                                         \
    void *void_data() { return static_cast<void *>(data_); }     \
    const void *void_data() const                                \
    {                                                            \
      return static_cast<const void *>(data_);                   \
    }                                                            \
    template <typename Type>                                     \
    Type *data_as() { return static_cast<Type *>(void_data()); } \
    template <typename Type>                                     \
    const Type *data_as() const                                  \
    {                                                            \
      return static_cast<const Type *>(void_data());             \
    }                                                            \
                                                                 \
  private:                                                       \
    void *operator new(size_t);                                  \
    void operator delete(void *);                                \
  }

// Specialization for all alignments is required because MSVC (as of VS 2008)
// does not understand ALIGNAS(ALIGNOF(Type)) or ALIGNAS(template_param).
// Greater than 4096 alignment is not supported by some compilers, so 4096 is
// the maximum specified here.
BASE_DECL_ALIGNED_MEMORY(1);
BASE_DECL_ALIGNED_MEMORY(2);
BASE_DECL_ALIGNED_MEMORY(4);
BASE_DECL_ALIGNED_MEMORY(8);
BASE_DECL_ALIGNED_MEMORY(16);
BASE_DECL_ALIGNED_MEMORY(32);
BASE_DECL_ALIGNED_MEMORY(64);
BASE_DECL_ALIGNED_MEMORY(128);
BASE_DECL_ALIGNED_MEMORY(256);
BASE_DECL_ALIGNED_MEMORY(512);
BASE_DECL_ALIGNED_MEMORY(1024);
BASE_DECL_ALIGNED_MEMORY(2048);
BASE_DECL_ALIGNED_MEMORY(4096);

#undef BASE_DECL_ALIGNED_MEMORY

// Aligned allocation/deallocation. `minimum_alignment` must be a power of 2
// and a multiple of sizeof(void*).
void *AlignedMalloc(uint64_t size, int32_t minimum_alignment);
void AlignedFree(void *aligned_memory);

// Deleter for use with scoped_ptr. E.g., use as
//   scoped_ptr<Foo, base::AlignedFreeDeleter> foo;
struct AlignedFreeDeleter
{
  inline void operator()(void *ptr) const
  {
    AlignedFree(ptr);
  }
};

void *Malloc(uint64_t size);
void *Realloc(void *ptr, uint64_t size);
void Free(void *ptr);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_MEM_UTIL_H_
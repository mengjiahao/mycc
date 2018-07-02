
#include "memory_pool.h"

namespace mycc
{
namespace util
{

// class CachedObject : public CachedAllocator<CachedObject>
// {
// public:
// };

// class Object
// {
// public:
// };

// TEST(CachedAllocatortTest, AllocateAndRelease)
// {
//   CachedObject *t = new CachedObject();
//   delete t;
//   ASSERT_EQ(1, MemoryPieceStore::Instance()->allocate_count());
//   ASSERT_EQ(1, MemoryPieceStore::Instance()->release_count());

//   std::vector<CachedObject *> objects;
//   for (size_t i = 0; i < MemoryPieceStore::kMaxCacheCount * 10; ++i)
//   {
//     objects.push_back(new CachedObject());
//   }
//   ASSERT_EQ(MemoryPieceStore::kMaxCacheCount * 10 + 1,
//             MemoryPieceStore::Instance()->allocate_count());
//   for (size_t i = 0; i < objects.size(); ++i)
//   {
//     delete objects[i];
//   }
//   ASSERT_EQ(MemoryPieceStore::kMaxCacheCount * 10 + 1,
//             MemoryPieceStore::Instance()->release_count());
// }

// TEST(CachedAllocatortTest, DeleteNull)
// {
//   CachedObject *t = NULL;
//   delete t;
// }

// // Test result show that, cached object brings about 17x performance gains
// TEST(CachedAllocatortTest, PressureTest)
// {
//   struct timeval t1, t2, t3;
//   gettimeofday(&t1, NULL);
//   for (size_t i = 0; i < 1000 * 1000; ++i)
//   {
//     CachedObject *t = new CachedObject();
//     delete t;
//   }
//   gettimeofday(&t2, NULL);
//   for (size_t i = 0; i < 1000 * 1000; ++i)
//   {
//     Object *t = new Object();
//     delete t;
//   }
//   gettimeofday(&t3, NULL);
//   int cost1_us = (t2.tv_sec - t1.tv_sec) * 1000 * 1000 + (t2.tv_usec - t1.tv_usec);
//   int cost2_us = (t3.tv_sec - t2.tv_sec) * 1000 * 1000 + (t3.tv_usec - t2.tv_usec);
//   PRINTF_INFO("cost1: %d, cost2: %d\n", cost1_us, cost2_us);
// }

} // namespace util
} // namespace mycc
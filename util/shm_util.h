
#ifndef MYCC_UTIL_SHM_UTIL_H_
#define MYCC_UTIL_SHM_UTIL_H_

#include <sys/shm.h>
#include "types_util.h"

namespace mycc
{
namespace util
{

// Use shm as bitmap
class ShmBitmapManager
{
public:
  enum
  {
    MAX_UIN = 0x80000000
  };
  enum
  {
    PASSPORT_SHM_SIZE = (MAX_UIN >> 3)
  };
  enum
  {
    PASSPORT_SHM_KEY = 0x00710000
  };
  enum
  {
    BITMAP_CHK = 0,
    BITMAP_SET = 1,
    BITMAP_CLR = 2
  };

  ~ShmBitmapManager(void);
  static ShmBitmapManager *instance(void);

  int open(key_t key = 0, uint32_t max_uin = 0);
  int close(void);

  int get_bit(uint32_t uin);
  int set_bit(uint32_t uin);
  int clr_bit(uint32_t uin);
  int count_bit();

private:
  void *get_share_memory(key_t key, uint64_t size);
  int init_passport_shm(void);
  int handle_shm_bit_i(void *shm_addr, uint32_t uin, int flag);
  int count_shm_set_bit_num(void *shm_addr);

private:
  void *shm_addr_; // bit map 共享内存映射地址
  static ShmBitmapManager *instance_;
  uint32_t max_uin_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SHM_UTIL_H_
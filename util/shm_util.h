
#ifndef MYCC_UTIL_SHM_UTIL_H_
#define MYCC_UTIL_SHM_UTIL_H_

#include <unistd.h>
#include <sys/shm.h>
#include "types_util.h"

namespace mycc
{
namespace util
{

class TCShm
{
public:
  TCShm(bool bOwner = false)
      : _shmSize(0), _shmKey(0), _pshm(NULL), _shemID(-1),
        _bOwner(bOwner), _bCreate(true) {}

  TCShm(uint64_t iShmSize, key_t iKey, bool bOwner = false);
  ~TCShm();

  void init(uint64_t iShmSize, key_t iKey, bool bOwner = false);
  // created or connect shm, created shm need to be inited
  bool iscreate() { return _bCreate; }
  // shm base
  void *getPointer() { return _pshm; }
  // shm key
  key_t getkey() { return _shmKey; }
  // shm id
  int getid() { return _shemID; }
  // shm size
  uint64_t size() { return _shmSize; }
  // detach shm in current process
  int detach();
  // totally delete shm
  int del();

protected:
  uint64_t _shmSize;
  key_t _shmKey;
  void *_pshm;
  int _shemID;
  bool _bOwner; // when _bOwner=true, need to detach when dtor
  bool _bCreate;
};

// Use shm as bitmap
class TCShmBitmapManager
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

  ~TCShmBitmapManager(void);
  static TCShmBitmapManager *instance(void);

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
  static TCShmBitmapManager *instance_;
  uint32_t max_uin_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SHM_UTIL_H_
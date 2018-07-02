#include "shm_util.h"
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include "error_util.h"

namespace mycc
{
namespace util
{

TCShm::TCShm(uint64_t iShmSize, key_t iKey, bool bOwner)
{
  init(iShmSize, iKey, bOwner);
}

TCShm::~TCShm()
{
  if (_bOwner)
  {
    detach();
  }
}

void TCShm::init(uint64_t iShmSize, key_t iKey, bool bOwner)
{
  assert(_pshm == NULL);
  _bOwner = bOwner;

  if ((_shemID = shmget(iKey, iShmSize, IPC_CREAT | IPC_EXCL | 0666)) < 0)
  {
    _bCreate = false; // init here is safe in multi-thread
    // if exist the same key_shm, then try to connect
    if ((_shemID = shmget(iKey, iShmSize, 0666)) < 0)
    {
      PRINT_ERROR("shmget error\n");
      return;
    }
  }
  else
  {
    _bCreate = true;
  }

  //try to access shm
  if ((_pshm = shmat(_shemID, NULL, 0)) == (char *)-1)
  {
    PRINT_ERROR("shmat error\n");
    return;
  }
  _shmSize = iShmSize;
  _shmKey = iKey;
}

int TCShm::detach()
{
  int iRetCode = 0;
  if (_pshm != NULL)
  {
    iRetCode = shmdt(_pshm);
    _pshm = NULL;
  }
  return iRetCode;
}

int TCShm::del()
{
  int iRetCode = 0;
  if (_pshm != NULL)
  {
    iRetCode = shmctl(_shemID, IPC_RMID, 0);
    _pshm = NULL;
  }
  return iRetCode;
}

TCShmBitmapManager *TCShmBitmapManager::instance_ = NULL;

TCShmBitmapManager::~TCShmBitmapManager()
{
}

TCShmBitmapManager *TCShmBitmapManager::instance(void)
{
  if (NULL == instance_)
  {
    PRINT_INFO("%s\n", "TCShmBitmapManager::instance()");
    instance_ = new TCShmBitmapManager();
  }
  return instance_;
}

// if shm is created, then max_uin is invalid, use ipcs -m to check the size
int TCShmBitmapManager::open(key_t key, uint32_t max_uin)
{
  key_t key_handle;
  uint64_t size_handle;

  if ((0 == key) || (0 == max_uin))
  {
    key_handle = PASSPORT_SHM_KEY;
    max_uin_ = MAX_UIN;
    size_handle = PASSPORT_SHM_SIZE;
  }
  else
  {
    key_handle = key;
    max_uin_ = max_uin;
    size_handle = max_uin >> 3; // 8 bits flag
  }

  // Req 256M share memory
  shm_addr_ = get_share_memory(key_handle, size_handle);
  if (NULL == shm_addr_)
  {
    PRINT_ERROR("init_shm error key=0x%08x, size=%u\n",
                key_handle,
                (unsigned)size_handle);
    return -1;
  }

  PRINT_INFO("init_shm successfull."
             "shm_addr=0x%08lx, "
             "key=0x%08x, "
             "size=%u\n",
             (uintptr_t)(shm_addr_),
             (int)key_handle,
             (unsigned)size_handle);
  return 0;
}

int TCShmBitmapManager::close(void)
{
  int ret = 0;
  if (NULL != shm_addr_)
  {
    ret = shmdt(shm_addr_);
    if (-1 == ret)
    {
      PRINT_ERROR("shmdt() addr=0x%08lx,errno=%d\n",
                  (uintptr_t)shm_addr_, errno);
    }
  }
  shm_addr_ = NULL;
  return ret;
}

void *TCShmBitmapManager::get_share_memory(key_t key, uint64_t size)
{
  void *shm_addr = NULL;
  key_t sem_key;
  int sem_id;
  sem_key = key; //ftok( pathname, key );
  PRINT_INFO("sem_key=%u\n", sem_key);

  sem_id = shmget(sem_key, size, (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP));

  // not exist, create a new one
  if (-1 == sem_id)
  {
    if (ENOENT == errno)
    {
      // shm mode=0660
      sem_id = shmget(sem_key, size, ((S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) | IPC_CREAT));
      if (-1 == sem_id)
      {
        PRINT_ERROR("shmget() key=0x%08x,errno=%d\n",
                    sem_key, errno);
        return (void *)NULL;
      }

      shm_addr = shmat(sem_id, NULL, 0);
      if ((void *)-1 == shm_addr)
      {
        PRINT_ERROR("shmat() sem_id=0x%08x,errno=%d\n",
                    sem_id, errno);
        return NULL;
      }
      else
      {
        memset(shm_addr, 0, size);
        PRINT_INFO("create key=0x%08x,sem_id=%u,shm_addr=0x%08lx\n",
                   sem_key, sem_id, (uintptr_t)shm_addr);
        return shm_addr;
      }
    }
    else
    {
      PRINT_ERROR("shmget() key=0x%08x,errno=%d\n",
                  sem_key, errno);
      return NULL;
    }
  }
  else
  {
    shm_addr = shmat(sem_id, NULL, 0);
    if ((void *)-1 == shm_addr)
    {
      PRINT_ERROR("shmat() sem_id=0x%08x,errno=%d\n",
                  sem_id, errno);
      return NULL;
    }

    PRINT_INFO("find key=0x%08x,sem_id=%u,shmat_addr=0x%08lx\n",
               sem_key, sem_id, (uintptr_t)shm_addr);
    return shm_addr;
  }
}

int TCShmBitmapManager::get_bit(uint32_t uin)
{
  if (uin < max_uin_)
  {
    return handle_shm_bit_i(shm_addr_, uin, BITMAP_CHK);
  }
  else
  {
    return 0;
  }
}

int TCShmBitmapManager::set_bit(uint32_t uin)
{
  if (uin < max_uin_)
  {
    return handle_shm_bit_i(shm_addr_, uin, BITMAP_SET);
  }
  else
  {
    return 0;
  }
}

int TCShmBitmapManager::clr_bit(uint32_t uin)
{
  if (uin < max_uin_)
  {
    return handle_shm_bit_i(shm_addr_, uin, BITMAP_CLR);
  }
  else
  {
    return 0;
  }
}

inline int TCShmBitmapManager::handle_shm_bit_i(void *shm_addr, uint32_t uin, int flag)
{
  uint8_t bit_now = 0;
  uint32_t uin_byte_addr_offset = (uin >> 3); // uin pre 29 bits
  uint8_t uin_bit = (1 << (uin & 0x07));      // uin last 3 bits used for index
  uint8_t uin_byte = 0;
  unsigned char *shm_map_addr = (unsigned char *)shm_addr;
  uint8_t *curr_addr = (uint8_t *)(shm_map_addr + uin_byte_addr_offset);

  if ((NULL != shm_map_addr) && ((void *)-1 != shm_map_addr))
  {
    // uin
    uin_byte = (*(uint8_t *)curr_addr);
    uint8_t bit_old = (uin_byte & uin_bit) ? 1 : 0;

    switch (flag)
    {
    case BITMAP_CHK: // check bit
      bit_now = ((*curr_addr) & uin_bit) ? 1 : 0;
      break;

    case BITMAP_SET: // set bit
      (*curr_addr) = uin_byte | uin_bit;
      bit_now = ((*curr_addr) & uin_bit) ? 1 : 0;
      break;

    case BITMAP_CLR: //clr bit
      (*curr_addr) = uin_byte & (~uin_bit);
      bit_now = ((*curr_addr) & uin_bit) ? 1 : 0;
      break;

    default:
      break;
    }

    PRINT_INFO("uin=%u, %d=>%d\n",
               uin, bit_old, bit_now);
  }
  else
  {
    PRINT_INFO("shm_addr=%p, uin=%u, uin_byte=0x%02X, uin_bit=0x%02X\n",
               shm_addr, uin, uin_byte, uin_bit);
    return -1;
  }

  return bit_now;
}

int TCShmBitmapManager::count_bit()
{
  return count_shm_set_bit_num(shm_addr_);
}

int TCShmBitmapManager::count_shm_set_bit_num(void *shm_addr)
{
  uint32_t num = 0;
  uint32_t uin = 10000;
  for (; uin < max_uin_; uin++)
  {
    uint32_t uin_byte_addr_offset = (uin >> 3);
    uint8_t uin_bit = (1 << (uin & 0x07));
    uint8_t uin_byte = 0;
    unsigned char *shm_map_addr = (unsigned char *)shm_addr;
    uint8_t *curr_addr = (uint8_t *)(shm_map_addr + uin_byte_addr_offset);

    if ((NULL != shm_map_addr) && ((void *)-1 != shm_map_addr))
    {
      uin_byte = (*(uint8_t *)curr_addr);
      uint8_t bit_old = (uin_byte & uin_bit) ? 1 : 0;
      if (1 == bit_old)
      {
        num++;
      }
    }
    else
    {
      PRINT_ERROR("shm_addr=%p, uin=%u, uin_byte=0x%02X, uin_bit=0x%02X\n",
                  shm_addr, uin, uin_byte, uin_bit);
      return 0;
    }
  }
  return num;
}

} // namespace util
} // namespace mycc
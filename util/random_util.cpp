
#include "random_util.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "macros_util.h"
#include "string_util.h"

namespace mycc
{
namespace util
{

// We keep the file descriptor for /dev/urandom around so we don't need to
// reopen it (which is expensive), and since we may not even be able to reopen
// it if we are later put in a sandbox. This class wraps the file descriptor so
// we can use LazyInstance to handle opening it on the first access.
class URandomFd
{
public:
  URandomFd() : fd_(::open("/dev/urandom", O_RDONLY))
  {
    //DCHECK_GE(fd_, 0) << "Cannot open /dev/urandom: " << errno;
  }

  ~URandomFd() { ::close(fd_); }

  int fd() const { return fd_; }

private:
  const int fd_;
};

static URandomFd g_urandom_fd;

static bool ReadFromFD(int fd, char *buffer, uint64_t bytes)
{
  uint64_t total_read = 0;
  while (total_read < bytes)
  {
    int64_t bytes_read =
        HANDLE_EINTR(read(fd, buffer + total_read, bytes - total_read));
    if (bytes_read <= 0)
      break;
    total_read += bytes_read;
  }
  return total_read == bytes;
}

void RandBytes(void *output, uint64_t output_length)
{
  const int urandom_fd = g_urandom_fd.fd();
  ReadFromFD(urandom_fd, static_cast<char *>(output), output_length);
}

// NOTE: This function must be cryptographically secure. http://crbug.com/140076
uint64_t RandUint64()
{
  uint64_t number;
  RandBytes(&number, sizeof(number));
  return number;
}

std::string GenerateGUID()
{
  uint64_t sixteen_bytes[2] = {RandUint64(), RandUint64()};
  return RandomDataToGUIDString(sixteen_bytes);
}

bool IsValidGUID(const string &guid)
{
  static const uint64_t kGUIDLength = 36U;
  if (guid.length() != kGUIDLength)
    return false;

  const string hexchars = "0123456789ABCDEF";
  for (uint32_t i = 0; i < guid.length(); ++i)
  {
    char current = guid[i];
    if (i == 8 || i == 13 || i == 18 || i == 23)
    {
      if (current != '-')
        return false;
    }
    else
    {
      if (hexchars.find(current) == string::npos)
        return false;
    }
  }

  return true;
}

// TODO(cmasone): Once we're comfortable this works, migrate Windows code to
// use this as well.
string RandomDataToGUIDString(const uint64_t bytes[2])
{
  return StringFormat("%08X-%04X-%04X-%04X-%012llX",
                      static_cast<unsigned int>(bytes[0] >> 32),
                      static_cast<unsigned int>((bytes[0] >> 16) & 0x0000ffff),
                      static_cast<unsigned int>(bytes[0] & 0x0000ffff),
                      static_cast<unsigned int>(bytes[1] >> 48),
                      bytes[1] & 0x0000ffffffffffffULL);
}

} // namespace util
} // namespace mycc
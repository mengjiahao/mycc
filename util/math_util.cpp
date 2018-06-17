
#include "math_util.h"

namespace mycc
{
namespace util
{

uint64_t Count1BitsOfInt64(uint64_t i)
{
  uint64_t count = 0;
  while (i)
  {
    i = i & (i - 1);
    ++count;
  }
  return count;
}

} // namespace util
} // namespace mycc
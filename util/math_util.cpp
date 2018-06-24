
#include "math_util.h"

namespace mycc
{
namespace util
{

uint64_t Count1Bit(uint64_t x)
{
    uint64_t ret = 0;
    while (x) {
        x >>= 1;
        ++ret;
    }
    return ret;
}

} // namespace util
} // namespace mycc
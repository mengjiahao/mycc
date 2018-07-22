
#ifndef MYCC_UTIL_NET_UTIL_H_
#define MYCC_UTIL_NET_UTIL_H_

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <unistd.h>
#include "types_util.h"

#include <linux/unistd.h>

namespace mycc
{
namespace util
{

static const uint32_t kMaxHostNameSize = 255;
static const string kUnknownIpAddr = "255.255.255.255:0000";

uint32_t GetLocalIPInt(const char *dev_name);
bool IsLocalIP(uint32_t ip, bool loopSkip = true);
uint32_t IPStringToInt(const char *ip);
string IPIntToString(uint64_t ipport);
uint64_t IPStringPortToInt(const char *ip, uint64_t port);
uint64_t IPIntPortToInt(uint32_t ip, uint64_t port);
string GetLocalHostName();
unsigned short PickupAvailablePort();
bool IsPortAvailable(int *port, bool is_tcp);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_NET_UTIL_H_
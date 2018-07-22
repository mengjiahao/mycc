
#include "net_util.h"
#include <stdio.h>

namespace mycc
{
namespace util
{

uint32_t GetLocalIPInt(const char *dev_name)
{
  int fd, intrface;
  struct ifreq buf[16];
  struct ifconf ifc;

  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) <= 0)
  {
    return 0;
  }

  ifc.ifc_len = sizeof(buf);
  ifc.ifc_buf = (caddr_t)buf;
  if (ioctl(fd, SIOCGIFCONF, (char *)&ifc))
  {
    close(fd);
    return 0;
  }

  intrface = ifc.ifc_len / sizeof(struct ifreq);
  while (intrface-- > 0)
  {
    if (ioctl(fd, SIOCGIFFLAGS, (char *)&buf[intrface]))
    {
      continue;
    }
    if (buf[intrface].ifr_flags & IFF_LOOPBACK)
      continue;
    if (!(buf[intrface].ifr_flags & IFF_UP))
      continue;
    if (dev_name != NULL && strcmp(dev_name, buf[intrface].ifr_name))
      continue;
    if (!(ioctl(fd, SIOCGIFADDR, (char *)&buf[intrface])))
    {
      close(fd);
      return ((struct sockaddr_in *)(&buf[intrface].ifr_addr))->sin_addr.s_addr;
    }
  }
  close(fd);
  return 0;
}

bool IsLocalIP(uint32_t ip, bool loopSkip)
{
  int fd, intrface;
  struct ifreq buf[16];
  struct ifconf ifc;

  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) <= 0)
  {
    return false;
  }

  ifc.ifc_len = sizeof(buf);
  ifc.ifc_buf = (caddr_t)buf;
  if (ioctl(fd, SIOCGIFCONF, (char *)&ifc))
  {
    close(fd);
    return false;
  }

  intrface = ifc.ifc_len / sizeof(struct ifreq);
  while (intrface-- > 0)
  {
    if (ioctl(fd, SIOCGIFFLAGS, (char *)&buf[intrface]))
    {
      continue;
    }
    if (loopSkip && buf[intrface].ifr_flags & IFF_LOOPBACK)
      continue;
    if (!(buf[intrface].ifr_flags & IFF_UP))
      continue;
    if (ioctl(fd, SIOCGIFADDR, (char *)&buf[intrface]))
    {
      continue;
    }
    if (((struct sockaddr_in *)(&buf[intrface].ifr_addr))->sin_addr.s_addr == ip)
    {
      close(fd);
      return true;
    }
  }
  close(fd);
  return false;
}

uint32_t IPStringToInt(const char *ip)
{
  if (ip == NULL)
    return 0;
  uint32_t x = inet_addr(ip);
  if (x == (uint32_t)INADDR_NONE)
  {
    struct hostent *hp;
    if ((hp = gethostbyname(ip)) == NULL)
    {
      return 0;
    }
    x = ((struct in_addr *)hp->h_addr)->s_addr;
  }
  return x;
}

string IPIntToString(uint64_t ipport)
{
  char str[32];
  uint32_t ip = (uint32_t)(ipport & 0xffffffff);
  int32_t port = (int32_t)((ipport >> 32) & 0xffff);
  unsigned char *bytes = (unsigned char *)&ip;
  if (port > 0)
  {
    sprintf(str, "%d.%d.%d.%d:%d", bytes[0], bytes[1], bytes[2], bytes[3], port);
  }
  else
  {
    sprintf(str, "%d.%d.%d.%d", bytes[0], bytes[1], bytes[2], bytes[3]);
  }
  return str;
}

uint64_t IPStringPortToInt(const char *ip, uint64_t port)
{
  uint32_t nip = 0;
  const char *p = strchr(ip, ':');
  if (p != NULL && p > ip)
  {
    int32_t len = p - ip;
    if (len > 64)
      len = 64;
    char tmp[128];
    strncpy(tmp, ip, len);
    tmp[len] = '\0';
    nip = IPStringToInt(tmp);
    port = atoi(p + 1);
  }
  else
  {
    nip = IPStringToInt(ip);
  }
  if (nip == 0)
  {
    return 0;
  }
  uint64_t ipport = port;
  ipport <<= 32;
  ipport |= nip;
  return ipport;
}

uint64_t IPIntPortToInt(uint32_t ip, uint64_t port)
{
  uint64_t ipport = port;
  ipport <<= 32;
  ipport |= ip;
  return ipport;
}

string GetLocalHostName()
{
  char str[kMaxHostNameSize + 1];
  if (0 != gethostname(str, kMaxHostNameSize + 1))
  {
    return "UnknownHostName";
  }
  string hostname(str);
  return hostname;
}

// return an available port on local machine
//    only support IPv4
//    return 0 on failure
unsigned short PickupAvailablePort()
{
  struct sockaddr_in addr;
  addr.sin_port = htons(0);                 // have system pick up a random port available for me
  addr.sin_family = AF_INET;                // IPV4
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // set our addr to any interface

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (0 != bind(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)))
  {
    perror("bind():");
    return 0;
  }

  socklen_t addr_len = sizeof(struct sockaddr_in);
  if (0 != getsockname(sock, (struct sockaddr *)&addr, &addr_len))
  {
    perror("getsockname():");
    return 0;
  }

  unsigned short ret_port = ntohs(addr.sin_port);
  close(sock);
  return ret_port;
}

bool IsPortAvailable(int *port, bool is_tcp)
{
  const int protocol = is_tcp ? IPPROTO_TCP : 0;
  const int fd = socket(AF_INET, is_tcp ? SOCK_STREAM : SOCK_DGRAM, protocol);

  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  int actual_port;

  //CHECK_GE(*port, 0);
  //CHECK_LE(*port, 65535);
  if (fd < 0)
  {
    //LOG(ERROR) << "socket() failed: " << strerror(errno);
    return false;
  }

  // SO_REUSEADDR lets us start up a server immediately after it exists.
  int one = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
  {
    //LOG(ERROR) << "setsockopt() failed: " << strerror(errno);
    close(fd);
    return false;
  }

  // Try binding to port.
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(static_cast<uint16_t>(*port));
  if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
  {
    //LOG(WARNING) << "bind(port=" << *port << ") failed: " << strerror(errno);
    close(fd);
    return false;
  }

  // Get the bound port number.
  if (getsockname(fd, reinterpret_cast<struct sockaddr *>(&addr), &addr_len) <
      0)
  {
    //LOG(WARNING) << "getsockname() failed: " << strerror(errno);
    close(fd);
    return false;
  }
  //CHECK_LE(addr_len, sizeof(addr));
  actual_port = ntohs(addr.sin_port);
  //CHECK_GT(actual_port, 0);
  if (*port == 0)
  {
    *port = actual_port;
  }
  else
  {
    //CHECK_EQ(*port, actual_port);
  }
  close(fd);
  return true;
}

} // namespace util
} // namespace mycc
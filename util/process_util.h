
#ifndef MYCC_UTIL_PROCESS_UTIL_H_
#define MYCC_UTIL_PROCESS_UTIL_H_

#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mycc
{
namespace util
{

class CProcess
{
public:
  // return pid
  static int startDaemon(const char *szPidFile, const char *szLogFile);
  static int existPid(const char *szPidFile);
  static void writePidFile(const char *szPidFile);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_PROCESS_UTIL_H_
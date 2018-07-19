
#include "os_util.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sstream>
#include <vector>

#include <linux/limits.h> // PATH_MAX

namespace mycc
{
namespace util
{

// /proc/pid/stat字段定义
struct pid_stat_fields
{
  int64_t pid;
  char comm[PATH_MAX];
  char state;
  int64_t ppid;
  int64_t pgrp;
  int64_t session;
  int64_t tty_nr;
  int64_t tpgid;
  int64_t flags;
  int64_t minflt;
  int64_t cminflt;
  int64_t majflt;
  int64_t cmajflt;
  int64_t utime;
  int64_t stime;
  int64_t cutime;
  int64_t cstime;
  // ...
};

// /proc/stat/cpu信息字段定义
struct cpu_stat_fields
{
  char cpu_label[16];
  int64_t user;
  int64_t nice;
  int64_t system;
  int64_t idle;
  int64_t iowait;
  int64_t irq;
  int64_t softirq;
  // ...
};

string RunShellCmd(const char *cmd, ...)
{
  std::vector<const char *> arr;
  va_list ap;
  va_start(ap, cmd);
  const char *c = cmd;
  do
  {
    arr.push_back(c);
    c = va_arg(ap, const char *);
  } while (c != NULL);
  va_end(ap);
  arr.push_back(NULL);

  int fret = fork();
  if (fret == -1)
  {
    int err = errno;
    std::ostringstream oss;
    oss << "RunShellCmd(" << cmd << "): unable to fork(): " << strerror(err);
    return oss.str();
  }
  else if (fret == 0)
  {
    // execvp doesn't modify its arguments, so the const-cast here is safe.
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    execvp(cmd, (char *const *)&arr[0]);
    _exit(127);
  }
  int status;
  while (waitpid(fret, &status, 0) == -1)
  {
    int err = errno;
    if (err == EINTR)
      continue;
    std::ostringstream oss;
    oss << "RunShellCmd(" << cmd << "): waitpid error: "
        << strerror(err);
    return oss.str();
  }
  if (WIFEXITED(status))
  {
    int wexitstatus = WEXITSTATUS(status);
    if (wexitstatus != 0)
    {
      std::ostringstream oss;
      oss << "RunShellCmd(" << cmd << "): exited with status " << wexitstatus;
      return oss.str();
    }
    return "";
  }
  else if (WIFSIGNALED(status))
  {
    std::ostringstream oss;
    oss << "RunShellCmd(" << cmd << "): terminated by signal";
    return oss.str();
  }
  std::ostringstream oss;
  oss << "RunShellCmd(" << cmd << "): terminated by unknown mechanism";
  return oss.str();
}

bool PopenCmd(const string cmd, string *ret_str)
{
  char output_buffer[80];
  FILE *fp = popen(cmd.c_str(), "r");
  if (!fp)
  {
    fprintf(stderr, "fail to execute cmd: %s\n", cmd.c_str());
    return false;
  }
  fgets(output_buffer, sizeof(output_buffer), fp);
  pclose(fp);
  if (ret_str)
  {
    *ret_str = string(output_buffer);
  }
  return true;
}

void Crash(const string &srcfile, int32_t srcline)
{
  fprintf(stdout, "Crashing at %s:%d\n", srcfile.c_str(), srcline);
  fflush(stdout);
  kill(getpid(), SIGTERM);
}

Status LoadLibrary(const char *library_filename, void **handle)
{
  *handle = dlopen(library_filename, RTLD_NOW | RTLD_LOCAL);
  if (!*handle)
  {
    return Status::Error(dlerror());
  }
  return Status::OK();
}

Status GetSymbolFromLibrary(void *handle, const char *symbol_name, void **symbol)
{
  *symbol = dlsym(handle, symbol_name);
  if (!*symbol)
  {
    return Status::Error(dlerror());
  }
  return Status::OK();
}

string FormatLibraryFileName(const string &name, const string &version)
{
  string filename;
  if (version.empty())
  {
    filename = "lib" + name + ".so";
  }
  else
  {
    filename = "lib" + name + ".so" + "." + version;
  }
  return filename;
}

string GetSelfExeName()
{
  char path[64] = {0};
  char link[PATH_MAX] = {0};

  snprintf(path, sizeof(path), "/proc/%d/exe", getpid());
  readlink(path, link, sizeof(link));

  string filename(strrchr(link, '/') + 1);

  return filename;
}

string GetCurrentLocationDir()
{
  char current_path[1024] = {'\0'};
  string current_dir;

  if (getcwd(current_path, 1024))
  {
    current_dir = current_path;
  }
  return current_dir;
}

int64_t GetCurCpuTime()
{
  char file_name[64] = {0};
  snprintf(file_name, sizeof(file_name), "/proc/%d/stat", getpid());

  FILE *pid_stat = fopen(file_name, "r");
  if (!pid_stat)
  {
    return -1;
  }

  pid_stat_fields result;
  int ret = fscanf(pid_stat, "%ld %s %c %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
                   &result.pid, result.comm, &result.state, &result.ppid, &result.pgrp, &result.session,
                   &result.tty_nr, &result.tpgid, &result.flags, &result.minflt, &result.cminflt,
                   &result.majflt, &result.cmajflt, &result.utime, &result.stime, &result.cutime, &result.cstime);

  fclose(pid_stat);

  if (ret <= 0)
  {
    return -1;
  }

  return result.utime + result.stime + result.cutime + result.cstime;
}

int64_t GetTotalCpuTime()
{
  char file_name[] = "/proc/stat";
  FILE *stat = fopen(file_name, "r");
  if (!stat)
  {
    return -1;
  }

  cpu_stat_fields result;
  int ret = fscanf(stat, "%s %ld %ld %ld %ld %ld %ld %ld",
                   result.cpu_label, &result.user, &result.nice, &result.system, &result.idle,
                   &result.iowait, &result.irq, &result.softirq);

  fclose(stat);

  if (ret <= 0)
  {
    return -1;
  }

  return result.user + result.nice + result.system + result.idle +
         result.iowait + result.irq + result.softirq;
}

float CalculateCurCpuUseage(int64_t cur_cpu_time_start, int64_t cur_cpu_time_stop,
                            int64_t total_cpu_time_start, int64_t total_cpu_time_stop)
{
  int64_t cpu_result = total_cpu_time_stop - total_cpu_time_start;
  if (cpu_result <= 0)
  {
    return 0;
  }

  return (sysconf(_SC_NPROCESSORS_ONLN) * 100.0f * (cur_cpu_time_stop - cur_cpu_time_start)) / cpu_result;
}

int GetCurMemoryUsage(int *vm_size_kb, int *rss_size_kb)
{
  if (!vm_size_kb || !rss_size_kb)
  {
    return -1;
  }

  char file_name[64] = {0};
  snprintf(file_name, sizeof(file_name), "/proc/%d/status", getpid());

  FILE *pid_status = fopen(file_name, "r");
  if (!pid_status)
  {
    return -1;
  }

  int ret = 0;
  char line[256] = {0};
  char tmp[32] = {0};
  fseek(pid_status, 0, SEEK_SET);
  for (int i = 0; i < 16; i++)
  {
    if (fgets(line, sizeof(line), pid_status) == NULL)
    {
      ret = -2;
      break;
    }

    if (strstr(line, "VmSize") != NULL)
    {
      sscanf(line, "%s %d", tmp, vm_size_kb);
    }
    else if (strstr(line, "VmRSS") != NULL)
    {
      sscanf(line, "%s %d", tmp, rss_size_kb);
    }
  }

  fclose(pid_status);
  return ret;
}

int64_t GetMaxOpenFiles()
{
  struct rlimit no_files_limit;
  if (getrlimit(RLIMIT_NOFILE, &no_files_limit) != 0)
  {
    return -1;
  }
  // protect against overflow
  if (no_files_limit.rlim_cur >= std::numeric_limits<int64_t>::max())
  {
    return std::numeric_limits<int64_t>::max();
  }
  return static_cast<int64_t>(no_files_limit.rlim_cur);
}

int64_t AmountOfMemory(int pages_name)
{
  long pages = sysconf(pages_name);
  long page_size = sysconf(_SC_PAGESIZE);
  if (pages == -1 || page_size == -1)
  {
    return 0;
  }
  return static_cast<int64_t>(pages) * page_size;
}

int64_t AmountOfPhysicalMemory()
{
  return AmountOfMemory(_SC_PHYS_PAGES);
}

int64_t AmountOfVirtualMemory()
{
  struct rlimit limit;
  int result = getrlimit(RLIMIT_DATA, &limit);
  if (result != 0)
  {
    return 0;
  }
  return limit.rlim_cur == RLIM_INFINITY ? 0 : limit.rlim_cur;
}

int NumberOfProcessors()
{
  // It seems that sysconf returns the number of "logical" processors on both
  // Mac and Linux.  So we get the number of "online logical" processors.
  long res = sysconf(_SC_NPROCESSORS_ONLN);
  if (res == -1)
  {
    return 1;
  }
  return static_cast<int>(res);
}

} // namespace util
} // namespace mycc
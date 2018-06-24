
#include "cmd_util.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

Status LoadLibrary(const char* library_filename, void** handle) {
  *handle = dlopen(library_filename, RTLD_NOW | RTLD_LOCAL);
  if (!*handle) {
    return Status::Error(dlerror());
  }
  return Status::OK();
}

Status GetSymbolFromLibrary(void* handle, const char* symbol_name,
                            void** symbol) {
  *symbol = dlsym(handle, symbol_name);
  if (!*symbol) {
    return Status::Error(dlerror());
  }
  return Status::OK();
}

string FormatLibraryFileName(const string& name, const string& version) {
  string filename;
  if (version.empty()) {
    filename = "lib" + name + ".so";
  } else {
    filename = "lib" + name + ".so" + "." + version;
  }
  return filename;
}

} // namespace util
} // namespace mycc
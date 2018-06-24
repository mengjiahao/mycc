
#ifndef MYCC_UTIL_CMD_UTIL_H_
#define MYCC_UTIL_CMD_UTIL_H_

#include <unistd.h>
#include "types_util.h"
#include "status.h"

namespace mycc
{
namespace util
{

//
// Fork a command and run it. The shell will not be invoked and shell
// expansions will not be done.
// This function takes a variable number of arguments. The last argument must
// be NULL.
//
// Example:
//   RunShellCmd("rm", "-rf", "foo", NULL)
//
// Returns an empty string on success, and an error string otherwise.
//
string RunShellCmd(const char *cmd, ...);

bool PopenCmd(const string cmd, string *ret_str);

string GetSelfExeName();

string GetCurrentLocationDir();

Status LoadLibrary(const char *library_filename, void **handle);
Status GetSymbolFromLibrary(void *handle, const char *symbol_name,
                            void **symbol);
string FormatLibraryFileName(const string &name, const string &version);

int64_t GetCurCpuTime();

int64_t GetTotalCpuTime();

float CalculateCurCpuUseage(int64_t cur_cpu_time_start, int64_t cur_cpu_time_stop,
                            int64_t total_cpu_time_start, int64_t total_cpu_time_stop);

int GetCurMemoryUsage(int *vm_size_kb, int *rss_size_kb);

void Crash(const string &srcfile, int32_t srcline);

int64_t GetMaxOpenFiles();

// Return the number of bytes of physical memory on the current machine.
extern int64_t AmountOfPhysicalMemory();

// Return the number of bytes of virtual memory of this process. A return
// value of zero means that there is no limit on the available virtual memory.
extern int64_t AmountOfVirtualMemory();

// Return the number of logical processors/cores on the current machine.
int NumberOfProcessors();

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_CMD_UTIL_H_
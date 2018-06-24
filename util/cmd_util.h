
#ifndef MYCC_UTIL_CMD_UTIL_H_
#define MYCC_UTIL_CMD_UTIL_H_

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

Status LoadLibrary(const char* library_filename, void** handle);
Status GetSymbolFromLibrary(void* handle, const char* symbol_name,
                            void** symbol);
string FormatLibraryFileName(const string& name, const string& version);

} // namespace myceph
} // namespace util

#endif // MYCC_UTIL_CMD_UTIL_H_
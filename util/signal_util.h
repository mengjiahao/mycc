
#ifndef MYCC_UTIL_SIGNAL_UTIL_H_
#define MYCC_UTIL_SIGNAL_UTIL_H_

#include <signal.h>
#include "types_util.h"

namespace mycc
{ // namespace mycc
namespace util
{ // namespace util

string SignalMaskToStr();
void RestoreSigset(const sigset_t *old_sigset);
void BlockSignals(const int *siglist, sigset_t *old_sigset);
void UnblockAllSignals(sigset_t *old_sigset);
void HookupSignalHandler();
void UnhookSignalHandler();

int32_t GetStackTrace(void **result, int32_t max_depth, int32_t skip_num);
void FatalSignalHandler(int signum, siginfo_t *siginfo, void *ucontext);
void InstallSignalHandler();

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SIGNAL_UTIL_H_
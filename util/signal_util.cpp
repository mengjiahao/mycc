
#include "signal_util.h"
#include <stdio.h>
#include <atomic>
#include <sstream>
#include "error_util.h"
#include "time_util.h"

namespace mycc
{ // namespace mycc
namespace util
{ // namespace util

struct sigaction previousSighup;
struct sigaction previousSigint;
std::atomic<int> sigintCount(0);
std::atomic<int> sighupCount(0);
std::atomic<int> hookedUpCount(0);

static void handleSignal(int signal)
{
  switch (signal)
  {
  // TODO: what if the previous handler uses sa_sigaction?
  case SIGHUP:
    sighupCount += 1;
    if (previousSighup.sa_handler)
    {
      previousSighup.sa_handler(signal);
    }
    break;
  case SIGINT:
    sigintCount += 1;
    if (previousSigint.sa_handler)
    {
      previousSigint.sa_handler(signal);
    }
    break;
  }
}

string SignalMaskToStr()
{
  sigset_t old_sigset;
  if (pthread_sigmask(SIG_SETMASK, NULL, &old_sigset))
  {
    return "(pthread_signmask failed)";
  }

  std::ostringstream oss;
  oss << "show_signal_mask: { ";
  string sep("");
  for (int32_t signum = 0; signum < NSIG; ++signum)
  {
    if (sigismember(&old_sigset, signum) == 1)
    {
      oss << sep << signum;
      sep = ", ";
    }
  }
  oss << " }";
  return oss.str();
}

void RestoreSigset(const sigset_t *old_sigset)
{
  int ret = pthread_sigmask(SIG_SETMASK, old_sigset, NULL);
  assert(ret == 0);
}

/* Block the signals in 'siglist'. If siglist == NULL, block all signals. */
void BlockSignals(const int *siglist, sigset_t *old_sigset)
{
  sigset_t sigset;
  if (!siglist)
  {
    sigfillset(&sigset);
  }
  else
  {
    int i = 0;
    sigemptyset(&sigset);
    while (siglist[i])
    {
      sigaddset(&sigset, siglist[i]);
      ++i;
    }
  }
  int ret = pthread_sigmask(SIG_BLOCK, &sigset, old_sigset);
  assert(ret == 0);
}

void UnblockAllSignals(sigset_t *old_sigset)
{
  sigset_t sigset;
  sigfillset(&sigset);
  sigdelset(&sigset, SIGKILL);
  int ret = pthread_sigmask(SIG_UNBLOCK, &sigset, old_sigset);
  assert(ret == 0);
}

void HookupSignalHandler()
{
  if (hookedUpCount++)
  {
    return;
  }
  struct sigaction sa;
  // Setup the handler
  sa.sa_handler = &handleSignal;
  // Restart the system call, if at all possible
  sa.sa_flags = SA_RESTART;
  // Block every signal during the handler
  sigfillset(&sa.sa_mask);
  // Intercept SIGHUP and SIGINT
  if (sigaction(SIGHUP, &sa, &previousSighup) == -1)
  {
    PANIC("Cannot install SIGHUP handler.");
  }
  if (sigaction(SIGINT, &sa, &previousSigint) == -1)
  {
    PANIC("Cannot install SIGINT handler.");
  }
}

// Set the signal handlers to the default.
void UnhookSignalHandler()
{
  if (--hookedUpCount > 0)
  {
    return;
  }
  struct sigaction sa;
  // Setup the sighub handler
  sa.sa_handler = SIG_DFL;
  // Restart the system call, if at all possible
  sa.sa_flags = SA_RESTART;
  // Block every signal during the handler
  sigfillset(&sa.sa_mask);
  // Intercept SIGHUP and SIGINT
  if (sigaction(SIGHUP, &previousSighup, nullptr) == -1)
  {
    PANIC("Cannot uninstall SIGHUP handler.");
  }
  if (sigaction(SIGINT, &previousSigint, nullptr) == -1)
  {
    PANIC("Cannot uninstall SIGINT handler.");
  }
}

static const struct
{
  int number;
  const char *name;
} g_failure_signals[] = {
    {SIGSEGV, "SIGSEGV"},
    {SIGILL, "SIGILL"},
    {SIGFPE, "SIGFPE"},
    {SIGABRT, "SIGABRT"},
    {SIGBUS, "SIGBUS"},
    {SIGTERM, "SIGTERM"}};

static struct sigaction g_sigaction_bak[ARRAYSIZE_UNSAFE(g_failure_signals)];

int32_t GetStackTrace(void **result, int32_t max_depth, int32_t skip_num)
{
  static const int32_t stack_len = 64;
  void *stack[stack_len] = {0};

  int32_t size = backtrace(stack, stack_len);

  int32_t remain = size - (++skip_num);
  if (remain < 0)
  {
    remain = 0;
  }
  if (remain > max_depth)
  {
    remain = max_depth;
  }

  for (int32_t i = 0; i < remain; i++)
  {
    result[i] = stack[i + skip_num];
  }

  return remain;
}

void FatalSignalHandler(int signum, siginfo_t *siginfo, void *ucontext)
{
  std::ostringstream oss;
  oss << "[" << CurrentSystimeString() << "]";

  const char *signame = "";
  uint32_t i = 0;
  for (; i < ARRAYSIZE_UNSAFE(g_failure_signals); i++)
  {
    if (g_failure_signals[i].number == signum)
    {
      signame = g_failure_signals[i].name;
      break;
    }
  }
  oss << "[(SIGNAL)(" << signame << ")][FATAL]";

  void *stack[32] = {0};
  int32_t depth = GetStackTrace(stack, ARRAYSIZE_UNSAFE(stack), 1);
  char **symbols = backtrace_symbols(stack, depth);

  oss << "\nstack trace:\n";
  for (int32_t i = 0; i < depth; i++)
  {
    oss << "#" << i << " " << symbols[i] << "\n";
  }
  oss << "\n";

  fprintf(stderr, "%s\n", oss.str().c_str());

  // restore
  sigaction(signum, &g_sigaction_bak[i], NULL);
  kill(getpid(), signum);
}

void InstallSignalHandler()
{
  static bool _installed = false;
  if (_installed)
  {
    return;
  }
  _installed = true;

  struct sigaction sig_action;
  memset(&sig_action, 0, sizeof(sig_action));
  sigemptyset(&sig_action.sa_mask);
  sig_action.sa_flags |= SA_SIGINFO;
  sig_action.sa_sigaction = &FatalSignalHandler;

  for (uint32_t i = 0; i < ARRAYSIZE_UNSAFE(g_failure_signals); i++)
  {
    sigaction(g_failure_signals[i].number, &sig_action, &g_sigaction_bak[i]);
  }
}

} // namespace util
} // namespace mycc
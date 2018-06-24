
#include "signal_util.h"
#include <atomic>
#include <sstream>
#include "error_util.h"

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
  int32_t ret = pthread_sigmask(SIG_SETMASK, old_sigset, NULL);
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
  int32_t ret = pthread_sigmask(SIG_BLOCK, &sigset, old_sigset);
  assert(ret == 0);
}

void UnblockAllSignals(sigset_t *old_sigset)
{
  sigset_t sigset;
  sigfillset(&sigset);
  sigdelset(&sigset, SIGKILL);
  int32_t ret = pthread_sigmask(SIG_UNBLOCK, &sigset, old_sigset);
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

} // namespace util
} // namespace mycc
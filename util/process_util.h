
#ifndef MYCC_UTIL_PROCESS_UTIL_H_
#define MYCC_UTIL_PROCESS_UTIL_H_

#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mutex>
#include <vector>
#include "types_util.h"

namespace mycc
{
namespace util
{

class CProcess
{
public:
  // return pid
  static int StartDaemon(const char *szPidFile, const char *szLogFile);
  static int ExistPid(const char *szPidFile);
  static void WritePidFile(const char *szPidFile);
  pid_t Pid();
  string PidString();
  uid_t Uid();
  string Username();
  uid_t Euid();
  string ExePath();
  int OpenedFiles();
  int MaxOpenFiles();
  string ProcStatus();
  string ProcStat();
  int NumThreads();
  std::vector<pid_t> ThreadPids();
};

// Supports spawning and killing child processes.

class SubProcess
{
public:
  // Channel identifiers.
  enum Channel
  {
    CHAN_STDIN = 0,
    CHAN_STDOUT = 1,
    CHAN_STDERR = 2,
  };

  // Specify how a channel is handled.
  enum ChannelAction
  {
    // Close the file descriptor when the process starts.
    // This is the default behavior.
    ACTION_CLOSE,

    // Make a pipe to the channel.  It is used in the Communicate() method to
    // transfer data between the parent and child processes.
    ACTION_PIPE,

    // Duplicate the parent's file descriptor. Useful if stdout/stderr should
    // go to the same place that the parent writes it.
    ACTION_DUPPARENT,
  };

  // SubProcess()
  //    nfds: The number of file descriptors to use.
  explicit SubProcess(int nfds = 3);

  // Virtual for backwards compatibility; do not create new subclasses.
  // It is illegal to delete the SubProcess within its exit callback.
  virtual ~SubProcess();

  // SetChannelAction()
  //    Set how to handle a channel.  The default action is ACTION_CLOSE.
  //    The action is set for all subsequent processes, until SetChannel()
  //    is called again.
  //
  //    SetChannel may not be called while the process is running.
  //
  //    chan: Which channel this applies to.
  //    action: What to do with the channel.
  // Virtual for backwards compatibility; do not create new subclasses.
  virtual void SetChannelAction(Channel chan, ChannelAction action);

  // SetProgram()
  //    Set up a program and argument list for execution, with the full
  //    "raw" argument list passed as a vector of strings.  argv[0]
  //    should be the program name, just as in execv().
  //
  //    file: The file containing the program.  This must be an absolute path
  //          name - $PATH is not searched.
  //    argv: The argument list.
  virtual void SetProgram(const string &file, const std::vector<string> &argv);

  // Start()
  //    Run the command that was previously set up with SetProgram().
  //    The following are fatal programming errors:
  //       * Attempting to start when a process is already running.
  //       * Attempting to start without first setting the command.
  //    Note, however, that Start() does not try to validate that the binary
  //    does anything reasonable (e.g. exists or can execute); as such, you can
  //    specify a non-existent binary and Start() will still return true.  You
  //    will get a failure from the process, but only after Start() returns.
  //
  //    Return true normally, or false if the program couldn't be started
  //    because of some error.
  // Virtual for backwards compatibility; do not create new subclasses.
  virtual bool Start();

  // Kill()
  //    Send the given signal to the process.
  //    Return true normally, or false if we couldn't send the signal - likely
  //    because the process doesn't exist.
  virtual bool Kill(int signal);

  // Wait()
  //    Block until the process exits.
  //    Return true normally, or false if the process wasn't running.
  virtual bool Wait();

  // Communicate()
  //    Read from stdout and stderr and writes to stdin until all pipes have
  //    closed, then waits for the process to exit.
  //    Note: Do NOT call Wait() after calling Communicate as it will always
  //     fail, since Communicate calls Wait() internally.
  //    'stdin_input', 'stdout_output', and 'stderr_output' may be NULL.
  //    If this process is not configured to send stdout or stderr to pipes,
  //     the output strings will not be modified.
  //    If this process is not configured to take stdin from a pipe, stdin_input
  //     will be ignored.
  //    Returns the command's exit status.
  virtual int Communicate(const string *stdin_input, string *stdout_output,
                          string *stderr_output);

private:
  static const int kNFds = 3;
  static bool chan_valid(int chan) { return ((chan >= 0) && (chan < kNFds)); }
  static bool retry(int e)
  {
    return ((e == EINTR) || (e == EAGAIN) || (e == EWOULDBLOCK));
  }
  void FreeArgs();
  void ClosePipes();
  bool WaitInternal(int *status);

  // The separation between proc_mu_ and data_mu_ mutexes allows Kill() to be
  // called by a thread while another thread is inside Wait() or Communicate().
  mutable std::mutex proc_mu_;
  bool running_;
  pid_t pid_;

  mutable std::mutex data_mu_;
  char *exec_path_;
  char **exec_argv_;
  ChannelAction action_[kNFds];
  int parent_pipe_[kNFds];
  int child_pipe_[kNFds];

  DISALLOW_COPY_AND_ASSIGN(SubProcess);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_PROCESS_UTIL_H_

#include "exit_util.h"
#include "error_util.h"

namespace mycc {
namespace util {

// Keep a stack of registered AtExitManagers.  We always operate on the most
// recent, and we should never have more than one outside of testing (for a
// statically linked version of this library).  Testing may use the shadow
// version of the constructor, and if we are building a dynamic library we may
// end up with multiple AtExitManagers on the same process.  We don't protect
// this for thread-safe access, since it will only be modified in testing.
static AtExitManager* g_top_manager = NULL;

AtExitManager::AtExitManager() : next_manager_(g_top_manager) {
// If multiple modules instantiate AtExitManagers they'll end up living in this
// module... they have to coexist.
  assert(!g_top_manager);
  g_top_manager = this;
}

AtExitManager::~AtExitManager() {
  if (!g_top_manager) {
    PRINT_ERROR("%s\n", "Tried to ~AtExitManager without an AtExitManager");
    return;
  }
  assert(this == g_top_manager);

  ProcessCallbacksNow();
  g_top_manager = next_manager_;
}

// static
void AtExitManager::RegisterCallback(AtExitCallbackType func, void* param) {
  assert(func);
  if (!g_top_manager) {
    PRINT_ERROR("%s\n", "Tried to RegisterCallback without an AtExitManager");
    return;
  }

  MutexLock lock(&(g_top_manager->mu_));
  g_top_manager->stack_.push({func, param});
}

// static
void AtExitManager::ProcessCallbacksNow() {
  if (!g_top_manager) {
    PRINT_ERROR("%s\n", "Tried to ProcessCallbacksNow without an AtExitManager");
    return;
  }

  MutexLock lock(&(g_top_manager->mu_));

  while (!g_top_manager->stack_.empty()) {
    Callback task = g_top_manager->stack_.top();
    task.func(task.param);
    g_top_manager->stack_.pop();
  }
}

AtExitManager::AtExitManager(bool shadow) : next_manager_(g_top_manager) {
  assert(shadow || !g_top_manager);
  g_top_manager = this;
}

}  
}
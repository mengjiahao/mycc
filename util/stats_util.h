
#ifndef MYCC_UTIL_STAS_UTIL_H_
#define MYCC_UTIL_STAS_UTIL_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "types_util.h"

namespace mycc
{
namespace util
{

class StatValue
{
  std::atomic<int64_t> v_{0};

public:
  int64_t increment(int64_t inc)
  {
    return v_ += inc;
  }

  int64_t reset(int64_t value = 0)
  {
    return v_.exchange(value);
  }

  int64_t get() const
  {
    return v_.load();
  }
};

struct ExportedStatValue
{
  string key;
  int64_t value;
  std::chrono::time_point<std::chrono::high_resolution_clock> ts;
};

/**
 * @brief Holds names and values of counters exported from a StatRegistry.
 */
using ExportedStatList = std::vector<ExportedStatValue>;
using ExportedStatMap = std::unordered_map<string, int64_t>;

/**
 * @brief Holds a map of atomic counters keyed by name.
 *
 * The StatRegistry singleton, accessed through StatRegistry::get(), holds
 * counters registered through the macro CAFFE_EXPORTED_STAT. Example of usage:
 *
 * struct MyCaffeClass {
 *   MyCaffeClass(const string& instanceName): stats_(instanceName) {}
 *   void run(int numRuns) {
 *     try {
 *       CAFFE_EVENT(stats_, num_runs, numRuns);
 *       tryRun(numRuns);
 *       CAFFE_EVENT(stats_, num_successes);
 *     } catch (std::exception& e) {
 *       CAFFE_EVENT(stats_, num_failures, 1, "arg_to_usdt", e.what());
 *     }
 *     CAFFE_EVENT(stats_, usdt_only, 1, "arg_to_usdt");
 *   }
 *  private:
 *   struct MyStats {
 *     CAFFE_STAT_CTOR(MyStats);
 *     CAFFE_EXPORTED_STAT(num_runs);
 *     CAFFE_EXPORTED_STAT(num_successes);
 *     CAFFE_EXPORTED_STAT(num_failures);
 *     CAFFE_STAT(usdt_only);
 *   } stats_;
 * };
 *
 * int main() {
 *   MyCaffeClass a("first");
 *   MyCaffeClass b("second");
 *   for (int i = 0; i < 10; ++i) {
 *     a.run(10);
 *     b.run(5);
 *   }
 *   ExportedStatList finalStats;
 *   StatRegistry::get().publish(finalStats);
 * }
 *
 * For every new instance of MyCaffeClass, a new counter is created with
 * the instance name as prefix. Everytime run() is called, the corresponding
 * counter will be incremented by the given value, or 1 if value not provided.
 *
 * Counter values can then be exported into an ExportedStatList. In the
 * example above, considering "tryRun" never throws, `finalStats` will be
 * populated as follows:
 *
 *   first/num_runs       100
 *   first/num_successes   10
 *   first/num_failures     0
 *   second/num_runs       50
 *   second/num_successes  10
 *   second/num_failures    0
 *
 * The event usdt_only is not present in ExportedStatList because it is declared
 * as CAFFE_STAT, which does not create a counter.
 *
 * Additionally, for each call to CAFFE_EVENT, a USDT probe is generated.
 * The probe will be set up with the following arguments:
 *   - Probe name: field name (e.g. "num_runs")
 *   - Arg #0: instance name (e.g. "first", "second")
 *   - Arg #1: For CAFFE_EXPORTED_STAT, value of the updated counter
 *             For CAFFE_STAT, -1 since no counter is available
 *   - Args ...: Arguments passed to CAFFE_EVENT, including update value
 *             when provided.
 *
 * It is also possible to create additional StatRegistry instances beyond
 * the singleton. These instances are not automatically populated with
 * CAFFE_EVENT. Instead, they can be populated from an ExportedStatList
 * structure by calling StatRegistry::update().
 *
 */
class StatRegistry
{
  std::mutex mutex_;
  std::unordered_map<string, std::unique_ptr<StatValue>> stats_;

public:
  /**
   * Retrieve the singleton StatRegistry, which gets populated
   * through the CAFFE_EVENT macro.
   */
  static StatRegistry &get();

  /**
   * Add a new counter with given name. If a counter for this name already
   * exists, returns a pointer to it.
   */
  StatValue *add(const string &name);

  /**
   * Populate an ExportedStatList with current counter values.
   * If `reset` is true, resets all counters to zero. It is guaranteed that no
   * count is lost.
   */
  void publish(ExportedStatList &exported, bool reset = false);

  ExportedStatList publish(bool reset = false)
  {
    ExportedStatList stats;
    publish(stats, reset);
    return stats;
  }

  /**
   * Update values of counters contained in the given ExportedStatList to
   * the values provided, creating counters that don't exist.
   */
  void update(const ExportedStatList &data);

  ~StatRegistry();
};

struct ExportedStat
{
  string groupName;
  string name;
  StatValue *value_;

  ExportedStat(const string &gn, const string &n)
      : groupName(gn), name(n) {}

  int64_t increment(int64_t value = 1)
  {
    return value_->increment(value);
  }

  template <typename T, typename Unused1, typename... Unused>
  int64_t increment(T value, Unused1, Unused...)
  {
    return increment(value);
  }
};

template <class T>
struct StateScopedGuard
{
  T f_;
  std::chrono::high_resolution_clock::time_point start_;

  explicit StateScopedGuard(T f)
      : f_(f), start_(std::chrono::high_resolution_clock::now()) {}
  ~StateScopedGuard()
  {
    using namespace std::chrono;
    auto duration = high_resolution_clock::now() - start_;
    int64_t nanos = duration_cast<nanoseconds>(duration).count();
    f_(nanos);
  }

  // Using implicit cast to bool so that it can be used in an 'if' condition
  // within CAFFE_DURATION macro below.
  /* implicit */
  operator bool()
  {
    return true;
  }
};

#define CAFFE_STAT_CTOR(ClassName)                 \
  ClassName(std::string name) : groupName(name) {} \
  std::string groupName

#define CAFFE_EXPORTED_STAT(name) \
  ExportedStat name               \
  {                               \
    groupName, #name              \
  }

#define CAFFE_STAT(name) \
  Stat name              \
  {                      \
    groupName, #name     \
  }

#define CAFFE_EVENT(stats, field, ...)                              \
  {                                                                 \
    auto __caffe_event_value_ = stats.field.increment(__VA_ARGS__); \
  }

#define CAFFE_DURATION(stats, field, ...)                \
  if (auto g = StateScopedGuard([&](int64_t nanos) {     \
        CAFFE_EVENT(stats, field, nanos, ##__VA_ARGS__); \
      }))

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_STAS_UTIL_H_
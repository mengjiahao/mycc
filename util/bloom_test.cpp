
#include "bloom.h"
#include <atomic>
#include <thread>
#include <vector>
#include "env_util.h"
#include "random_util.h"
#include "testharness.h"
#include "types_util.h"

using namespace std;

namespace mycc
{
namespace util
{

class BloomTest
{
};

Env *g_env = Env::Default();

TEST(BloomTest, simple_test)
{
  Status s;
  string test_filename = "test0.txt";
  BloomFilter bloom1;
  Random64 rad(31);

  uint32_t count = 3;
  bloom1.initialize(64, 1);
  bloom1.add("hello");
  for (uint32_t i = 0; i < count; ++i)
  {
    bloom1.addConcurrently("hello");
  }
  bloom1.add("world");

  PRINT_INFO("step0, count=%d, %s\n", count, bloom1.toString().c_str());
  PANIC_TRUE(bloom1.mayContain("hello"));
  PANIC_TRUE(bloom1.mayContainConcurrently("world"));
  PANIC_TRUE(!bloom1.mayContainConcurrently("x"));

  s = bloom1.dump(test_filename);
  PANIC_ENFORCE(s.ok(), "%s\n", s.toString().c_str());
  PRINT_INFO("step1, count=%d, %s\n", count, bloom1.toString().c_str());

  BloomFilter bloom2;
  s = bloom2.load(test_filename);
  PANIC_ENFORCE(s.ok(), "%s\n", s.toString().c_str());
  PRINT_INFO("step2, count=%d, %s\n", count, bloom2.toString().c_str());
}

NO_TEST(BloomTest, onethread_with_perf)
{
  uint32_t num_probes = 2;
  uint64_t m_limit = 1;

  util::Timer timer, timeronce;
  uint64_t elapsedOnce = 0;
  UNUSED_PARAM(elapsedOnce);
  uint64_t total_count = 0;

  for (uint64_t m = 1; m <= m_limit; ++m)
  {
    const uint64_t num_keys = m * 8 * 1024 * 1024;
    PRINT_INFO("testing %" PRIu64 " keys\n", num_keys);
    PRINT_INFO("------------------------------------\n");

    BloomFilter std_bloom;
    std_bloom.initialize(num_keys * 8, num_probes);
    PRINT_INFO("%s\n", std_bloom.toString().c_str());

    // test #1
    timer.start();
    uint64_t v = 1;
    UNUSED_PARAM(v);
    // add [1, num_keys]
    for (uint64_t i = 1; i <= num_keys; ++i)
    {
      std_bloom.add(StringPiece(reinterpret_cast<const char *>(&i), 8));
    }

    uint64_t elapsed = timer.elapsedNanos();
    PRINT_INFO("#standard bloom, avg add latency %lf nanos/key\n",
               (double)elapsed / num_keys);
    PRINT_INFO("------------------------------------\n");

    // test #2
    timer.start();
    total_count = 0;
    // check [1, num_keys]
    for (uint64_t i = 1; i <= num_keys; ++i)
    {
      if (std_bloom.mayContain(StringPiece(reinterpret_cast<const char *>(&i), 8)))
      {
        ++total_count;
      }
    }
    PANIC_EQ(total_count, num_keys);
    elapsed = timer.elapsedNanos();
    PANIC_GT(total_count, 0);
    PRINT_INFO("#standard bloom, avg hit latency %lf nanos/key\n",
               (double)elapsed / total_count);
    PRINT_INFO("------------------------------------\n");

    // test #3
    timer.start();
    uint32_t false_positives = 0;
    // check (num_keys, 2 * num_keys]
    for (uint64_t i = num_keys + 1; i <= 2 * num_keys; ++i)
    {
      bool f = std_bloom.mayContain(StringPiece(reinterpret_cast<const char *>(&i), 8));
      if (f)
      {
        ++false_positives;
      }
    }

    elapsed = timer.elapsedNanos();
    PRINT_INFO("standard bloom, avg miss latency %lf nanos/key, %f%% false positive rate\n",
               (double)elapsed / num_keys, false_positives * 100.0 / num_keys);

    PRINT_INFO("%s\n", std_bloom.toString().c_str());
  }
}

NO_TEST(BloomTest, multithread_with_perf)
{
  uint32_t num_probes = 2;
  uint64_t m_limit = 6;

  util::Timer timer;
  uint64_t elapsedOnce = 0;
  UNUSED_PARAM(elapsedOnce);

  uint32_t num_threads = 4;
  std::vector<std::thread> threads;

  for (uint64_t m = 1; m <= m_limit; ++m)
  {
    const uint64_t num_keys = m * 1024 * 1024 * 1024;
    PRINT_INFO("testing %" PRIu64 " keys\n", num_keys);
    PRINT_INFO("------------------------------------\n");

    BloomFilter std_bloom;
    std_bloom.initialize(num_keys * 8, num_probes);
    PRINT_INFO("%s\n", std_bloom.toString().c_str());

    // test #1
    timer.start();

    std::function<void(uint64_t)> adder = [&](uint64_t t) {
      // add [1, num_keys]
      uint64_t v = 1;
      UNUSED_PARAM(v);
      uint64_t count = 0;
      util::Timer timer_one;
      timer_one.start();
      for (uint64_t i = 1 + t; i <= num_keys; i += num_threads)
      {
        std_bloom.addConcurrently(
            StringPiece(reinterpret_cast<const char *>(&i), 8));
        ++count;
      }
      uint64_t elapsed_one = timer_one.elapsedNanos();
      PANIC_GT(count, 0);
      PRINT_INFO("#standard bloom, thread[0x%016" PRIx64 "] avg add latency %lf nanos/key, count=%" PRIu64 "\n",
                 g_env->GetStdThreadId(), (double)elapsed_one / count, count);
    };
    for (uint64_t t = 0; t < num_threads; ++t)
    {
      threads.emplace_back(adder, t);
    }
    while (threads.size() > 0)
    {
      threads.back().join();
      threads.pop_back();
    }

    uint64_t elapsed = timer.elapsedNanos();
    PRINT_INFO("standard bloom, avg parallel %u add qps %lf\n",
               num_threads, (double)num_keys * 1000000000 / elapsed);
    PRINT_INFO("------------------------------------\n");

    // test #2
    timer.start();

    std::function<void(uint64_t)> hitter = [&](uint64_t t) {
      // check [1, num_keys]
      uint64_t v = 1;
      UNUSED_PARAM(v);
      uint64_t count = 0;
      util::Timer timer_one;
      timer_one.start();
      for (uint64_t i = 1 + t; i <= num_keys; i += num_threads)
      {
        bool f = std_bloom.mayContainConcurrently(StringPiece(reinterpret_cast<const char *>(&i), 8));
        PANIC_TRUE(f);
        ++count;
      }
      uint64_t elapsed_one = timer_one.elapsedNanos();
      PANIC_GE(count, 0);
      PRINT_INFO("#standard bloom, thread[0x%016" PRIu64 "] avg hit latency %lf nanos/key, count=%" PRIu64 "\n",
                 g_env->GetStdThreadId(), (double)elapsed_one / count, count);
    };
    for (uint64_t t = 0; t < num_threads; ++t)
    {
      threads.emplace_back(hitter, t);
    }
    while (threads.size() > 0)
    {
      threads.back().join();
      threads.pop_back();
    }

    elapsed = timer.elapsedNanos();
    PRINT_INFO("standard bloom, avg parallel %u hit qps %lf\n",
               num_threads, (double)num_keys * 1000000000 / elapsed);
    PRINT_INFO("------------------------------------\n");

    // test #3
    timer.start();

    std::atomic<uint32_t> false_positives(0);
    std::function<void(uint64_t)> misser = [&](uint64_t t) {
      // check (num_keys, 2 * num_keys]
      uint64_t count = 0;
      util::Timer timer_one;
      timer_one.start();
      for (uint64_t i = num_keys + 1 + t; i <= 2 * num_keys;
           i += num_threads)
      {
        bool f = std_bloom.mayContainConcurrently(StringPiece(reinterpret_cast<const char *>(&i), 8));
        if (f)
        {
          ++false_positives;
        }
        ++count;
      }
      uint64_t elapsed_one = timer_one.elapsedNanos();
      PANIC_GT(count, 0);
      PRINT_INFO("#standard bloom, thread[0x%016" PRIu64 "] avg miss latency %lf nanos/key, count=%" PRIu64 "\n",
                 g_env->GetStdThreadId(), (double)elapsed_one / count, count);
    };
    for (uint64_t t = 0; t < num_threads; ++t)
    {
      threads.emplace_back(misser, t);
    }
    while (threads.size() > 0)
    {
      threads.back().join();
      threads.pop_back();
    }

    elapsed = timer.elapsedNanos();
    timer.start();
    uint64_t count_elapsed = timer.elapsedNanos();
    PRINT_INFO("standard bloom, avg parallel %u miss qps %lf, %f%% false positive rate, count_elapsed=%" PRIu64 " nanos\n",
               num_threads, (double)num_keys * 1000000000 / elapsed, false_positives.load() * 100.0 / num_keys, count_elapsed);
    PRINT_INFO("%s\n", std_bloom.toString().c_str());
  }
}

} // namespace util
} // namespace mycc

int main(int argc, char **argv)
{
  return mycc::test::RunAllTests();
}

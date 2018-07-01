
#ifndef MYCC_UTIL_HISTOGRAM_H_
#define MYCC_UTIL_HISTOGRAM_H_

#include "types_util.h"

namespace mycc
{
namespace util
{

class Histogram
{
public:
  Histogram() {}
  ~Histogram() {}

  void Clear();
  void Add(double value);
  void Merge(const Histogram &other);

  string ToString() const;

  double Median() const;
  double Percentile(double p) const;
  double Average() const;
  double StandardDeviation() const;

private:
  double min_;
  double max_;
  double num_;
  double sum_;
  double sum_squares_;

  enum
  {
    kNumBuckets = 154
  };
  static const double kBucketLimit[kNumBuckets];
  double buckets_[kNumBuckets];
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_HISTOGRAM_H_

#ifndef MYCC_UTIL_RANGE_FRAGMENT_H_
#define MYCC_UTIL_RANGE_FRAGMENT_H_

#include <list>
#include <string>
#include "types_util.h"

namespace mycc
{
namespace util
{

//  a b c d e f g h i j k l m n o p q r s t u v w x y z
//  ----------->g       k<--------p       t-------x
//                                  q---s

class CharRangeFragment
{
public:
  // caller should use Lock to avoid data races
  // On success, return true. Otherwise, return false due to invalid argumetns
  bool AddToRange(const string &start, const string &end);

  bool IsCompleteRange() const;

  bool IsCoverRange(const string &start, const string &end) const;

  string DebugString() const;

private:
  std::list<std::pair<string, string>> range_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_RANGE_FRAGMENT_H_
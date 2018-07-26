
#include "small_container.h"
#include <sstream>

namespace mycc
{
namespace util
{

namespace
{

static int CompareTwoEndKey(const string &a, const string &b)
{
  if (a == "" && b == "")
  {
    return 0;
  }
  if (a == "")
  {
    return 1;
  }
  if (b == "")
  {
    return -1;
  }
  return a.compare(b);
}

} // namespace

bool CharRangeFragment::IsCoverRange(const string &start, const string &end) const
{
  std::list<std::pair<string, string>>::const_iterator it = range_.begin();
  for (; it != range_.end(); ++it)
  {
    if (it->second != "" && start.compare(it->second) > 0)
    {
      continue;
    }
    break;
  }

  if (it == range_.end())
  {
    return false;
  }
  return (start.compare(it->first) >= 0) && (CompareTwoEndKey(end, it->second) <= 0);
}

bool CharRangeFragment::AddToRange(const string &start, const string &end)
{
  if (end != "" && start.compare(end) > 0)
  {
    return false;
  }
  std::list<std::pair<string, string>>::iterator it = range_.begin();
  for (; it != range_.end(); ++it)
  {
    if (it->second != "" && start.compare(it->second) > 0)
    {
      continue;
    }
    break;
  }
  if (it == range_.end())
  {
    range_.push_back(std::pair<string, string>(start, end));
    return true;
  }
  /*
     *                         [  )
     *              [-------)          [------)
     *
     *
     *                                   [                  )
     *              [-------)          [------)  .....   [----)
     *
     *
     *                                   [                      )
     *              [-------)          [------)  .....   [----)   [----)
     *
     *
     *                            [                         )
     *              [-------)          [------)  .....   [----)
     *
     *
     *                            [                             )
     *              [-------)          [------)  .....   [----)   [-----)
     */
  string new_start = start.compare(it->first) < 0 ? start : it->first;
  string new_end = end;
  while (it != range_.end())
  {
    if (end == "" || end.compare(it->first) >= 0)
    {
      new_end = CompareTwoEndKey(end, it->second) > 0 ? end : it->second;
      it = range_.erase(it);
      continue;
    }
    break;
  }
  range_.insert(it, std::pair<string, string>(new_start, new_end));
  return true;
}

string CharRangeFragment::DebugString() const
{
  std::list<std::pair<string, string>>::const_iterator it = range_.begin();
  std::stringstream ss;
  for (; it != range_.end(); ++it)
  {
    ss << it->first << ":" << it->second << " ";
  }
  return ss.str();
}

bool CharRangeFragment::IsCompleteRange() const
{
  return (range_.size() == 1) && (range_.begin()->first == "") && (range_.begin()->second == "");
}

} // namespace util
} // namespace mycc
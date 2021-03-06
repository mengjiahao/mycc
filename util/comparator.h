
#ifndef MYCC_UTIL_COMPARATOR_H_
#define MYCC_UTIL_COMPARATOR_H_

#include <string.h>
#include "stringpiece.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

// comparators for stl containers
// for std::unordered_map:
//   std::unordered_map<const char*, long, hash<const char*>, eqstr> vals;
struct Eqstr
{
  bool operator()(const char *s1, const char *s2) const
  {
    return strcmp(s1, s2) == 0;
  }
};

// for set, map
struct Ltstr
{
  bool operator()(const char *s1, const char *s2) const
  {
    return strcmp(s1, s2) < 0;
  }
};

// A Comparator object provides a total order across slices that are
// used as keys in an sstable or a database.  A Comparator implementation
// must be thread-safe since rocksdb may invoke its methods concurrently
// from multiple threads.
class Comparator
{
public:
  virtual ~Comparator();

  // Three-way comparison.  Returns value:
  //   < 0 iff "a" < "b",
  //   == 0 iff "a" == "b",
  //   > 0 iff "a" > "b"
  virtual int Compare(const StringPiece &a, const StringPiece &b) const = 0;

  // Compares two slices for equality. The following invariant should always
  // hold (and is the default implementation):
  //   Equal(a, b) iff Compare(a, b) == 0
  // Overwrite only if equality comparisons can be done more efficiently than
  // three-way comparisons.
  virtual bool Equal(const StringPiece &a, const StringPiece &b) const
  {
    return Compare(a, b) == 0;
  }

  // The name of the comparator.  Used to check for comparator
  // mismatches (i.e., a DB created with one comparator is
  // accessed using a different comparator.
  //
  // The client of this package should switch to a new name whenever
  // the comparator implementation changes in a way that will cause
  // the relative ordering of any two keys to change.
  //
  // Names starting with "rocksdb." are reserved and should not be used
  // by any clients of this package.
  virtual const char *Name() const = 0;

  // Advanced functions: these are used to reduce the space requirements
  // for internal data structures like index blocks.

  // If *start < limit, changes *start to a short string in [start,limit).
  // Simple comparator implementations may return with *start unchanged,
  // i.e., an implementation of this method that does nothing is correct.
  virtual void FindShortestSeparator(
      string *start,
      const StringPiece &limit) const = 0;

  // Changes *key to a short string >= *key.
  // Simple comparator implementations may return with *key unchanged,
  // i.e., an implementation of this method that does nothing is correct.
  virtual void FindShortSuccessor(string *key) const = 0;

  // if it is a wrapped comparator, may return the root one.
  // return itself it is not wrapped.
  virtual const Comparator *GetRootComparator() const { return this; }
};

// Return a builtin comparator that uses lexicographic byte-wise
// ordering.  The result remains the property of this module and
// must not be deleted.
extern const Comparator *BytewiseComparator();

// Return a builtin comparator that uses reverse lexicographic byte-wise
// ordering.
extern const Comparator *ReverseBytewiseComparator();

struct LessOfComparator
{
  explicit LessOfComparator(const Comparator *c = BytewiseComparator())
      : cmp(c) {}

  bool operator()(const string &a, const string &b) const
  {
    return cmp->Compare(StringPiece(a), StringPiece(b)) < 0;
  }
  bool operator()(const StringPiece &a, const StringPiece &b) const
  {
    return cmp->Compare(a, b) < 0;
  }

  const Comparator *cmp;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_COMPARATOR_H_
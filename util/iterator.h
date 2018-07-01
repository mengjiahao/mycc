
#ifndef MYCC_UTIL_ITERATOR_H_
#define MYCC_UTIL_ITERATOR_H_

#include "status.h"
#include "stringpiece.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

// An iterator yields a sequence of key/value pairs from a source.
// The following class defines the interface.  Multiple implementations
// are provided by this library.  In particular, iterators are provided
// to access the contents of a Table or a DB.
//
// Multiple threads can invoke const methods on an Iterator without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Iterator must use
// external synchronization.

class Iterator
{
public:
  Iterator();
  virtual ~Iterator();

  // An iterator is either positioned at a key/value pair, or
  // not valid.  This method returns true iff the iterator is valid.
  virtual bool Valid() const = 0;

  // Position at the first key in the source.  The iterator is Valid()
  // after this call iff the source is not empty.
  virtual void SeekToFirst() = 0;

  // Position at the first key in the source that is at or past target.
  // The iterator is Valid() after this call iff the source contains
  // an entry that comes at or past target.
  virtual void Seek(const StringPiece &target) = 0;

  // Moves to the next entry in the source.  After this call, Valid() is
  // true iff the iterator was not positioned at the last entry in the source.
  // REQUIRES: Valid()
  virtual void Next() = 0;

  // Return the key for the current entry.  The underlying storage for
  // the returned slice is valid only until the next modification of
  // the iterator.
  // REQUIRES: Valid()
  virtual StringPiece key() const = 0;

  // Return the value for the current entry.  The underlying storage for
  // the returned slice is valid only until the next modification of
  // the iterator.
  // REQUIRES: Valid()
  virtual StringPiece value() const = 0;

  // If an error has occurred, return it.  Else return an ok status.
  virtual Status status() const = 0;

  // Clients are allowed to register function/arg1/arg2 triples that
  // will be invoked when this iterator is destroyed.
  //
  // Note that unlike all of the preceding methods, this method is
  // not abstract and therefore clients should not override it.
  typedef void (*CleanupFunction)(void *arg1, void *arg2);
  void RegisterCleanup(CleanupFunction function, void *arg1, void *arg2);

private:
  struct Cleanup
  {
    CleanupFunction function;
    void *arg1;
    void *arg2;
    Cleanup *next;
  };
  Cleanup cleanup_;

  // No copying allowed
  DISALLOW_COPY_AND_ASSIGN(Iterator);
};

// Return an empty iterator (yields nothing).
extern Iterator *NewEmptyIterator();

// Return an empty iterator with the specified status.
extern Iterator *NewErrorIterator(const Status &status);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_ITERATOR_H_
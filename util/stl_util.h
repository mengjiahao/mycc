
#ifndef MYCC_UTIL_STL_UTIL_H_
#define MYCC_UTIL_STL_UTIL_H_

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <algorithm>
#include <deque>
//#include <iomanip>
//#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mycc
{
namespace util
{

// comparators for stl containers
// for std::unordered_map:
//   std::unordered_map<const char*, long, hash<const char*>, eqstr> vals;
struct EqStr
{
  bool operator()(const char *s1, const char *s2) const
  {
    return strcmp(s1, s2) == 0;
  }
};

// for set, map
struct LtStr
{
  bool operator()(const char *s1, const char *s2) const
  {
    return strcmp(s1, s2) < 0;
  }
};

/*!
 * \brief safely get the beginning address of a vector
 * \param vec input vector
 * \return beginning address of a vector
 */
template <typename T>
inline T *BeginPtr(std::vector<T> &vec)
{ // NOLINT(*)
  if (vec.size() == 0)
  {
    return NULL;
  }
  else
  {
    return &vec[0];
  }
}
/*!
 * \brief get the beginning address of a const vector
 * \param vec input vector
 * \return beginning address of a vector
 */
template <typename T>
inline const T *BeginPtr(const std::vector<T> &vec)
{
  if (vec.size() == 0)
  {
    return NULL;
  }
  else
  {
    return &vec[0];
  }
}
/*!
 * \brief get the beginning address of a string
 * \param str input string
 * \return beginning address of a string
 */
inline char *BeginPtr(std::string &str)
{ // NOLINT(*)
  if (str.length() == 0)
    return NULL;
  return &str[0];
}
/*!
 * \brief get the beginning address of a const string
 * \param str input string
 * \return beginning address of a string
 */
inline const char *BeginPtr(const std::string &str)
{
  if (str.length() == 0)
    return NULL;
  return &str[0];
}

template <class T, class A>
T STLJoinify(const A &begin, const A &end, const T &t)
{
  T result;
  for (A it = begin; it != end; it++)
  {
    if (!result.empty())
      result.append(t);
    result.append(*it);
  }
  return result;
}

// Clears internal memory of an STL object.
// STL clear()/reserve(0) does not always free internal memory allocated
// This function uses swap/destructor to ensure the internal memory is freed.
template <class T>
void STLClearObject(T *obj)
{
  T tmp;
  tmp.swap(*obj);
  // Sometimes "T tmp" allocates objects with memory (arena implementation?).
  // Hence using additional reserve(0) even if it doesn't always work.
  obj->reserve(0);
}

/**
 * find the value given a key k from container c.
 * If the key can be found, the value is stored in *value
 * return true if the key can be found. false otherwise.
 */
template <class K, class V, class C>
bool STLMapGet(const K &k, const C &c, V *value)
{
  auto it = c.find(k);
  if (it != c.end())
  {
    *value = it->second;
    return true;
  }
  else
  {
    return false;
  }
}

/**
 * pop and get the front element of a container
 */
template <typename Container>
typename Container::value_type STLPopGetFront(Container &c)
{
  typename Container::value_type v;
  std::swap(v, c.front());
  c.pop_front();
  return v;
}

// Returns a mutable char* pointing to a string's internal buffer, which may not
// be null-terminated. Returns nullptr for an empty string. If not non-null,
// writing through this pointer will modify the string.
//
// string_as_array(&str)[i] is valid for 0 <= i < str.size() until the
// next call to a string method that invalidates iterators.
//
// In C++11 you may simply use &str[0] to get a mutable char*.
//
// Prior to C++11, there was no standard-blessed way of getting a mutable
// reference to a string's internal buffer. The requirement that string be
// contiguous is officially part of the C++11 standard [string.require]/5.
// According to Matt Austern, this should already work on all current C++98
// implementations.
inline char *string_as_array(std::string *str)
{
  return str->empty() ? nullptr : &*str->begin();
}

// Returns the T* array for the given vector, or nullptr if the vector was empty.
//
// Note: If you know the array will never be empty, you can use &*v.begin()
// directly, but that is may dump core if v is empty. This function is the most
// efficient code that will work, taking into account how our STL is actually
// implemented. THIS IS NON-PORTABLE CODE, so use this function instead of
// repeating the nonportable code everywhere. If our STL implementation changes,
// we will need to change this as well.
template <typename T, typename Allocator>
inline T *vector_as_array(std::vector<T, Allocator> *v)
{
#if defined NDEBUG && !defined _GLIBCXX_DEBUG
  return &*v->begin();
#else
  return v->empty() ? nullptr : &*v->begin();
#endif
}
// vector_as_array overload for const std::vector<>.
template <typename T, typename Allocator>
inline const T *vector_as_array(const std::vector<T, Allocator> *v)
{
#if defined NDEBUG && !defined _GLIBCXX_DEBUG
  return &*v->begin();
#else
  return v->empty() ? nullptr : &*v->begin();
#endif
}

// Like str->resize(new_size), except any new characters added to "*str" as a
// result of resizing may be left uninitialized, rather than being filled with
// '0' bytes. Typically used when code is then going to overwrite the backing
// store of the string with known data. Uses a Google extension to ::string.
inline void STLStringResizeUninitialized(std::string *s, size_t new_size)
{
  s->resize(new_size);
}

// For a range within a container of pointers, calls delete (non-array version)
// on these pointers.
// NOTE: for these three functions, we could just implement a DeleteObject
// functor and then call for_each() on the range and functor, but this
// requires us to pull in all of algorithm.h, which seems expensive.
// For hash_[multi]set, it is important that this deletes behind the iterator
// because the hash_set may call the hash function on the iterator when it is
// advanced, which could result in the hash function trying to deference a
// stale pointer.
template <class ForwardIterator>
void STLDeleteContainerPointers(ForwardIterator begin, ForwardIterator end)
{
  while (begin != end)
  {
    ForwardIterator temp = begin;
    ++begin;
    delete *temp;
  }
}

// For a range within a container of pairs, calls delete (non-array version) on
// BOTH items in the pairs.
// NOTE: Like STLDeleteContainerPointers, it is important that this deletes
// behind the iterator because if both the key and value are deleted, the
// container may call the hash function on the iterator when it is advanced,
// which could result in the hash function trying to dereference a stale
// pointer.
template <class ForwardIterator>
void STLDeleteContainerPairPointers(ForwardIterator begin,
                                    ForwardIterator end)
{
  while (begin != end)
  {
    ForwardIterator temp = begin;
    ++begin;
    delete temp->first;
    delete temp->second;
  }
}

// For a range within a container of pairs, calls delete (non-array version) on
// the FIRST item in the pairs.
// NOTE: Like STLDeleteContainerPointers, deleting behind the iterator.
template <class ForwardIterator>
void STLDeleteContainerPairFirstPointers(ForwardIterator begin,
                                         ForwardIterator end)
{
  while (begin != end)
  {
    ForwardIterator temp = begin;
    ++begin;
    delete temp->first;
  }
}

// Calls delete (non-array version) on the SECOND item (pointer) in each pair in
// the range [begin, end).
//
// Note: If you're calling this on an entire container, you probably want to
// call STLDeleteValues(&container) instead, or use ValueDeleter.
template <typename ForwardIterator>
void STLDeleteContainerPairSecondPointers(ForwardIterator begin,
                                          ForwardIterator end)
{
  while (begin != end)
  {
    ForwardIterator temp = begin;
    ++begin;
    delete temp->second;
  }
}

// Deletes all the elements in an STL container and clears the container. This
// function is suitable for use with a vector, set, hash_set, or any other STL
// container which defines sensible begin(), end(), and clear() methods.
//
// If container is NULL, this function is a no-op.
template <typename T>
void STLDeleteElements(T *container)
{
  if (!container)
    return;
  auto it = container->begin(); // typename T::iterator it = container->begin();
  while (it != container->end())
  {
    auto temp = it;
    ++it;
    delete *temp;
  }
  container->clear();
}

// Given an STL container consisting of (key, value) pairs, STLDeleteValues
// deletes all the "value" components and clears the container. Does nothing in
// the case it's given a NULL pointer.
template <typename T>
void STLDeleteValues(T *container)
{
  if (!container)
    return;
  auto it = container->begin();
  while (it != container->end())
  {
    auto temp = it;
    ++it;
    delete temp->second;
  }
  container->clear();
}

// The following classes provide a convenient way to delete all elements or
// values from STL containers when they goes out of scope.  This greatly
// simplifies code that creates temporary objects and has multiple return
// statements.  Example:
//
// vector<MyProto *> tmp_proto;
// STLElementDeleter<vector<MyProto *> > d(&tmp_proto);
// if (...) return false;
// ...
// return success;

// Given a pointer to an STL container this class will delete all the element
// pointers when it goes out of scope.
template <class T>
class STLElementDeleter
{
public:
  STLElementDeleter<T>(T *container) : container_(container) {}
  ~STLElementDeleter<T>() { STLDeleteElements(container_); }

private:
  T *container_;
};

// Given a pointer to an STL container this class will delete all the value
// pointers when it goes out of scope.
template <class T>
class STLValueDeleter
{
public:
  STLValueDeleter<T>(T *container) : container_(container) {}
  ~STLValueDeleter<T>() { STLDeleteValues(container_); }

private:
  T *container_;
};

// Sorts and removes duplicates from a sequence container.
template <typename T>
inline void STLSortAndRemoveDuplicates(T *v)
{
  std::sort(v->begin(), v->end());
  v->erase(std::unique(v->begin(), v->end()), v->end());
}

// Counts the number of instances of val in a container.
template <typename Container, typename T>
typename std::iterator_traits<typename Container::const_iterator>::difference_type
STLCount(const Container &container, const T &val)
{
  return std::count(container.begin(), container.end(), val);
}

// Test to see if a set, map, hash_set or hash_map contains a particular key.
// Returns true if the key is in the collection.
template <typename Collection, typename Key>
bool STLContainsKey(const Collection &collection, const Key &key)
{
  return collection.find(key) != collection.end();
}

// Test to see if a collection like a vector contains a particular value.
// Returns true if the value is in the collection.
template <typename Collection, typename Value>
bool STLContainsValue(const Collection &collection, const Value &value)
{
  return std::find(collection.begin(), collection.end(), value) !=
         collection.end();
}

// Returns true if the container is sorted.
template <typename Container>
bool STLIsSorted(const Container &cont)
{
  // Note: Use reverse iterator on container to ensure we only require
  // value_type to implement operator<.
  return std::adjacent_find(cont.rbegin(), cont.rend(),
                            std::less<typename Container::value_type>()) == cont.rend();
}

// Returns a new ResultType containing the difference of two sorted containers.
template <typename ResultType, typename Arg1, typename Arg2>
ResultType STLSetDifference(const Arg1 &a1, const Arg2 &a2)
{
  //DCHECK(STLIsSorted(a1));
  //DCHECK(STLIsSorted(a2));
  ResultType difference;
  std::set_difference(a1.begin(), a1.end(),
                      a2.begin(), a2.end(),
                      std::inserter(difference, difference.end()));
  return difference;
}

// Returns a new ResultType containing the union of two sorted containers.
template <typename ResultType, typename Arg1, typename Arg2>
ResultType STLSetUnion(const Arg1 &a1, const Arg2 &a2)
{
  //DCHECK(STLIsSorted(a1));
  //DCHECK(STLIsSorted(a2));
  ResultType result;
  std::set_union(a1.begin(), a1.end(),
                 a2.begin(), a2.end(),
                 std::inserter(result, result.end()));
  return result;
}

// Returns a new ResultType containing the intersection of two sorted
// containers.
template <typename ResultType, typename Arg1, typename Arg2>
ResultType STLSetIntersection(const Arg1 &a1, const Arg2 &a2)
{
  //DCHECK(STLIsSorted(a1));
  //DCHECK(STLIsSorted(a2));
  ResultType result;
  std::set_intersection(a1.begin(), a1.end(),
                        a2.begin(), a2.end(),
                        std::inserter(result, result.end()));
  return result;
}

// Returns true if the sorted container |a1| contains all elements of the sorted
// container |a2|.
template <typename Arg1, typename Arg2>
bool STLIncludes(const Arg1 &a1, const Arg2 &a2)
{
  //DCHECK(STLIsSorted(a1));
  //DCHECK(STLIsSorted(a2));
  return std::includes(a1.begin(), a1.end(),
                       a2.begin(), a2.end());
}

// Helper methods to estimate memroy usage by std containers.

template <class Key, class Value, class Hash>
size_t STLApproximateMemoryUsage(
    const std::unordered_map<Key, Value, Hash> &umap)
{
  typedef std::unordered_map<Key, Value, Hash> Map;
  return sizeof(umap) +
         // Size of all items plus a next pointer for each item.
         (sizeof(typename Map::value_type) + sizeof(void *)) * umap.size() +
         // Size of hash buckets.
         umap.bucket_count() * sizeof(void *);
}

// Returns a pointer to the const value associated with the given key if it
// exists, or NULL otherwise.
template <class Collection>
const typename Collection::value_type::second_type *STLFindOrNull(
    const Collection &collection,
    const typename Collection::value_type::first_type &key)
{
  typename Collection::const_iterator it = collection.find(key);
  if (it == collection.end())
  {
    return 0;
  }
  return &it->second;
}

// Same as above but returns a pointer to the non-const value.
template <class Collection>
typename Collection::value_type::second_type *STLFindOrNull(
    Collection &collection, // NOLINT
    const typename Collection::value_type::first_type &key)
{
  typename Collection::iterator it = collection.find(key);
  if (it == collection.end())
  {
    return 0;
  }
  return &it->second;
}

// Returns the pointer value associated with the given key. If none is found,
// NULL is returned. The function is designed to be used with a map of keys to
// pointers.
//
// This function does not distinguish between a missing key and a key mapped
// to a NULL value.
template <class Collection>
typename Collection::value_type::second_type STLFindPtrOrNull(
    const Collection &collection,
    const typename Collection::value_type::first_type &key)
{
  typename Collection::const_iterator it = collection.find(key);
  if (it == collection.end())
  {
    return typename Collection::value_type::second_type();
  }
  return it->second;
}

// Finds the value associated with the given key and copies it to *value (if not
// NULL). Returns false if the key was not found, true otherwise.
template <class Collection, class Key, class Value>
bool STLFindCopy(const Collection &collection,
                 const Key &key,
                 Value *const value)
{
  typename Collection::const_iterator it = collection.find(key);
  if (it == collection.end())
  {
    return false;
  }
  if (value)
  {
    *value = it->second;
  }
  return true;
}

// Returns true if and only if the given collection contains the given key-value
// pair.
template <class Collection, class Key, class Value>
bool STLContainsKeyValuePair(const Collection &collection,
                             const Key &key,
                             const Value &value)
{
  typedef typename Collection::const_iterator const_iterator;
  std::pair<const_iterator, const_iterator> range = collection.equal_range(key);
  for (const_iterator it = range.first; it != range.second; ++it)
  {
    if (it->second == value)
    {
      return true;
    }
  }
  return false;
}

// Inserts the given key-value pair into the collection. Returns true if and
// only if the key from the given pair didn't previously exist. Otherwise, the
// value in the map is replaced with the value from the given pair.
template <class Collection>
bool STLInsertOrUpdate(Collection *const collection,
                       const typename Collection::value_type &vt)
{
  std::pair<typename Collection::iterator, bool> ret = collection->insert(vt);
  if (!ret.second)
  {
    // update
    ret.first->second = vt.second;
    return false;
  }
  return true;
}

// Same as above, except that the key and value are passed separately.
template <class Collection>
bool STLInsertOrUpdate(Collection *const collection,
                       const typename Collection::value_type::first_type &key,
                       const typename Collection::value_type::second_type &value)
{
  return STLInsertOrUpdate(collection,
                           typename Collection::value_type(key, value));
}

// Inserts/updates all the key-value pairs from the range defined by the
// iterators "first" and "last" into the given collection.
template <class Collection, class InputIterator>
void STLInsertOrUpdateMany(Collection *const collection,
                           InputIterator first, InputIterator last)
{
  for (; first != last; ++first)
  {
    STLInsertOrUpdate(collection, *first);
  }
}

// Looks up a given key and value pair in a collection and inserts the key-value
// pair if it's not already present. Returns a reference to the value associated
// with the key.
template <class Collection>
typename Collection::value_type::second_type &STLLookupOrInsert(
    Collection *const collection, const typename Collection::value_type &vt)
{
  return collection->insert(vt).first->second;
}

// Same as above except the key-value are passed separately.
template <class Collection>
typename Collection::value_type::second_type &STLLookupOrInsert(
    Collection *const collection,
    const typename Collection::value_type::first_type &key,
    const typename Collection::value_type::second_type &value)
{
  return STLLookupOrInsert(collection,
                           typename Collection::value_type(key, value));
}

// Counts the number of equivalent elements in the given "sequence", and stores
// the results in "count_map" with element as the key and count as the value.
//
// Example:
//   vector<string> v = {"a", "b", "c", "a", "b"};
//   map<string, int> m;
//   AddTokenCounts(v, 1, &m);
//   assert(m["a"] == 2);
//   assert(m["b"] == 2);
//   assert(m["c"] == 1);
template <typename Sequence, typename Collection>
void STLAddTokenCounts(
    const Sequence &sequence,
    const typename Collection::value_type::second_type &increment,
    Collection *const count_map)
{
  for (typename Sequence::const_iterator it = sequence.begin();
       it != sequence.end(); ++it)
  {
    typename Collection::value_type::second_type &value =
        STLLookupOrInsert(count_map, *it,
                          typename Collection::value_type::second_type());
    value += increment;
  }
}

// Returns a reference to the value associated with key. If not found, a value
// is default constructed on the heap and added to the map.
//
// This function is useful for containers of the form map<Key, Value*>, where
// inserting a new key, value pair involves constructing a new heap-allocated
// Value, and storing a pointer to that in the collection.
template <class Collection>
typename Collection::value_type::second_type &
STLLookupOrInsertNew(Collection *const collection,
                     const typename Collection::value_type::first_type &key)
{
  typedef typename std::iterator_traits<
      typename Collection::value_type::second_type>::value_type Element;
  std::pair<typename Collection::iterator, bool> ret =
      collection->insert(typename Collection::value_type(
          key,
          static_cast<typename Collection::value_type::second_type>(NULL)));
  if (ret.second)
  {
    ret.first->second = new Element();
  }
  return ret.first->second;
}

// Same as above but constructs the value using the single-argument constructor
// and the given "arg".
template <class Collection, class Arg>
typename Collection::value_type::second_type &
STLLookupOrInsertNew(Collection *const collection,
                     const typename Collection::value_type::first_type &key,
                     const Arg &arg)
{
  typedef typename std::iterator_traits<
      typename Collection::value_type::second_type>::value_type Element;
  std::pair<typename Collection::iterator, bool> ret =
      collection->insert(typename Collection::value_type(
          key,
          static_cast<typename Collection::value_type::second_type>(NULL)));
  if (ret.second)
  {
    ret.first->second = new Element(arg);
  }
  return ret.first->second;
}

// Inserts all the keys from map_container into key_container, which must
// support insert(MapContainer::key_type).
//
// Note: any initial contents of the key_container are not cleared.
template <class MapContainer, class KeyContainer>
void STLInsertKeysFromMap(const MapContainer &map_container,
                          KeyContainer *key_container)
{
  if (key_container == nullptr)
    return;
  for (typename MapContainer::const_iterator it = map_container.begin();
       it != map_container.end(); ++it)
  {
    key_container->insert(it->first);
  }
}

// Appends all the keys from map_container into key_container, which must
// support push_back(MapContainer::key_type).
//
// Note: any initial contents of the key_container are not cleared.
template <class MapContainer, class KeyContainer>
void STLAppendKeysFromMap(const MapContainer &map_container,
                          KeyContainer *key_container)
{
  if (key_container == nullptr)
    return;
  for (typename MapContainer::const_iterator it = map_container.begin();
       it != map_container.end(); ++it)
  {
    key_container->push_back(it->first);
  }
}

// A more specialized overload of AppendKeysFromMap to optimize reallocations
// for the common case in which we're appending keys to a vector and hence can
// (and sometimes should) call reserve() first.
//
// (It would be possible to play SFINAE games to call reserve() for any
// container that supports it, but this seems to get us 99% of what we need
// without the complexity of a SFINAE-based solution.)
template <class MapContainer, class KeyType>
void STLAppendKeysFromMap(const MapContainer &map_container,
                          std::vector<KeyType> *key_container)
{
  if (key_container == nullptr)
    return;
  // We now have the opportunity to call reserve(). Calling reserve() every
  // time is a bad idea for some use cases: libstdc++'s implementation of
  // vector<>::reserve() resizes the vector's backing store to exactly the
  // given size (unless it's already at least that big). Because of this,
  // the use case that involves appending a lot of small maps (total size
  // N) one by one to a vector would be O(N^2). But never calling reserve()
  // loses the opportunity to improve the use case of adding from a large
  // map to an empty vector (this improves performance by up to 33%). A
  // number of heuristics are possible; see the discussion in
  // cl/34081696. Here we use the simplest one.
  if (key_container->empty())
  {
    key_container->reserve(map_container.size());
  }
  for (typename MapContainer::const_iterator it = map_container.begin();
       it != map_container.end(); ++it)
  {
    key_container->push_back(it->first);
  }
}

// Inserts all the values from map_container into value_container, which must
// support push_back(MapContainer::mapped_type).
//
// Note: any initial contents of the value_container are not cleared.
template <class MapContainer, class ValueContainer>
void STLAppendValuesFromMap(const MapContainer &map_container,
                            ValueContainer *value_container)
{
  if (value_container == nullptr)
    return;
  for (typename MapContainer::const_iterator it = map_container.begin();
       it != map_container.end(); ++it)
  {
    value_container->push_back(it->second);
  }
}

// A more specialized overload of AppendValuesFromMap to optimize reallocations
// for the common case in which we're appending values to a vector and hence
// can (and sometimes should) call reserve() first.
//
// (It would be possible to play SFINAE games to call reserve() for any
// container that supports it, but this seems to get us 99% of what we need
// without the complexity of a SFINAE-based solution.)
template <class MapContainer, class ValueType>
void STLAppendValuesFromMap(const MapContainer &map_container,
                            std::vector<ValueType> *value_container)
{
  if (value_container == nullptr)
    return;
  // See AppendKeysFromMap for why this is done.
  if (value_container->empty())
  {
    value_container->reserve(map_container.size());
  }
  for (typename MapContainer::const_iterator it = map_container.begin();
       it != map_container.end(); ++it)
  {
    value_container->push_back(it->second);
  }
}

// These functions return true if there is some element in the sorted range
// [begin1, end) which is equal to some element in the sorted range [begin2,
// end2). The iterators do not have to be of the same type, but the value types
// must be less-than comparable. (Two elements a,b are considered equal if
// !(a < b) && !(b < a).
template<typename InputIterator1, typename InputIterator2>
bool SortedRangesHaveIntersection(InputIterator1 begin1, InputIterator1 end1,
                                  InputIterator2 begin2, InputIterator2 end2) {
  assert(std::is_sorted(begin1, end1));
  assert(std::is_sorted(begin2, end2));
  while (begin1 != end1 && begin2 != end2) {
    if (*begin1 < *begin2) {
      ++begin1;
    } else if (*begin2 < *begin1) {
      ++begin2;
    } else {
      return true;
    }
  }
  return false;
}

// This is equivalent to the function above, but using a custom comparison
// function.
template<typename InputIterator1, typename InputIterator2, typename Comp>
bool SortedRangesHaveIntersection(InputIterator1 begin1, InputIterator1 end1,
                                  InputIterator2 begin2, InputIterator2 end2,
                                  Comp comparator) {
  assert(std::is_sorted(begin1, end1, comparator));
  assert(std::is_sorted(begin2, end2, comparator));
  while (begin1 != end1 && begin2 != end2) {
    if (comparator(*begin1, *begin2)) {
      ++begin1;
    } else if (comparator(*begin2, *begin1)) {
      ++begin2;
    } else {
      return true;
    }
  }
  return false;
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_STL_UTIL_H_
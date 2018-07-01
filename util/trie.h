
#ifndef MYCC_UTIL_TRIE_H_
#define MYCC_UTIL_TRIE_H_

#include "types_util.h"

namespace mycc
{
namespace util
{

/*
 * A compressed trie.  A trie node consists of zero or more characters that
 * are common to all elements with this prefix, optionally followed by some
 * children.  If value is not NULL, the trie node is a terminal node.
 *
 * For example, consider the following set of strings:
 * abc
 * def
 * definite
 * definition
 *
 * The trie would look like:
 * root: len = 0, children a and d non-NULL, value = NULL.
 *    a: len = 2, contents = bc, value = (data for "abc")
 *    d: len = 2, contents = ef, children i non-NULL, value = (data for "def")
 *       i: len = 3, contents = nit, children e and i non-NULL, value = NULL
 *           e: len = 0, children all NULL, value = (data for "definite")
 *           i: len = 2, contents = on, children all NULL,
 *              value = (data for "definition")
 */
struct trie
{
  struct trie *children[256];
  int32_t len;
  char *contents;
  void *value;
};

struct trie *trie_make_node(const char *key, void *value);

void *trie_add(struct trie *root, const char *key, void *value);

typedef int32_t (*trie_make_fn)(const char *unmatched, void *data, void *baton);

int32_t trie_find(struct trie *root, const char *key, trie_make_fn fn,
                  void *baton);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_TRIE_H_
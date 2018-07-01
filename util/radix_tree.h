
#ifndef MYCC_UTIL_RADIX_TREE_H_
#define MYCC_UTIL_RADIX_TREE_H_

//#include <linux/types.h>
#include <stddef.h>
#include "bitmap.h"
#include "math_util.h"

namespace mycc
{
namespace util
{

#define RADIX_TREE_MAP_SHIFT 6
#define RADIX_TREE_MAP_SIZE (1 << RADIX_TREE_MAP_SHIFT)
#define RADIX_TREE_MAP_MASK (RADIX_TREE_MAP_SIZE - 1)
#define RADIX_TREE_MAX_HEIGHT \
  DIV_ROUND_UP((sizeof(uint32_t) * NBITS_IN_BYTE), RADIX_TREE_MAP_SHIFT)

struct radix_tree_node
{
  void *slots[RADIX_TREE_MAP_SIZE];
  int32_t count;
};

struct radix_tree_root
{
  struct radix_tree_node *rnode;
  //gfp_t                   gfp_mask;
  int32_t height;
};

#define RADIX_TREE_INIT(mask) \
  {.rnode = NULL, .height = 0};
#define INIT_RADIX_TREE(root, mask) \
  {                                 \
    (root)->rnode = NULL;           \
    (root)->height = 0;             \
  }
#define RADIX_TREE(name, mask) \
  struct radix_tree_root name = RADIX_TREE_INIT(mask)

void *radix_tree_lookup(struct radix_tree_root *, int32_t);
void *radix_tree_delete(struct radix_tree_root *, int32_t);
int32_t radix_tree_insert(struct radix_tree_root *, int32_t, void *);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_RADIX_TREE_H_
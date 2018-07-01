
#include "radix_tree.h"
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

namespace mycc
{
namespace util
{

static inline int32_t
radix_max(struct radix_tree_root *root)
{
  return (1 << (root->height * RADIX_TREE_MAP_SHIFT)) - 1;
}

static inline int32_t
radix_pos(int32_t id, int32_t height)
{
  return (id >> (RADIX_TREE_MAP_SHIFT * height)) & RADIX_TREE_MAP_MASK;
}

void *
radix_tree_lookup(struct radix_tree_root *root, int32_t index)
{
  struct radix_tree_node *node;
  void *item;
  int32_t height;

  item = NULL;
  node = root->rnode;
  height = root->height - 1;
  if (index > radix_max(root))
    goto out;
  while (height && node)
    node = (struct radix_tree_node *)(node->slots[radix_pos(index, height--)]);
  if (node)
    item = node->slots[radix_pos(index, 0)];

out:
  return (item);
}

void *
radix_tree_delete(struct radix_tree_root *root, int32_t index)
{
  struct radix_tree_node *stack[RADIX_TREE_MAX_HEIGHT];
  struct radix_tree_node *node;
  void *item;
  int32_t height;
  int32_t idx;

  item = NULL;
  node = root->rnode;
  height = root->height - 1;
  if (index > radix_max(root))
    goto out;
  // Find the node and record the path in stack.
  while (height && node)
  {
    stack[height] = node;
    node = (struct radix_tree_node *)(node->slots[radix_pos(index, height--)]);
  }
  idx = radix_pos(index, 0);
  if (node)
    item = node->slots[idx];
  // If we removed something reduce the height of the tree.
  if (item)
    for (;;)
    {
      node->slots[idx] = NULL;
      node->count--;
      if (node->count > 0)
        break;
      free(node);
      if (node == root->rnode)
      {
        root->rnode = NULL;
        root->height = 0;
        break;
      }
      height++;
      node = stack[height];
      idx = radix_pos(index, height);
    }
out:
  return (item);
}

int32_t radix_tree_insert(struct radix_tree_root *root, int32_t index, void *item)
{
  struct radix_tree_node *node;
  struct radix_tree_node *temp[RADIX_TREE_MAX_HEIGHT - 1];
  int32_t height;
  int32_t idx;

  /* bail out upon insertion of a NULL item */
  if (item == NULL)
    return (-EINVAL);

  /* get root node, if any */
  node = root->rnode;

  /* allocate root node, if any */
  if (node == NULL)
  {
    node = (struct radix_tree_node *)malloc(sizeof(*node));
    if (node == NULL)
      return (-ENOMEM);
    root->rnode = node;
    root->height++;
  }

  /* expand radix tree as needed */
  while (radix_max(root) < index)
  {
    /* check if the radix tree is getting too big */
    if (root->height == RADIX_TREE_MAX_HEIGHT)
      return (-E2BIG);

    // If the root radix level is not empty, we need to allocate a new radix level:
    if (node->count != 0)
    {
      node = (struct radix_tree_node *)malloc(sizeof(*node));
      if (node == NULL)
        return (-ENOMEM);
      node->slots[0] = root->rnode;
      node->count++;
      root->rnode = node;
    }
    root->height++;
  }

  /* get radix tree height index */
  height = root->height - 1;

  /* walk down the tree until the first missing node, if any */
  for (; height != 0; height--)
  {
    idx = radix_pos(index, height);
    if (node->slots[idx] == NULL)
      break;
    node = (struct radix_tree_node *)(node->slots[idx]);
  }

  /* allocate the missing radix levels, if any */
  for (idx = 0; idx != height; idx++)
  {
    temp[idx] = (struct radix_tree_node *)malloc(sizeof(*node));
    if (temp[idx] == NULL)
    {
      while (idx--)
        free(temp[idx]);
      /* Check if we should free the root node as well. */
      if (root->rnode->count == 0)
      {
        free(root->rnode);
        root->rnode = NULL;
        root->height = 0;
      }
      return (-ENOMEM);
    }
  }

  /* setup new radix levels, if any */
  for (; height != 0; height--)
  {
    idx = radix_pos(index, height);
    node->slots[idx] = temp[height - 1];
    node->count++;
    node = (struct radix_tree_node *)(node->slots[idx]);
  }

  // Insert and adjust count if the item does not already exist.
  idx = radix_pos(index, 0);
  if (node->slots[idx])
    return (-EEXIST);
  node->slots[idx] = item;
  node->count++;

  return (0);
}

} // namespace util
} // namespace mycc
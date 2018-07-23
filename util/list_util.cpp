
#include "list_util.h"
#include <stdlib.h>

namespace mycc
{
namespace util
{

namespace ad_list
{

/* Directions for iterators */
static const int ADLIST_START_HEAD = 0;
static const int ADLIST_START_TAIL = 1;

/* Create a new Adlist. The created Adlist can be freed with
 * AlFreeList(), but private value of every node need to be freed
 * by the user before to call AlFreeList().
 *
 * On error, NULL is returned. Otherwise the pointer to the new Adlist. */
Adlist *AdlistCreate(void)
{
  struct Adlist *Adlist;

  if ((Adlist = (struct Adlist *)malloc(sizeof(*Adlist))) == NULL)
    return NULL;
  Adlist->head = Adlist->tail = NULL;
  Adlist->len = 0;
  Adlist->dup = NULL;
  Adlist->free = NULL;
  Adlist->match = NULL;
  return Adlist;
}

/* Remove all the elements from the Adlist without destroying the Adlist itself. */
void AdlistEmpty(Adlist *Adlist)
{
  uint64_t len;
  AdlistNode *current, *next;

  current = Adlist->head;
  len = Adlist->len;
  while (len--)
  {
    next = current->next;
    if (Adlist->free)
      Adlist->free(current->value);
    free(current);
    current = next;
  }
  Adlist->head = Adlist->tail = NULL;
  Adlist->len = 0;
}

/* Free the whole Adlist.
 *
 * This function can't fail. */
void AdlistRelease(Adlist *Adlist)
{
  AdlistEmpty(Adlist);
  free(Adlist);
}

/* Add a new node to the Adlist, to head, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * Adlist remains unaltered).
 * On success the 'Adlist' pointer you pass to the function is returned. */
Adlist *AdlistAddNodeHead(Adlist *Adlist, void *value)
{
  AdlistNode *node;

  if ((node = (AdlistNode *)malloc(sizeof(*node))) == NULL)
    return NULL;
  node->value = value;
  if (Adlist->len == 0)
  {
    Adlist->head = Adlist->tail = node;
    node->prev = node->next = NULL;
  }
  else
  {
    node->prev = NULL;
    node->next = Adlist->head;
    Adlist->head->prev = node;
    Adlist->head = node;
  }
  Adlist->len++;
  return Adlist;
}

/* Add a new node to the Adlist, to tail, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * Adlist remains unaltered).
 * On success the 'Adlist' pointer you pass to the function is returned. */
Adlist *AdlistAddNodeTail(Adlist *Adlist, void *value)
{
  AdlistNode *node;

  if ((node = (AdlistNode *)malloc(sizeof(*node))) == NULL)
    return NULL;
  node->value = value;
  if (Adlist->len == 0)
  {
    Adlist->head = Adlist->tail = node;
    node->prev = node->next = NULL;
  }
  else
  {
    node->prev = Adlist->tail;
    node->next = NULL;
    Adlist->tail->next = node;
    Adlist->tail = node;
  }
  Adlist->len++;
  return Adlist;
}

Adlist *AdlistInsertNode(Adlist *Adlist, AdlistNode *old_node, void *value, int after)
{
  AdlistNode *node;

  if ((node = (AdlistNode *)malloc(sizeof(*node))) == NULL)
    return NULL;
  node->value = value;
  if (after)
  {
    node->prev = old_node;
    node->next = old_node->next;
    if (Adlist->tail == old_node)
    {
      Adlist->tail = node;
    }
  }
  else
  {
    node->next = old_node;
    node->prev = old_node->prev;
    if (Adlist->head == old_node)
    {
      Adlist->head = node;
    }
  }
  if (node->prev != NULL)
  {
    node->prev->next = node;
  }
  if (node->next != NULL)
  {
    node->next->prev = node;
  }
  Adlist->len++;
  return Adlist;
}

/* Remove the specified node from the specified Adlist.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */
void AdlistDelNode(Adlist *Adlist, AdlistNode *node)
{
  if (node->prev)
    node->prev->next = node->next;
  else
    Adlist->head = node->next;
  if (node->next)
    node->next->prev = node->prev;
  else
    Adlist->tail = node->prev;
  if (Adlist->free)
    Adlist->free(node->value);
  free(node);
  Adlist->len--;
}

/* Returns a Adlist iterator 'iter'. After the initialization every
 * call to AdlistNext() will return the next element of the Adlist.
 *
 * This function can't fail. */
AdlistIter *AdlistGetIterator(Adlist *Adlist, int direction)
{
  AdlistIter *iter;

  if ((iter = (AdlistIter *)malloc(sizeof(*iter))) == NULL)
    return NULL;
  if (direction == ADLIST_START_HEAD)
    iter->next = Adlist->head;
  else
    iter->next = Adlist->tail;
  iter->direction = direction;
  return iter;
}

/* Release the iterator memory */
void AdlistReleaseIterator(AdlistIter *iter)
{
  free(iter);
}

/* Create an iterator in the Adlist private iterator structure */
void AdlistRewind(Adlist *Adlist, AdlistIter *li)
{
  li->next = Adlist->head;
  li->direction = ADLIST_START_HEAD;
}

void AdlistRewindTail(Adlist *Adlist, AdlistIter *li)
{
  li->next = Adlist->tail;
  li->direction = ADLIST_START_TAIL;
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * AdlistDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the Adlist,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 *
 * iter = AdlistGetIterator(Adlist,<direction>);
 * while ((node = AdlistNext(iter)) != NULL) {
 *     doSomethingWith(AdlistNodeValue(node));
 * }
 *
 * */
AdlistNode *AdlistNext(AdlistIter *iter)
{
  AdlistNode *current = iter->next;

  if (current != NULL)
  {
    if (iter->direction == ADLIST_START_HEAD)
      iter->next = current->next;
    else
      iter->next = current->prev;
  }
  return current;
}

/* Duplicate the whole Adlist. On out of memory NULL is returned.
 * On success a copy of the original Adlist is returned.
 *
 * The 'Dup' method set with AdlistSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original Adlist both on success or error is never modified. */
Adlist *AdlistDup(Adlist *orig)
{
  Adlist *copy;
  AdlistIter iter;
  AdlistNode *node;

  if ((copy = AdlistCreate()) == NULL)
    return NULL;
  copy->dup = orig->dup;
  copy->free = orig->free;
  copy->match = orig->match;
  AdlistRewind(orig, &iter);
  while ((node = AdlistNext(&iter)) != NULL)
  {
    void *value;

    if (copy->dup)
    {
      value = copy->dup(node->value);
      if (value == NULL)
      {
        AdlistRelease(copy);
        return NULL;
      }
    }
    else
      value = node->value;
    if (AdlistAddNodeTail(copy, value) == NULL)
    {
      AdlistRelease(copy);
      return NULL;
    }
  }
  return copy;
}

/* Search the Adlist for a node matching a given key.
 * The match is performed using the 'match' method
 * set with AdlistSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */
AdlistNode *AdlistSearchKey(Adlist *Adlist, void *key)
{
  AdlistIter iter;
  AdlistNode *node;

  AdlistRewind(Adlist, &iter);
  while ((node = AdlistNext(&iter)) != NULL)
  {
    if (Adlist->match)
    {
      if (Adlist->match(node->value, key))
      {
        return node;
      }
    }
    else
    {
      if (key == node->value)
      {
        return node;
      }
    }
  }
  return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range NULL is returned. */
AdlistNode *AdlistIndex(Adlist *Adlist, long index)
{
  AdlistNode *n;

  if (index < 0)
  {
    index = (-index) - 1;
    n = Adlist->tail;
    while (index-- && n)
      n = n->prev;
  }
  else
  {
    n = Adlist->head;
    while (index-- && n)
      n = n->next;
  }
  return n;
}

/* Rotate the Adlist removing the tail node and inserting it to the head. */
void AdlistRotate(Adlist *Adlist)
{
  AdlistNode *tail = Adlist->tail;

  if (AdlistLength(Adlist) <= 1)
    return;

  /* Detach current tail */
  Adlist->tail = tail->prev;
  Adlist->tail->next = NULL;
  /* Move it as head */
  Adlist->head->prev = tail;
  tail->prev = NULL;
  tail->next = Adlist->head;
  Adlist->head = tail;
}

/* Add all the elements of the Adlist 'o' at the end of the
 * Adlist 'l'. The Adlist 'other' remains empty but otherwise valid. */
void AdlistJoin(Adlist *l, Adlist *o)
{
  if (o->head)
    o->head->prev = l->tail;

  if (l->tail)
    l->tail->next = o->head;
  else
    l->head = o->head;

  if (o->tail)
    l->tail = o->tail;
  l->len += o->len;

  /* Setup other as an empty Adlist. */
  o->head = o->tail = NULL;
  o->len = 0;
}

} // namespace ad_list

} // namespace util
} // namespace mycc
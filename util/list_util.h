
#ifndef MYCC_UTIL_LIST_UTIL_H_
#define MYCC_UTIL_LIST_UTIL_H_

namespace mycc
{
namespace util
{

/*
 * The definitions of this file are adopted from those which can be
 * found in the Linux kernel headers to enable people familiar with the
 * latter find their way in these sources as well.
 */

/* Basic type for the double-link list. */
struct list_head
{
  struct list_head *next, *prev;
};

/* Define a variable with the head and tail of the list. */
#define STRUCT_LIST_HEAD(name) \
  struct list_head name = {&(name), &(name)}

/* Initialize a new list head. */
#define INIT_LIST_HEAD(ptr) \
  (ptr)->next = (ptr)->prev = (ptr)

#define LIST_HEAD_INIT(name) \
  {                          \
    &(name), &(name)         \
  }

/* Add new element at the head of the list. */
inline void list_add(struct list_head *newp, struct list_head *head)
{
  head->next->prev = newp;
  newp->next = head->next;
  newp->prev = head;
  head->next = newp;
}

/* Add new element at the tail of the list. */
inline void list_add_tail(struct list_head *newp, struct list_head *head)
{
  head->prev->next = newp;
  newp->next = head;
  newp->prev = head->prev;
  head->prev = newp;
}

/* Remove element from list. */
inline void __list_del(struct list_head *prev, struct list_head *next)
{
  next->prev = prev;
  prev->next = next;
}

/* Remove element from list. */
inline void list_del(struct list_head *elem)
{
  __list_del(elem->prev, elem->next);
}

/* Remove element from list, initializing the element's list pointers. */
inline void list_del_init(struct list_head *elem)
{
  list_del(elem);
  INIT_LIST_HEAD(elem);
}

/* Delete from list, add to another list as head. */
inline void list_move(struct list_head *elem, struct list_head *head)
{
  __list_del(elem->prev, elem->next);
  list_add(elem, head);
}

/* Replace an old entry. */
inline void list_replace(struct list_head *old, struct list_head *newp)
{
  newp->next = old->next;
  newp->prev = old->prev;
  newp->prev->next = newp;
  newp->next->prev = newp;
}

/* Join two lists. */
inline void list_splice(struct list_head *add, struct list_head *head)
{
  /* Do nothing if the list which gets added is empty. */
  if (add != add->next)
  {
    add->next->prev = head;
    add->prev->next = head->next;
    head->next->prev = add->prev;
    head->next = add->next;
  }
}

/* Get typed element from list at a given position. */
#define list_entry(ptr, type, member) \
  ((type *)((char *)(ptr)-offsetof(type, member)))

/* Get first entry from a list. */
#define list_first_entry(ptr, type, member) \
  list_entry((ptr)->next, type, member)

/* Iterate forward over the elements of the list. */
#define list_for_each(pos, head) \
  for (pos = (head)->next; pos != (head); pos = pos->next)

/*
 * Iterate forward over the elements list. The list elements can be
 * removed from the list while doing this.
 */
#define list_for_each_safe(pos, p, head)  \
  for (pos = (head)->next, p = pos->next; \
       pos != (head);                     \
       pos = p, p = pos->next)

/* Iterate backward over the elements of the list. */
#define list_for_each_prev(pos, head) \
  for (pos = (head)->prev; pos != (head); pos = pos->prev)

/*
 * Iterate backwards over the elements list. The list elements can be
 * removed from the list while doing this.
 */
#define list_for_each_prev_safe(pos, p, head) \
  for (pos = (head)->prev, p = pos->prev;     \
       pos != (head);                         \
       pos = p, p = pos->prev)

inline int list_empty(struct list_head *head)
{
  return head == head->next;
}

inline void list_replace_init(struct list_head *old,
                              struct list_head *newp)
{
  struct list_head *head = old->next;

  list_del(old);
  list_add_tail(newp, head);
  INIT_LIST_HEAD(old);
}

/*
 * This is exactly the same as a normal list_head, except that it can be
 * declared volatile (e.g., if you have a list that may be accessed from signal
 * handlers).
 */
struct volatile_list_head
{
  volatile struct volatile_list_head *next, *prev;
};

#define VOLATILE_LIST_HEAD(name) \
  volatile struct volatile_list_head name = {&(name), &(name)}

inline void __volatile_list_del(volatile struct volatile_list_head *prev,
                                volatile struct volatile_list_head *next)
{
  next->prev = prev;
  prev->next = next;
}

inline void volatile_list_del(volatile struct volatile_list_head *elem)
{
  __volatile_list_del(elem->prev, elem->next);
}

inline int volatile_list_empty(volatile struct volatile_list_head *head)
{
  return head == head->next;
}

inline void volatile_list_add(volatile struct volatile_list_head *newp,
                              volatile struct volatile_list_head *head)
{
  head->next->prev = newp;
  newp->next = head->next;
  newp->prev = head;
  head->next = newp;
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_LIST_UTIL_H_
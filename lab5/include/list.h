/* include/list.h */
#pragma once
#include <stddef.h>

/* ========================================================================= */
/* CIRCULAR DOUBLY LINKED LIST IMPLEMENTATION                                */
/* (Mimics Linux Kernel list.h)                                              */
/* ========================================================================= */

struct list_head {
    struct list_head *next, *prev;
};

/* Initialize a list head to point to itself */
static inline void list_init(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

/* * FIX: Map INIT_LIST_HEAD to list_init 
 * timer.c calls INIT_LIST_HEAD(&timer_queue), so we need this macro.
 */
#define INIT_LIST_HEAD(ptr) list_init(ptr)

/* Add a new entry after the specified head (Stack behavior / Insert at Head) */
static inline void list_add(struct list_head *new_entry, struct list_head *head) {
    head->next->prev = new_entry;
    new_entry->next = head->next;
    new_entry->prev = head;
    head->next = new_entry;
}

/* Add a new entry before the specified head (Queue behavior / Insert at Tail) */
static inline void list_add_tail(struct list_head *new_entry, struct list_head *head) {
    head->prev->next = new_entry;
    new_entry->prev = head->prev;
    new_entry->next = head;
    head->prev = new_entry;
}

/* Remove an entry from the list */
static inline void list_del(struct list_head *entry) {
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    
    // Poison the entry to detect bugs
    entry->next = NULL;
    entry->prev = NULL;
}

/* * FIX: Add list_del_init
 * timer.c calls this to remove a timer from the queue.
 * Unlike list_del, this re-initializes the entry so it can be reused safely.
 */
static inline void list_del_init(struct list_head *entry) {
    list_del(entry);
    list_init(entry);
}

/* Check if the list is empty */
static inline int list_empty(struct list_head *head) {
    return head->next == head;
}

/* ========================================================================= */
/* MACRO HELPERS                                                             */
/* ========================================================================= */

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define container_of(ptr, type, member) ({          \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type,member) );})

#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)
    

#define list_for_each_safe(pos, n, head)  for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)
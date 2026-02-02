#pragma once
#include <stddef.h>

/* * Circular Doubly Linked List Implementation 
 * (Mimics Linux Kernel list.h)
 */

struct list_head {
    struct list_head *next, *prev;
};

/* Initialize a list head to point to itself */
static inline void list_init(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

/* Add a new entry after the specified head (Stack behavior) */
static inline void list_add(struct list_head *new_entry, struct list_head *head) {
    head->next->prev = new_entry;
    new_entry->next = head->next;
    new_entry->prev = head;
    head->next = new_entry;
}

/* Remove an entry from the list */
static inline void list_del(struct list_head *entry) {
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    
    // Optional: poison the entry to detect bugs
    entry->next = NULL;
    entry->prev = NULL;
}

/* Check if the list is empty */
static inline int list_empty(struct list_head *head) {
    return head->next == head;
}
#ifndef _NN_LLIST_H_
#define _NN_LLIST_H_

/**
 * Malloc free  linked list implementation.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nn_llist_node;

struct nn_llist_node {
    struct nn_llist_node* next;
    struct nn_llist_node* prev;

    void* item;
};

struct nn_llist {
    struct nn_llist_node sentinel;
};

struct nn_llist_iterator {
    const struct nn_llist* list;
    struct nn_llist_node* node;
};

/**
 * Init the list.
 */
void nn_llist_init(struct nn_llist* list);

/**
 * Deinit the list
 */
void nn_llist_deinit(struct nn_llist* list);

/*
 * Initialize a node which is later used in a list
 */
void nn_llist_node_init(struct nn_llist_node* node);

/**
 * Test if a node is used in a list, init on the node has to be called
 * before this function works correctly.
 */
bool nn_llist_node_in_list(struct nn_llist_node* node);

// return true if the list is empty
bool nn_llist_empty(const struct nn_llist* list);

/**
 * return the size of the list, O(n)
 */
size_t nn_llist_size(const struct nn_llist* list);

/**
 * Add an item to the end of the list.
 *
 * The node is inserted into the list and the item pointer is stored
 * in the node.
 *
 * An iterator pointing to the newly inserted element is returned.
 */
struct nn_llist_iterator nn_llist_append(struct nn_llist* list, struct nn_llist_node* node, void* item);

/**
 * Insert node and item right before the element the iterator is pointing at.
 */
void nn_llist_insert_before(struct nn_llist_iterator* iterator, struct nn_llist_node* node, void* item);

/**
 * Remove the node from the list pointed to by the iterator
 */
void nn_llist_erase(struct nn_llist_iterator* iterator);

/**
 * remove the node from the list
 */
void nn_llist_erase_node(struct nn_llist_node* node);

/**
 * Get an iterator pointing to the start of the list
 */
struct nn_llist_iterator nn_llist_begin(const struct nn_llist* list);

/**
 * Create an iterator pointing at the end of the list
 */
struct nn_llist_iterator nn_llist_end(const struct nn_llist* list);

/**
 * Increment the iterator to the next item
 */
void nn_llist_next(struct nn_llist_iterator* iterator);

/**
 * Query if the end of the list has been reached
 */
bool nn_llist_is_end(const struct nn_llist_iterator* iterator);

/**
 * Get an item from the current iterator position
 */
void* nn_llist_get_item(const struct nn_llist_iterator* iterator);

/**
 * Iterate over the list
 *
 * struct nn_llist list;
 * ...
 * void* item;
 * NN_LIST_FOREACH(item, &list) {
 *   ...
 * }
 */
#define NN_LLIST_FOREACH(item, list) for (struct nn_llist_iterator it = nn_llist_begin(list); item = nn_llist_get_item(&it), !nn_llist_is_end(&it); nn_llist_next(&it))

#ifdef __cplusplus
} //extern "C"
#endif

#endif

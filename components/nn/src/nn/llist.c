#include <nn/llist.h>

void nn_llist_init(struct nn_llist* llist)
{
    llist->sentinel.next = &llist->sentinel;
    llist->sentinel.prev = &llist->sentinel;
    llist->sentinel.item = NULL;
}


void nn_llist_deinit(struct nn_llist* llist)
{

}

void nn_llist_node_init(struct nn_llist_node* node)
{
    node->next = node;
    node->prev = node;
    node->item = NULL;
}

bool nn_llist_node_in_list(struct nn_llist_node* node)
{
    // if a node is not pointing to itself it is in a list.
    return (node->next != node && node->prev != node);
}

// return true if the llist is empty
bool nn_llist_empty(const struct nn_llist* llist)
{
    return llist->sentinel.next == &llist->sentinel;
}

size_t nn_llist_size(const struct nn_llist* llist)
{
    size_t size = 0;
    struct nn_llist_iterator it;
    for (it = nn_llist_begin(llist); !nn_llist_is_end(&it); nn_llist_next(&it))
    {
        size++;
    }
    return size;
}

// add an node to the end of the llist
struct nn_llist_iterator nn_llist_append(struct nn_llist* llist, struct nn_llist_node* node, void* item)
{
    node->item = item;
    struct nn_llist_node* before = llist->sentinel.prev;
    struct nn_llist_node* after = before->next;

    before->next = node;
    node->next = after;
    after->prev = node;
    node->prev = before;

    struct nn_llist_iterator it;
    it.list = llist;
    it.node = node;
    return it;
}

void nn_llist_insert_before(struct nn_llist_iterator* iterator, struct nn_llist_node* node, void* item)
{
    node->item = item;

    struct nn_llist_node* before = iterator->node->prev;
    struct nn_llist_node* after = iterator->node;

    before->next = node;
    node->next = after;
    after->prev = node;
    node->prev = before;
}

// erase an node from the llist.
void nn_llist_erase_node(struct nn_llist_node* node)
{
    struct nn_llist_node* before = node->prev;
    struct nn_llist_node* after = node->next;
    before->next = after;
    after->prev = before;

    nn_llist_node_init(node);
}

void nn_llist_erase(struct nn_llist_iterator* iterator)
{
    nn_llist_erase_node(iterator->node);
}

// return iterator pointing to the front of the llist
struct nn_llist_iterator nn_llist_begin(const struct nn_llist* llist)
{
    struct nn_llist_iterator iterator;
    iterator.list = llist;
    iterator.node = llist->sentinel.next;
    return iterator;
}

void nn_llist_next(struct nn_llist_iterator* iterator)
{
    iterator->node = iterator->node->next;
}

struct nn_llist_iterator nn_llist_end(const struct nn_llist* llist)
{
    struct nn_llist_iterator iterator;
    iterator.list = llist;
    iterator.node = (struct nn_llist_node*)&llist->sentinel;
    return iterator;
}

bool nn_llist_is_end(const struct nn_llist_iterator* iterator)
{
    return iterator->node == &iterator->list->sentinel;
}

struct nn_llist_node* nn_llist_get_node(const struct nn_llist_iterator* iterator)
{
    return iterator->node;
}

void* nn_llist_get_item(const struct nn_llist_iterator* iterator)
{
    return iterator->node->item;
}

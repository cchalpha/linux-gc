#ifndef _LINUX_SKIP_LISTS_H
#define _LINUX_SKIP_LISTS_H

#include <linux/kernel.h>

typedef u64 keyType;
typedef void *valueType;

#define NUM_SKIPLIST_LEVEL (16)
#define INVALID_SKIPLIST_LEVEL (~0x00)

struct skiplist_node {
	int level;	/* Levels in this node */
	struct skiplist_node *next[NUM_SKIPLIST_LEVEL];
	struct skiplist_node *prev[NUM_SKIPLIST_LEVEL];
	/* these two only for compatible */
	keyType key;
};

/*#define SKIPLIST_NODE_INIT(name) { INVALID_SKIPLIST_LEVEL }*/
#define SKIPLIST_NODE_INIT(name) { 0,\
				   {&name, &name, &name, &name,\
				    &name, &name, &name, &name,\
				    &name, &name, &name, &name,\
				    &name, &name, &name, &name},\
				   {&name, &name, &name, &name,\
				    &name, &name, &name, &name,\
				    &name, &name, &name, &name,\
				    &name, &name, &name, &name},\
				   ~0x00}

static inline void INIT_SKIPLIST_NODE(struct skiplist_node *node)
{
	int i;

	node->level = 0;
	for (i = 0; i < NUM_SKIPLIST_LEVEL; i++) {
		WRITE_ONCE(node->next[i], node);
		node->prev[i] = node;
	}
	/* Need this in insert operation, should be fix */
	node->key = ~0x00;
}

/**
 * skiplist_empty - test whether a skip list is empty
 * @head: the skip list to test.
 */
static inline int skiplist_empty(const struct skiplist_node *head)
{
	return READ_ONCE(head->next[0]) == head;
}

/**
 * skiplist_entry - get the struct for this entry
 * @ptr: the &struct skiplist_node pointer.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the skiplist_node within the struct.
 */
#define skiplist_entry(ptr, type, member) \
	container_of(ptr, type, member)

void skiplist_del_init(struct skiplist_node *head, struct skiplist_node *node);
void skiplist_insert(struct skiplist_node *head, struct skiplist_node *node);
#endif /* _LINUX_SKIP_LISTS_H */

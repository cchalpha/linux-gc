#ifndef _LINUX_SKIP_LISTS_H
#define _LINUX_SKIP_LISTS_H
typedef u64 keyType;
typedef void *valueType;

typedef struct nodeStructure skiplist_node;

struct nodeStructure {
	uint8_t level;	/* Levels in this structure */
	keyType key;
	valueType value;
	skiplist_node *next[16];
	skiplist_node *prev[16];
};

typedef struct listStructure {
	uint8_t level;	/* Maximum level of the list
			(1 more than the number of levels in the list) */
	skiplist_node *header; /* pointer to header */
} skiplist;

skiplist_node *skiplist_init(void);
skiplist *new_skiplist(skiplist_node *slnode);
skiplist_node *skiplist_insert(skiplist_node *slnode, skiplist *l, keyType key, valueType value, u64 randseed);
void skiplist_delnode(skiplist_node *slnode, skiplist *l, skiplist_node *node);
#endif /* _LINUX_SKIP_LISTS_H */

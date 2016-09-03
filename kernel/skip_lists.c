/*
  Copyright (C) 2011,2016 Con Kolivas.

  Code based on example originally by William Pugh.

Skip Lists are a probabilistic alternative to balanced trees, as
described in the June 1990 issue of CACM and were invented by
William Pugh in 1987.

A couple of comments about this implementation:
The routine randomLevel has been hard-coded to generate random
levels using p=0.25. It can be easily changed.

The insertion routine has been implemented so as to use the
dirty hack described in the CACM paper: if a random level is
generated that is more than the current maximum level, the
current maximum level plus one is used instead.

Levels start at zero and go up to MaxLevel (which is equal to
	(MaxNumberOfLevels-1).

The routines defined in this file are:

init: defines slnode

new_skiplist: returns a new, empty list

randomLevel: Returns a random level based on a u64 random seed passed to it.
In BFS, the "niffy" time is used for this purpose.

insert(l,key, value): inserts the binding (key, value) into l. This operation
occurs in O(log n) time.

delnode(slnode, l, node): deletes any binding of key from the l based on the
actual node value. This operation occurs in O(k) time where k is the
number of levels of the node in question (max 16). The original delete
function occurred in O(log n) time and involved a search.

BFS Notes: In this implementation of skiplists, there are bidirectional
next/prev pointers and the insert function returns a pointer to the actual
node the value is stored. The key here is chosen by the scheduler so as to
sort tasks according to the priority list requirements and is no longer used
by the scheduler after insertion. The scheduler lookup, however, occurs in
O(1) time because it is always the first item in the level 0 linked list.
Since the task struct stores a copy of the node pointer upon skiplist_insert,
it can also remove it much faster than the original implementation with the
aid of prev<->next pointer manipulation and no searching.

*/

#include <linux/slab.h>
#include <linux/skip_lists.h>

#define MaxNumberOfLevels 16 /* Fit within a uint8_t */
#define MaxLevel (MaxNumberOfLevels - 1)
#define newNode kmalloc(sizeof(skiplist_node), GFP_ATOMIC)

skiplist_node *skiplist_init(void)
{
	skiplist_node *slnode = newNode;
	int i;

	BUG_ON(!slnode);
	slnode->key = 0xFFFFFFFFFFFFFFFF;
	slnode->level = 0;
	slnode->value = NULL;
	for (i = 0; i < MaxNumberOfLevels; i++)
		slnode->next[i] = slnode->prev[i] = slnode;
	return slnode;
}

skiplist *new_skiplist(skiplist_node *slnode)
{
	skiplist *l = kmalloc(sizeof(skiplist), GFP_ATOMIC);

	BUG_ON(!l);
	l->level = 0;
	l->header = slnode;
	return l;
}

void free_skiplist(skiplist_node *slnode, skiplist *l)
{
	skiplist_node *p, *q;

	p = l->header;
	do {
		q = p->next[0];
		p->next[0]->prev[0] = q->prev[0];
		kfree(p);
		p = q;
	} while (p != slnode);
	kfree(l);
}

static inline uint8_t randomLevel(u64 randseed)
{
	return randseed & 0xF;
}

skiplist_node *skiplist_insert(skiplist_node *slnode, skiplist *l, keyType key, valueType value, u64 randseed)
{
	skiplist_node *update[MaxNumberOfLevels];
	skiplist_node *p, *q;
	int k = l->level;

	p = slnode;
	do {
		while (q = p->next[k], q->key <= key)
			p = q;
		update[k] = p;
	} while (--k >= 0);

	k = randomLevel(randseed);
	if (k > l->level) {
		k = ++l->level;
		update[k] = slnode;
	}

	q = newNode;
	q->level = k;
	q->key = key;
	q->value = value;
	do {
		p = update[k];
		q->next[k] = p->next[k];
		p->next[k] = q;
		q->prev[k] = p;
		q->next[k]->prev[k] = q;
	} while (--k >= 0);
	return q;
}

void skiplist_delnode(skiplist_node *slnode, skiplist *l, skiplist_node *node)
{
	int k, m = node->level;

	for (k = 0; k <= m; k++) {
		node->prev[k]->next[k] = node->next[k];
		node->next[k]->prev[k] = node->prev[k];
	}
	kfree(node);
	if (m == l->level) {
		while (l->header->next[m] == slnode && l->header->prev[m] == slnode && m > 0)
			m--;
		l->level = m;
	}
}

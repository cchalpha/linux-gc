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
MaxNumberOfLevels-1).

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

#include <linux/skip_lists.h>

void skiplist_del_init(struct skiplist_node *head, struct skiplist_node *node)
{
	int l, m = node->level;

	for (l = 0; l <= m; l++) {
		node->prev[l]->next[l] = node->next[l];
		node->next[l]->prev[l] = node->prev[l];
	}
	if (m == head->level) {
		while (head->next[m] == head && head->prev[m] == head && m > 0)
			m--;
		head->level = m;
	}
	INIT_SKIPLIST_NODE(node);
}

void skiplist_insert(struct skiplist_node *head, struct skiplist_node *node)
{
	struct skiplist_node *update[NUM_SKIPLIST_LEVEL];
	struct skiplist_node *p, *q;
	int k = head->level;
	u64 key = node->key;

	p = head;
	do {
		while (q = p->next[k], q->key <= key)
			p = q;
		update[k] = p;
	} while (--k >= 0);

	k = node->level;
	if (k > head->level) {
		k = ++head->level;
		update[k] = head;
	}

	node->level = k;
	q = node;
	do {
		p = update[k];
		q->next[k] = p->next[k];
		p->next[k] = q;
		q->prev[k] = p;
		q->next[k]->prev[k] = q;
	} while (--k >= 0);
}

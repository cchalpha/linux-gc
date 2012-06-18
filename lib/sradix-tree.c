#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/gcd.h>
#include <linux/sradix-tree.h>

/*
 *	Extend a sradix tree so it can store key @index.
 */
static int sradix_tree_extend(struct sradix_tree_root *root, unsigned long index)
{
	struct sradix_tree_node *node;
	unsigned int height;

	if (unlikely(root->rnode == NULL)) {
		if (!(node = sradix_tree_node_alloc(root)))
			return -ENOMEM;

		node->height = 1;
		node->count = 1;
		root->rnode = node;
		root->height = 1;
	}

	/* Figure out what the height should be.  */
	height = root->height;
	index >>= root->shift * height;

	while (index) {
		index >>= root->shift;
		height++;
	}

	while (height > root->height) {
		unsigned int newheight;
		if (!(node = root->alloc()))
			return -ENOMEM;

		/* Increase the height.  */
		node->stores[0] = root->rnode;
		root->rnode->parent = node;
		if (root->extend)
			root->extend(node, root->rnode);

		newheight = root->height + 1;
		node->height = newheight;
		node->count = 1;
		root->rnode = node;
		root->height = newheight;
	}

	return 0;
}

/*
 * Search the next item from the current node, that is not NULL
 * and can satify root->iter().
 */
void *sradix_tree_next(struct sradix_tree_root *root,
		       struct sradix_tree_node *node, unsigned long index,
		       int (*iter)(void *, unsigned long))
{
	unsigned long height, shift, offset;
	int error;
	void **ret;
	void *item;

	if (unlikely(node == NULL)) {
		node = root->rnode;
		for (;offset < root->stores_size; offset++) {
			item = node->stores[offset];
			if (item && (!iter || iter(item, offset)))
				break;
		}

		if (unlikely(offset >= root->stores_size))
			return NULL;

		if (node->height == 1)
			return item;
		else
			goto go_down;
	}

	while (node) {
		offset = (index & root->mask) + 1;					
		for (;offset < root->stores_size; offset++) {
			item = node->stores[offset];
			if (item && (!iter || iter(item, offset)))
				break;
		}

		if (offset < root->stores_size)
			break;

		node = node->parent;
		index >>= root->shift;
	}

	if (!node)
		return NULL;

	while (node->height > 1) {
go_down:
		node = item;
		for (offset = 0; offset < root->stores_size; offset++) {
			item = node->stores[offset];
			if (item && (!iter || iter(item, offset)))
				break;
		}

		if (offset < root->stores_size)
			break;
	}

	BUG_ON(offset > root->stores_size);

	return item;
}

/*
 * Blindly insert the item to the tree. Typically, we reuse the
 * first empty store item.
 */
int sradix_tree_enter(struct sradix_tree_root *root, void **item, int num)
{
	unsigned long index;
	unsigned int shift, height;
	struct sradix_tree_node *node, *node_saved;
	void **store;
	int error, i, j;


	node = root->rnode;

redo:
	index = root->min;
	if (node == NULL || (index >> (root->shift * root->height))) {
		error = sradix_tree_extend(root, index);
		if (error)
			return NULL;

		node = root->rnode;
	}

	height = node->height;
	shift = (height - 1) * root->shift;

	while (height > 0) {
		int offset, offset_saved;

		offset = (index >> shift) & root->mask;
		node_saved = node;
		offset_saved = offset;
		for (; offset < root->stores_size; offset++) {
			store = &node->stores[offset];
			node = *store;

			if (!node || node->fulls != root->stores_size)
				break;
		}
		if (offset != offset_saved)
			index = 0;

		if (!node && height > 1) {
			if (!(node = root->alloc()))
				return -ENOMEM;

			node->height = height;
			*store = node;
			node->parent = node_saved;
			node_saved->count++;
			if (root->extend)
				root->extend(node_saved, node);
		}

		shift -= root->shift;
		height--;
	}

	BUG_ON(*store);

	node = node_saved;
	for (i = 0, j = 0;
	      j < root->stores_size - node->count && 
	      i < root->stores_size - offset && j < num; i++) {
		if (!store[i]) {
			store[i] = item[j];
			if (root->assign)
				root->assign(node, index + i, item[j]);
			j++;
		}
	}

	node->count += j;
	node->fulls += j;
	num -= j;

	while (node->fulls == root->stores_size) {
		node = node->parent;
		if (!node)
			break;

		node->fulls++;
	}

	if (unlikely(!node)) {
		/* All nodes are full */
		root->min = 1 << (root->height * root->shift);
	}

	if (num) {
		item += j;
		goto redo;
	}

	return 0;
}


/**
 *	sradix_tree_shrink    -    shrink height of a sradix tree to minimal
 *	@root		sradix tree root
 */
static inline void sradix_tree_shrink(struct sradix_tree_root *root)
{
	/* try to shrink tree height */
	while (root->height > 1) {
		struct sradix_tree_node *to_free = root->rnode;

		/*
		 * The candidate node has more than one child, or its child
		 * is not at the leftmost store, we cannot shrink.
		 */
		if (to_free->count != 1 || !to_free->stores[0])
			break;

		root->rnode = to_free->stores[0];
		root->height--;
		root->free(to_free);
	}
}

/*
 * Del the item on the known leaf node and index
 */
void sradix_tree_delete_node(struct sradix_tree_root *root, struct sradix_tree_node *node, unsigned long index)
{
	unsigned int offset, shift = 0;
	struct sradix_tree_node *start, *end;

	BUG_ON(node->height != 1);

	start = node;
	while (node && !(--node->count))
		node = node->parent;

	end = node;
	if (!node)
		root->rnode = NULL;
	else {
		offset = (index >> (root->shift * (node->height - 1))) & root->mask;
		if (root->rm)
			root->rm(node, offset);
		node->stores[offset] = NULL;

		while (node->fulls == root->stores_size - 1) {
			node = node->parent;
			if (!node)
				break;
			node->fulls--;
		}
	}

	if (start != end) {
		sradix_tree_shrink(root);

		do {
			node = start;
			root->free(node);
			start = start->parent;
		} while (start != end);
	}
}

void *sradix_tree_lookup(struct sradix_tree_root *root, unsigned long index)
{
	unsigned int shift, height, offset;
	struct sradix_tree_node *node;

	node = root->rnode;
	if (node == NULL || (index >> (root->shift * root->height)))
		return NULL;

	height = root->height;
	shift = (height - 1) * root->shift;

	do {	
		offset = (index >> shift) & root->mask;
		node = node->stores[offset];
		if (!node)
			return NULL;

		shift -= root->shift;
		height--;
	} while (height > 0);

	return node;
}

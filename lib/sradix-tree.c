#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/gcd.h>
#include <linux/sradix-tree.h>


static inline int sradix_node_full(struct sradix_tree_root *root, struct sradix_tree_node *node)
{
	return node->fulls == root->stores_size || 
		(node->height == 1 && node->count == root->stores_size);
}

/*
 *	Extend a sradix tree so it can store key @index.
 */
static int sradix_tree_extend(struct sradix_tree_root *root, unsigned long index)
{
	struct sradix_tree_node *node;
	unsigned int height;

	if (unlikely(root->rnode == NULL)) {
		if (!(node = root->alloc()))
			return -ENOMEM;

		node->height = 1;
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
		if (sradix_node_full(root, root->rnode))
			node->fulls = 1;

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
	unsigned long offset;
	void *item;

	if (unlikely(node == NULL)) {
		node = root->rnode;
		for (offset = 0; offset < root->stores_size; offset++) {
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
__attribute__((optimize(0))) int 
sradix_tree_enter(struct sradix_tree_root *root, void **item, int num)
{
	unsigned long index;
	unsigned int height;
	struct sradix_tree_node *node, *tmp;
	int offset, offset_saved;
	void **store = NULL;
	int error, i, j, shift;


	node = root->rnode;

redo:
	index = root->min;
	if (node == NULL || (index >> (root->shift * root->height))
	    || sradix_node_full(root, root->rnode)) {
		error = sradix_tree_extend(root, index);
		if (error)
			return error;

		node = root->rnode;
	}

	height = node->height;
	shift = (height - 1) * root->shift;

	printk(KERN_ERR "index=%lu height=%u shift=%u root->height=%u", index, height, shift, root->height);

	offset = (index >> shift) & root->mask;
	while (shift > 0) {
		printk(KERN_ERR "!!!! offset=%d", offset);
		offset_saved = offset;
		for (; offset < root->stores_size; offset++) {
			store = &node->stores[offset];
			tmp = *store;

			printk(KERN_ERR "increase offset=%d", offset);
			if (!tmp || !sradix_node_full(root, tmp))
				break;
		}
		if (offset != offset_saved) {
			index += (offset - offset_saved) << shift;
			index &= ~((1UL << shift) - 1);
		}

		printk(KERN_ERR "****index=%lu shift=%u", index, shift);
		if (!tmp) {
			if (!(tmp = root->alloc()))
				return -ENOMEM;

			tmp->height = shift / root->shift;
			*store = tmp;
			tmp->parent = node;
			node->count++;
			if (root->extend)
				root->extend(node, tmp);
		}

		node = tmp;
		shift -= root->shift;
		offset = (index >> shift) & root->mask;
	}

	BUG_ON(node->height != 1);


	store = &node->stores[offset];
	for (i = 0, j = 0;
	      j < root->stores_size - node->count && 
	      i < root->stores_size - offset && j < num; i++) {
		if (!store[i]) {
			store[i] = item[j];
			printk(KERN_ERR "Assign item %d at index=%d", j, index + i);
			if (root->assign)
				root->assign(node, index + i, item[j]);
			j++;
		}
	}

	node->count += j;
	num -= j;
	root->min = index + i;

	while (sradix_node_full(root, node)) {
		node = node->parent;
		if (!node)
			break;

		printk(KERN_ERR "node at height=%u full", node->height);
		node->fulls++;
	}

	if (unlikely(!node)) {
		/* All nodes are full */
		root->min = 1 << (root->height * root->shift);
		printk(KERN_ERR "all nodes full root->min=%lu", root->min);
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
void sradix_tree_delete_from_leaf(struct sradix_tree_root *root, 
				  struct sradix_tree_node *node, unsigned long index)
{
	unsigned int offset;
	struct sradix_tree_node *start, *end;

	BUG_ON(node->height != 1);

	start = node;
	while (node && !(--node->count))
		node = node->parent;

	end = node;
	if (!node) {
		root->rnode = NULL;
		goto free_nodes;
	} else {
		offset = (index >> (root->shift * (node->height - 1))) & root->mask;
		if (root->rm)
			root->rm(node, offset);
		node->stores[offset] = NULL;
	}

	if (start != end) {
		sradix_tree_shrink(root);

free_nodes:
		do {
			node = start;
			root->free(node);
			start = start->parent;
		} while (start != end);
	} else if (node->count == root->stores_size - 1) {
		/* It WAS a full leaf node. Update the ancestors */
		node = node->parent;
		while (node) {
			node->fulls--;
			if (node->fulls != root->stores_size - 1)
				break;

			node = node->parent;
		}
	}

	if (root->min > index)
		root->min = index;
}

void *sradix_tree_lookup(struct sradix_tree_root *root, unsigned long index)
{
	unsigned int height, offset;
	struct sradix_tree_node *node;
	int shift;

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
	} while (shift >= 0);

	return node;
}

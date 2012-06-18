#ifndef _LINUX_SRADIX_TREE_H
#define _LINUX_SRADIX_TREE_H


#define INIT_SRADIX_TREE(root, mask)					\
do {									\
	(root)->height = 0;						\
	(root)->gfp_mask = (mask);					\
	(root)->rnode = NULL;						\
} while (0)

#define ULONG_BITS	(sizeof(unsigned long) * 8)
#define SRADIX_TREE_INDEX_BITS  (8 /* CHAR_BIT */ * sizeof(unsigned long))
//#define SRADIX_TREE_MAP_SHIFT	6
//#define SRADIX_TREE_MAP_SIZE	(1UL << SRADIX_TREE_MAP_SHIFT)
//#define SRADIX_TREE_MAP_MASK	(SRADIX_TREE_MAP_SIZE-1)

struct sradix_tree_node {
	unsigned int	height;		/* Height from the bottom */
	unsigned int	count;			/* if count < 0, then all sub levels are full*/
	unsigned int	fulls;		/* Number of full sublevel trees */ 
	struct sradix_tree_node *parent;
	void *stores[0];
};

/* A simple radix tree implementation */
struct sradix_tree_root {
        unsigned int            height;
        struct sradix_tree_node *rnode;
	unsigned int shift;
	unsigned int stores_size;
	unsigned int mask;
	unsigned long min;	/* The first hole index */
	//unsigned long *height_to_maxindex;

	/* How the node is allocated and freed. */
	struct sradix_tree_node *(*alloc)(void); 
	struct void (*free)(struct sradix_tree_node *node);

	/* When a new node is added and removed */
	void (*extend)(struct sradix_tree_node *parent, struct sradix_tree_node *child);
	void (*assign)(struct sradix_tree_node *node, unsigned index, void *item);
	void (*rm)(struct sradix_tree_node *node, unsigned offset, void *item);
};

struct sradix_tree_path {
	struct sradix_tree_node *node;
	int offset;
};

static inline 
int init_sradix_tree_root(struct sradix_tree_root *root, unsigned long shift)
{
	root->height = 0;
	root->rnode = NULL;
	root->shift = shift;
	root->stores_size = 1UL << shift;
	root->mask = root->stores_size - 1;
}

#endif /* _LINUX_SRADIX_TREE_H */

// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2014, Volkan Yazıcı <volkan.yazici@gmail.com>
 * All rights reserved.
 */

#include "../nvmev.h"
#include "pqueue.h"

#define left(i) ((i) << 1)
#define right(i) (((i) << 1) + 1)
#define parent(i) ((i) >> 1)

pqueue_t *pqueue_init(size_t n, pqueue_cmp_pri_f cmppri, pqueue_get_pri_f getpri,
		      pqueue_set_pri_f setpri, pqueue_get_pos_f getpos, pqueue_set_pos_f setpos)
{
	pqueue_t *q;

	pr_info_once(NVMEV_DRV_NAME ": pqueue: "
		     "Copyright (c) 2014, Volkan Yazıcı <volkan.yazici@gmail.com>. "
		     "All rights reserved.\n");

	if (!(q = kmalloc(sizeof(pqueue_t), GFP_KERNEL)))
		return NULL;

	/* Need to allocate n+1 elements since element 0 isn't used. */
	NVMEV_DEBUG("{alloc} n=%ld, size=%ld\n", n, (n + 1) * sizeof(void *));
	if (!(q->d = kmalloc((n + 1) * sizeof(void *), GFP_KERNEL))) {
		kfree(q);
		return NULL;
	}

	q->size = 1;
	q->avail = q->step = (n + 1); /* see comment above about n+1 */
	q->cmppri = cmppri;
	q->setpri = setpri;
	q->getpri = getpri;
	q->getpos = getpos;
	q->setpos = setpos;

	return q;
}

void pqueue_free(pqueue_t *q)
{
	kfree(q->d);
	kfree(q);
}

size_t pqueue_size(pqueue_t *q)
{
	/* queue element 0 exists but doesn't count since it isn't used. */
	return (q->size - 1);
}

static void bubble_up(pqueue_t *q, size_t i)
{
	size_t parent_node;
	void *moving_node = q->d[i];
	pqueue_pri_t moving_pri = q->getpri(moving_node);

	for (parent_node = parent(i);
	     ((i > 1) && q->cmppri(q->getpri(q->d[parent_node]), moving_pri));
	     i = parent_node, parent_node = parent(i)) {
		q->d[i] = q->d[parent_node];
		q->setpos(q->d[i], i);
	}

	q->d[i] = moving_node;
	q->setpos(moving_node, i);
}

static size_t maxchild(pqueue_t *q, size_t i)
{
	size_t child_node = left(i);

	if (child_node >= q->size)
		return 0;

	if ((child_node + 1) < q->size &&
	    q->cmppri(q->getpri(q->d[child_node]), q->getpri(q->d[child_node + 1])))
		child_node++; /* use right child instead of left */

	return child_node;
}

static void percolate_down(pqueue_t *q, size_t i)
{
	size_t child_node;
	void *moving_node = q->d[i];
	pqueue_pri_t moving_pri = q->getpri(moving_node);

	while ((child_node = maxchild(q, i)) &&
	       q->cmppri(moving_pri, q->getpri(q->d[child_node]))) {
		q->d[i] = q->d[child_node];
		q->setpos(q->d[i], i);
		i = child_node;
	}

	q->d[i] = moving_node;
	q->setpos(moving_node, i);
}

int pqueue_insert(pqueue_t *q, void *d)
{
	//void *tmp;
	size_t i;
	//size_t newsize;

	if (!q)
		return 1;

	/* allocate more memory if necessary */
	if (q->size >= q->avail) {
		NVMEV_ERROR("Need more space in pqueue\n");
		// newsize = q->size + q->step;
		// if (!(tmp = realloc(q->d, sizeof(void *) * newsize)))
		//     return 1;
		// q->d = tmp;
		// q->avail = newsize;
	}

	/* insert item */
	i = q->size++;
	q->d[i] = d;
	bubble_up(q, i);

	return 0;
}

void pqueue_change_priority(pqueue_t *q, pqueue_pri_t new_pri, void *d)
{
	size_t posn;
	pqueue_pri_t old_pri = q->getpri(d);

	q->setpri(d, new_pri);
	posn = q->getpos(d);
	if (q->cmppri(old_pri, new_pri))
		bubble_up(q, posn);
	else
		percolate_down(q, posn);
}

int pqueue_remove(pqueue_t *q, void *d)
{
	size_t posn = q->getpos(d);
	q->d[posn] = q->d[--q->size];
	if (q->cmppri(q->getpri(d), q->getpri(q->d[posn])))
		bubble_up(q, posn);
	else
		percolate_down(q, posn);

	return 0;
}

void *pqueue_pop(pqueue_t *q)
{
	void *head;

	if (!q || q->size == 1)
		return NULL;

	head = q->d[1];
	q->d[1] = q->d[--q->size];
	percolate_down(q, 1);

	return head;
}

void *pqueue_peek(pqueue_t *q)
{
	void *d;

	if (!q || q->size == 1)
		return NULL;
	d = q->d[1];
	return d;
}

#if 0
void pqueue_dump(pqueue_t *q, FILE *out, pqueue_print_entry_f print)
{
    int i;

    fprintf(stdout,"posn\tleft\tright\tparent\tmaxchild\t...\n");
    for (i = 1; i < q->size ;i++) {
        fprintf(stdout,
                "%d\t%d\t%d\t%d\t%ul\t",
                i,
                left(i), right(i), parent(i),
                (unsigned int)maxchild(q, i));
        print(out, q->d[i]);
    }
}

static void set_pos(void *d, size_t val)
{
    /* do nothing */
}

static void set_pri(void *d, pqueue_pri_t pri)
{
    /* do nothing */
}

void pqueue_print(pqueue_t *q, FILE *out, pqueue_print_entry_f print)
{
    pqueue_t *dup;
	void *e;

    dup = pqueue_init(q->size, q->cmppri, q->getpri, set_pri, q->getpos, set_pos);
    dup->size = q->size;
    dup->avail = q->avail;
    dup->step = q->step;

    memcpy(dup->d, q->d, (q->size * sizeof(void *)));

    while ((e = pqueue_pop(dup)))
		print(out, e);

    pqueue_free(dup);
}
#endif

static int subtree_is_valid(pqueue_t *q, int pos)
{
	if (left(pos) < q->size) {
		/* has a left child */
		if (q->cmppri(q->getpri(q->d[pos]), q->getpri(q->d[left(pos)])))
			return 0;
		if (!subtree_is_valid(q, left(pos)))
			return 0;
	}
	if (right(pos) < q->size) {
		/* has a right child */
		if (q->cmppri(q->getpri(q->d[pos]), q->getpri(q->d[right(pos)])))
			return 0;
		if (!subtree_is_valid(q, right(pos)))
			return 0;
	}
	return 1;
}

int pqueue_is_valid(pqueue_t *q)
{
	return subtree_is_valid(q, 1);
}

/*
 * Copyright (C) 2002 Free Software Foundation, Inc.
 * (originally part of the GNU C Library and Userspace RCU)
 * Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.
 *
 * Copyright (C) 2009 Pierre-Marc Fournier
 * Conversion to RCU list.
 * Copyright (C) 2010 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 */

#ifndef LIST_H
#define LIST_H

/* git v2.53.0 list.h:46-47 */
#define INIT_LIST_HEAD(ptr) \
	(ptr)->next = (ptr)->prev = (ptr)

/* git v2.53.0 list.h:121-122 */
#define list_entry(ptr, type, member) \
	((type *) ((char *) (ptr) - offsetof(type, member)))

/* git v2.53.0 list.h:129-130 */
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

/* git v2.53.0 list.h:169-205 */
/*
 * This is exactly the same as a normal list_head, except that it can be
 * declared volatile (e.g., if you have a list that may be accessed from signal
 * handlers).
 */
struct volatile_list_head {
	volatile struct volatile_list_head *next, *prev;
};

#define VOLATILE_LIST_HEAD(name) \
	volatile struct volatile_list_head name = { &(name), &(name) }

static inline void __volatile_list_del(volatile struct volatile_list_head *prev,
				       volatile struct volatile_list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void volatile_list_del(volatile struct volatile_list_head *elem)
{
	__volatile_list_del(elem->prev, elem->next);
}

static inline int volatile_list_empty(volatile struct volatile_list_head *head)
{
	return head == head->next;
}

static inline void volatile_list_add(volatile struct volatile_list_head *newp,
				     volatile struct volatile_list_head *head)
{
	head->next->prev = newp;
	newp->next = head->next;
	newp->prev = head;
	head->next = newp;
}

#endif

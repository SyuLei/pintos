#include "vm/frame.h"
#include "threads/synch.h"

struct lock frame_lock;
struct list frame_table;
struct list_elem *page_cursor;

void init_frame(void)
{
  lock_init(&frame_lock);
  list_init(&frame_list);
  page_cursor = NULL;
}

void add_page(struct page *p)
{
  lock_acquire(&frame_lock);

  ASSERT(p != NULL);
  ASSERT(p->thread);

  list_push_back(&frame_list, &(p->elem));
  lock_release(&frame_lock);
}

void remove_page(struct page *p)
{
  lock_acquire(&frame_lock);
  list_remove(&(p->elem));
  lock_release(&frame_lock);
}

struct page* get_page_by_kaddr(void *kaddr)
{
  struct list_elem *e;

  for(e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
  {
    struct page *p = list_entry(e, struct page, elem);

    ASSERT(p != NULL);

    if(p->kaddr == kaddr)
      return p;
  }

  return NULL;
}

struct page* get_evict_page(void)
{
  struct page *p;


  if (page_cursor == NULL || page_cursor == list_end (&frame_table))
    page_cursor = list_begin (&frame_table);
  else
  {
    page_cursor = list_next (page_cursor);

    if (page_cursor == list_end (&frame_table))
      page_cursor = list_next (page_cursor);
  }

  p = list_entry (page_cursor, struct page, elem);
}

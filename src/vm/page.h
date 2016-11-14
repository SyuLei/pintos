#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/palloc.h"
#include <hash.h>

struct page
{
  void *kaddr;
  struct page_table_entry *pte;
  struct thread *thread;
  struct list_elem elem;
}

struct page_table_entry
{
  void *vaddr;

  struct hash_elem elem;
}

void page_table_init(struct hash *);
struct page_table_entry* get_pte_by_vaddr(void *vaddr);
void pte_insert (struct hash *page_table, struct page_table_entry *pte)
void pte_delete (struct hash *page_table, struct page_table_entry *pte)


struct page* page_alloc(enum palloc_flags);
#endif

#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/palloc.h"
#include <hash.h>

enum page_table_type
{
  VM_ANON, VM_FILE, VM_BIN
};
/*
struct page
{
  void *kaddr;
  struct page_table_entry *pte;
  struct thread *thread;
  struct list_elem elem;
};
*/
struct page_table_entry
{
  int type;
  struct file *f;
  void *vaddr;
  int32_t ofs;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  bool writable;
  bool loaded;
  struct hash_elem elem;
};

void page_table_init(struct hash *);
void page_table_destroy(struct hash *);
struct page_table_entry* get_pte_by_vaddr(void *);
void pte_insert (struct hash *, struct page_table_entry *);
void pte_delete (struct hash *, struct page_table_entry *);

//struct page* page_alloc(enum palloc_flags);
bool load_page (struct page_table_entry *, void *);
#endif

#include "page.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include <string.h>
#include <debug.h>
#include <stdio.h>


static unsigned pt_hash_func(const struct hash_elem *, void *); 
static bool pt_less_func(const struct hash_elem *, const struct hash_elem *, void *);
static void pt_destroy_func(struct hash_elem *, void *);


void page_table_init(struct hash *page_table)
{
  ASSERT (page_table != NULL);
  hash_init(page_table, pt_hash_func, pt_less_func, NULL);
}

void page_table_destroy(struct hash *page_table)
{
  ASSERT (page_table != NULL);
  hash_destroy(page_table, pt_destroy_func);
}

static unsigned pt_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
  ASSERT(e != NULL);
  return hash_int ((int)(hash_entry (e, struct page_table_entry, elem)->vaddr));
}

static bool pt_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  ASSERT(a != NULL);
  ASSERT(b != NULL);

  return hash_entry(a, struct page_table_entry, elem)->vaddr < hash_entry(b, struct page_table_entry, elem)->vaddr;
}

static void pt_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
  ASSERT(e != NULL);

  struct page_table_entry *pte = hash_entry(e, struct page_table_entry, elem);
  free(pte);
}

struct page_table_entry* get_pte_by_vaddr(void *vaddr)
{
  struct hash *page_table;
  struct page_table_entry pte;
  struct hash_elem *hash_entry;

  page_table = &(thread_current()->page_table);
  pte.vaddr = pg_round_down (vaddr);

  ASSERT (pg_ofs (pte.vaddr) == 0);

  hash_entry = hash_find (page_table, &(pte.elem));

  if (hash_entry == NULL)
    return NULL;

  else
    return hash_entry (hash_entry, struct page_table_entry, elem);
}

void pte_insert (struct hash *page_table, struct page_table_entry *pte)
{
  ASSERT (page_table != NULL);
  ASSERT (pte != NULL);
  ASSERT (pg_ofs (pte->vaddr) == 0);

  hash_insert (page_table, &(pte->elem));
}

void pte_delete (struct hash *page_table, struct page_table_entry *pte)
{
  ASSERT (page_table != NULL);
  ASSERT (pte != NULL);

  hash_delete (page_table, &(pte->elem));
}
/*
struct page* page_alloc(enum palloc_flags flags)
{
  struct page *p;
  p = (struct page *)malloc(sizeof(struct page));

  if(p == NULL)
    return NULL;

  memset(p, 0, sizeof(struct page));

  p->thread = thread_current();
  p->kaddr = palloc_get_page(flags);

  return p;
}
*/
bool load_page (struct page_table_entry *pte, void *kaddr)
{
  ASSERT (pte != NULL);
  ASSERT (kaddr != NULL);

  if ((uint32_t)file_read_at (pte->f, kaddr, pte->read_bytes, pte->ofs) != pte->read_bytes)
    return false;

  memset (kaddr + pte->read_bytes, 0, pte->zero_bytes);

  return true;
}

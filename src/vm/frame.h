#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "vm/page.h"

void frame_table_init(void);
void add_page(struct page *);
void remove_page(struct page *);
struct page* get_page_by_kaddr(void *);
//struct page* get_evict_page(void);

#endif

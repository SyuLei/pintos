#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "vm/page.h"

void frame_init(void);
void add_page(struct page *);
struct page *get_page(void *);
void remove_page(struct page *);

#endif

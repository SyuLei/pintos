#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "vm/page.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);


int add_file(struct file *f);
void remove_file(int fd);
struct file* get_file(int fd);

struct file_struct {
  struct file *f;
  int fd;
  struct list_elem elem;
};

bool load_pte (struct page_table_entry *);

#endif /* userprog/process.h */

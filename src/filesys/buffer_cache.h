#ifndef FILESYS_BUFFER_CACHE_H
#define FILESYS_BUFFER_CACHE_H

#include "devices/disk.h"
#include "threads/synch.h"
#include "filesys/off_t.h"

struct buffer_head
{
  bool dirty;			// checking data change after read
  bool valid;			// valid of buffer
  disk_sector_t address;	// address
  bool clock;			// for clock algoritm in select victim(eviction)
  struct lock lock;		// lock for using this buffer head
  void *buffer;			// buffer pointer
};

void bc_init (void);
void bc_remove (void);
bool bc_read (disk_sector_t, void *, off_t, int, int);
bool bc_write (disk_sector_t, const void *, off_t, int, int);
struct buffer_head *bc_lookup (disk_sector_t);
struct buffer_head *bc_select_victim (void);
void bc_evict_head (struct buffer_head *);

#endif


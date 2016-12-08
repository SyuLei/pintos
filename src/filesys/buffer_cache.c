#include "filesys/buffer_cache.h"
#include "threads/palloc.h"
#include <string.h>
#include <debug.h>

#define BUFFER_CACHE_ENTRIES 64

static void init_head (struct buffer_head *, void *);

static struct buffer_head buffer_head[BUFFER_CACHE_ENTRIES];
static char p_buffer_cache[BUFFER_CACHE_ENTRIES * DISK_SECTOR_SIZE];
static struct buffer_head *clock_hand;
static struct lock bc_lock;

void bc_init (void)
{
  struct buffer_head *head;
  void *cache = p_buffer_cache;

  for (head = buffer_head;
       head != buffer_head + BUFFER_CACHE_ENTRIES;
       head++, cache += DISK_SECTOR_SIZE)
    init_head (head, cache);

  clock_hand = buffer_head;

  lock_init (&bc_lock);
}

static void init_head (struct buffer_head *head, void *buffer)
{
  memset (head, 0, sizeof (struct buffer_head));

  lock_init (&(head->lock));

  head->buffer = buffer;
}

void bc_term (void)
{
  struct buffer_head *head;

  for (head = buffer_head;
       head != buffer_head + BUFFER_CACHE_ENTRIES;
       head++)
  {
    lock_acquire (&(head->lock));
    bc_flush_entry (head);
    lock_release (&(head->lock));
  }
}

bool bc_read (disk_sector_t address, void *buffer, off_t offset, int chunk_size, int sector_ofs)
{
  struct buffer_head *head;

  if (!(head = bc_lookup (address)))
  {
    head = bc_select_victim ();
    bc_flush_entry (head);

    head->valid = true;
    head->dirty = false;
    head->address = address;

    lock_release (&bc_lock);

    disk_read (filesys_disk, address, head->buffer);
  }

  head->clock = true;

  memcpy (buffer + offset, head->buffer + sector_ofs, chunk_size);

  lock_release (&(head->lock));

  return true;
}

bool bc_write (disk_sector_t address, void *buffer, off_t offset, int chunk_size, int sector_ofs)
{
  struct buffer_head *head;

  if (!(head = bc_lookup (address)))
  {
    head = bc_select_victim ();
    bc_flush_entry (head);

    head->valid = true; 
    head->address = address;

    lock_release (&bc_lock);

    disk_read (filesys_disk, address, head->buffer);
  }

  head->clock = true;
  head->dirty = true;

  memcpy (head->buffer + sector_ofs, buffer + offset, chunk_size);

  lock_release (&(head->lock));

  return true;

}

struct buffer_head *bc_lookup (disk_sector_t address)
{
  lock_acquire (&bc_lock);

  struct buffer_head *head;

  for (head = buffer_head;
       head != buffer_head + BUFFER_CACHE_ENTRIES;
       head++)
  {
    if (head->valid && head->address == address)
    {
	    lock_acquire (&(head->lock));
	    lock_release (&bc_lock);

	    return head;
    }
  }

  return NULL;
}

struct buffer_head *bc_select_victim (void)
{
  while (true)
  {
    for (;
         clock_hand != buffer_head + BUFFER_CACHE_ENTRIES;
	 clock_hand++)
    {
      lock_acquire (&(clock_hand->lock));

      if (!(clock_hand->valid && clock_hand->clock))
        return clock_hand++;

      clock_hand->clock = false;

      lock_release (&(clock_hand->lock));
    }

    clock_hand = buffer_head;
  }

  NOT_REACHED ();
}

void bc_flush_entry (struct buffer_head *entry)
{
  ASSERT (lock_held_by_current_thread (&(entry->lock)));

  if (!(entry->valid && entry->dirty))
    return;

  entry->dirty = false;

  disk_write (filesys_disk, entry->address, entry->buffer);
}

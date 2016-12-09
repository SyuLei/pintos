#include "filesys/buffer_cache.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include <string.h>
#include <debug.h>

#define NUM_OF_SECTOR 64

/* static function */
static void head_init (struct buffer_head *, void *);		// init buffer head

/* static variables */
static struct buffer_head head_list[NUM_OF_SECTOR];		// list for buffer head
static char buffer_cache[NUM_OF_SECTOR * DISK_SECTOR_SIZE];	// buffer cache
static struct buffer_head *victim_cursor;			// cursor for evicting
static struct lock bc_lock;					// lock for buffer cache



/* init buffer cache */
void bc_init (void)
{
  struct buffer_head *head_cursor;
  void *cache_cursor = buffer_cache;

  /* init every buffer heads
   * buffer of i'th head will be i'th cache cursor */
  for (head_cursor = head_list;
       head_cursor != head_list + NUM_OF_SECTOR;
       head_cursor++, cache_cursor += DISK_SECTOR_SIZE)
    head_init (head_cursor, cache_cursor);

  /* first head will be first victim */
  victim_cursor = head_list;

  /* init lock */
  lock_init (&bc_lock);
}

/* init buffer head */
static void head_init (struct buffer_head *head, void *buffer)
{
  memset (head, 0, sizeof (struct buffer_head));

  lock_init (&(head->lock));

  head->buffer = buffer;
}

/* destroy buffer cache */
void bc_remove (void)
{
  struct buffer_head *head_cursor;

  /* evict every buffer heads */
  for (head_cursor = head_list;
       head_cursor != head_list + NUM_OF_SECTOR;
       head_cursor++)
  {
    lock_acquire (&(head_cursor->lock));
    bc_evict_head (head_cursor);
    lock_release (&(head_cursor->lock));
  }
}

/* read data */
bool bc_read (disk_sector_t address, void *buffer, off_t offset, int chunk_size, int sector_ofs)
{
  struct buffer_head *head;

  /* check whether there is a buffer head with given address */
  if (!(head = bc_lookup (address)))
  {
    /* if such buffer head does not exist
     * get victim and load data to buffer cache */
    head = bc_select_victim ();
    bc_evict_head (head);

    head->valid = true;
    head->dirty = false;
    head->address = address;

    lock_release (&bc_lock);

    disk_read (filesys_disk, address, head->buffer);
  }

  head->clock = true;

  /* read data from buffer cache */
  memcpy (buffer + offset, head->buffer + sector_ofs, chunk_size);

  lock_release (&(head->lock));

  return true;
}

/* write data */
bool bc_write (disk_sector_t address, const void *buffer, off_t offset, int chunk_size, int sector_ofs)
{
  struct buffer_head *head;

  /* check whether there is a buffer head with given address */
  if (!(head = bc_lookup (address)))
  {
    /* if such buffer head does not exist
     * get victim and load data to buffer cache */
    head = bc_select_victim ();
    bc_evict_head (head);

    head->valid = true; 
    head->address = address;

    lock_release (&bc_lock);

    disk_read (filesys_disk, address, head->buffer);
  }

  head->clock = true;
  head->dirty = true;

  /* write data to buffer cache */
  memcpy (head->buffer + sector_ofs, buffer + offset, chunk_size);

  lock_release (&(head->lock));

  return true;
}

/* find buffer head which has given address */
struct buffer_head *bc_lookup (disk_sector_t address)
{
  lock_acquire (&bc_lock);

  struct buffer_head *head_cursor;

  /* from begin of buffer head list
   * find buffer head with given address */
  for (head_cursor = head_list;
       head_cursor != head_list + NUM_OF_SECTOR;
       head_cursor++)
  {
    /* if a buffer has same address
     * and if the buffer is valid
     * return pointer of the buffer */
    if (head_cursor->valid && head_cursor->address == address)
    {
      lock_acquire (&(head_cursor->lock));
      lock_release (&bc_lock);

      return head_cursor;
    }
  }

  /* if there is no buffer head with given address
   * return NULL */
  return NULL;
}

/* select next buffer head to be evicted */
struct buffer_head *bc_select_victim (void)
{
  while (true)
  {
    for (;
         victim_cursor != head_list + NUM_OF_SECTOR;
	 victim_cursor++)
    {
      lock_acquire (&(victim_cursor->lock));

      /* if clock is false or the buffer head is not valid
       * return pointer of the buffer head.
       * And victim cursor will point next buffer head */
      if (!(victim_cursor->valid && victim_cursor->clock))
        return victim_cursor++;

      victim_cursor->clock = false;

      lock_release (&(victim_cursor->lock));
    }

    victim_cursor = head_list;
  }

  NOT_REACHED ();
}

/* evict buffer head */
void bc_evict_head (struct buffer_head *head)
{
  ASSERT (lock_held_by_current_thread (&(head->lock)));

  /* if the buffer head is never loaded
   * or nothing changed
   * do nothing */
  if (!(head->valid && head->dirty))
    return;

  /* else, write buffer to disk
   * because some data in buffer is changed */
  disk_write (filesys_disk, head->address, head->buffer);
}

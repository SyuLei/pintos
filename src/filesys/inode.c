#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/buffer_cache.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Variables for project4 extends */
#define DIRECT_DISK_ENTRIES 123
#define INDIRECT_DISK_ENTRIES (DISK_SECTOR_SIZE / sizeof (disk_sector_t))

enum inode_enum
{
  DIRECT,
  INDIRECT,
  DOUBLE_INDIRECT,
  OUT_LIMIT
};

struct sector_information
{
  int direct_type;
  int index1;
  int index2;
};

/* almost like inode_disk
 * find 'inode pointer structure in Wikipedia */
struct inode_indirect_disk
{
  disk_sector_t map_table[INDIRECT_DISK_ENTRIES];
};

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long.
   DISK_SECTOR_SIZE = 128 * sizeof (signed int)*/
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t is_dir;			/* true = directory, false = file */

    disk_sector_t direct_map_table[DIRECT_DISK_ENTRIES];/* address of direct blocks */
    disk_sector_t indirect_disk_sec;			/* address of indirect table */
    disk_sector_t double_indirect_disk_sec;		/* address of doubly indirect table */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
//static inline size_t
//bytes_to_sectors (off_t size)
//{
//  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
//}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */

    struct lock extend_lock;
  };

static bool get_disk_inode (const struct inode *, struct inode_disk *);
static void make_sector_info (off_t, struct sector_information *);
static bool register_sector (struct inode_disk *, disk_sector_t, struct sector_information);
static bool inode_update_file_length (struct inode_disk *, off_t, off_t);
static void free_inode_sectors (struct inode_disk *);

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos) 
{
  ASSERT (inode_disk != NULL);

  struct inode_indirect_disk inode_indirect_disk; 
  struct sector_information sector_info;
  disk_sector_t table_sector;

  /* pos(offset) cannot be later than length of file */
  if (pos >= inode_disk->length)
    return -1;

  make_sector_info (pos, &sector_info);

  switch (sector_info.direct_type)
  {
    /* in case of DIRECT, get directly */
    case DIRECT:
      return inode_disk->direct_map_table[sector_info.index1];
    /* IDIRECT case, read table which is located in indirect_disk_sec */
    case INDIRECT:
      if (inode_disk->indirect_disk_sec == (disk_sector_t) -1)
        return -1;

      if (!bc_read (inode_disk->indirect_disk_sec,
	            &inode_indirect_disk,
		    0,
		    sizeof (struct inode_indirect_disk),
		    0))
	return -1;

      return inode_indirect_disk.map_table[sector_info.index1];
    /* read table two times */
    case DOUBLE_INDIRECT:
      if (inode_disk->double_indirect_disk_sec == (disk_sector_t) -1)
        return -1;
      
      if (!bc_read (inode_disk->double_indirect_disk_sec,
	            &inode_indirect_disk,
		    0,
		    sizeof (struct inode_indirect_disk),
		    0))
        return -1;

      table_sector = inode_indirect_disk.map_table[sector_info.index2];

      if (table_sector == (disk_sector_t) -1)
        return -1;

      if (!bc_read (table_sector,
	            &inode_indirect_disk,
		    0,
		    sizeof (struct inode_indirect_disk),
		    0))
	return -1;

      return inode_indirect_disk.map_table[sector_info.index1];
    /* error case */
    default:
      return -1;
  }

  NOT_REACHED ();
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, uint32_t is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);

  if (disk_inode != NULL)
    {
      memset (disk_inode, -1, sizeof (struct inode_disk));

      disk_inode->length = 0;

      if (!inode_update_file_length (disk_inode, disk_inode->length, length))
      {
	free (disk_inode);
	return false;
      }

      disk_inode->magic = INODE_MAGIC;

      disk_inode->is_dir = is_dir;

      bc_write (sector, disk_inode, 0, DISK_SECTOR_SIZE, 0);

      free (disk_inode);

      success = true;
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  lock_init (&inode->extend_lock);

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
	  struct inode_disk inode_disk;

	  bc_read (inode->sector, &inode_disk, 0, DISK_SECTOR_SIZE, 0);

	  free_inode_sectors (&inode_disk);
	  free_map_release (inode->sector, 1);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  struct inode_disk inode_disk;
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  lock_acquire (&inode->extend_lock);
  get_disk_inode (inode, &inode_disk);

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (&inode_disk, offset);

      if (sector_idx == (disk_sector_t) -1)
	break;

      lock_release (&inode->extend_lock);

      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_disk.length - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;

      if (chunk_size <= 0)
      {
        lock_acquire (&inode->extend_lock);

        break;
      }

      bc_read (sector_idx, buffer, bytes_read, chunk_size, sector_ofs);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;

      lock_acquire (&inode->extend_lock);
    }

  lock_release (&inode->extend_lock);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  struct inode_disk inode_disk;
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  lock_acquire (&inode->extend_lock);
  get_disk_inode (inode, &inode_disk);

  if (inode_disk.length < offset + size)
  {
    if (!inode_update_file_length (&inode_disk, inode_disk.length, offset + size))
      NOT_REACHED ();

    bc_write (inode->sector, &inode_disk, 0, DISK_SECTOR_SIZE, 0);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (&inode_disk, offset);
      lock_release (&inode->extend_lock);;
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_disk.length - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
      {
        lock_acquire (&inode->extend_lock);
        break;
      }

      bc_write (sector_idx, buffer, bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
      lock_acquire (&inode->extend_lock);
    }

  lock_release (&inode->extend_lock);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

static bool get_disk_inode (const struct inode *inode, struct inode_disk *inode_disk)
{
  return bc_read (inode->sector, inode_disk, 0, sizeof (struct inode_disk), 0);
}

static void make_sector_info (off_t pos, struct sector_information *sector_info)
{
  off_t pos_sector = pos / DISK_SECTOR_SIZE;
  sector_info->direct_type = OUT_LIMIT;

  if (pos_sector < DIRECT_DISK_ENTRIES)
  {
    sector_info->direct_type = DIRECT;
    sector_info->index1 = pos_sector;
  }
  else if ((pos_sector -= DIRECT_DISK_ENTRIES) < (signed)INDIRECT_DISK_ENTRIES)
  {
    sector_info->direct_type = INDIRECT;
    sector_info->index1 = pos_sector;
  }
  else if ((pos_sector -= INDIRECT_DISK_ENTRIES) < (signed)(INDIRECT_DISK_ENTRIES * INDIRECT_DISK_ENTRIES))
  {
    sector_info->direct_type = DOUBLE_INDIRECT;
    sector_info->index2 = pos_sector / INDIRECT_DISK_ENTRIES;
    sector_info->index1 = pos_sector % INDIRECT_DISK_ENTRIES;
  }
}

static bool register_sector (struct inode_disk *inode_disk, disk_sector_t new_sector, struct sector_information sector_info)
{
  struct inode_indirect_disk first_disk, second_disk;
  bool first_dirty = false;
  disk_sector_t *table_sector = &inode_disk->indirect_disk_sec;

  switch (sector_info.direct_type)
  {
    case DIRECT:
      inode_disk->direct_map_table[sector_info.index1] = new_sector;

      return true;
    case DOUBLE_INDIRECT:
      table_sector = &inode_disk->double_indirect_disk_sec;

      if (*table_sector == (disk_sector_t) -1)
      {
        if (!free_map_allocate (1, table_sector))
          return false;

	memset (&first_disk, -1, sizeof (struct inode_indirect_disk));
      }
      else
      {
        if (!bc_read (*table_sector, &first_disk, 0, sizeof (struct inode_indirect_disk), 0))
          return false;
      }

      table_sector = &first_disk.map_table[sector_info.index2];

      if (*table_sector == (disk_sector_t) -1)
        first_dirty = true;
    case INDIRECT:
      if (*table_sector == (disk_sector_t) -1)
      {
        if (!free_map_allocate (1, table_sector))
          return false;

	memset (&second_disk, -1, sizeof (struct inode_indirect_disk));
      }
      else
      {
	if (!bc_read (*table_sector, &second_disk, 0, sizeof (struct inode_indirect_disk), 0))
          return false;
      }

      if (second_disk.map_table[sector_info.index1] == (disk_sector_t) -1)
        second_disk.map_table[sector_info.index1] = new_sector;
      else
        NOT_REACHED ();

      if (first_dirty)
      {
	if (!bc_write (inode_disk->double_indirect_disk_sec,
		       &first_disk,
		       0,
		       sizeof (struct inode_indirect_disk),
		       0))
	  return false;
      }

      if (!bc_write (*table_sector, &second_disk, 0, sizeof (struct inode_indirect_disk), 0))
	return false;

      return true;
    default:
      return false;
  }

  NOT_REACHED ();
}

static bool inode_update_file_length (struct inode_disk* inode_disk, off_t length, off_t new_length)
{
  static char zeros[DISK_SECTOR_SIZE];

  if (length == new_length)
    return true;

  if (length > new_length)
    return false;

  ASSERT (length < new_length);

  inode_disk->length = new_length;
  new_length--;

  length = length / DISK_SECTOR_SIZE * DISK_SECTOR_SIZE;
  new_length = new_length / DISK_SECTOR_SIZE * DISK_SECTOR_SIZE;

  for (; length <= new_length; length += DISK_SECTOR_SIZE)
  {
    struct sector_information sector_info;

    disk_sector_t sector = byte_to_sector (inode_disk, length);

    if (sector != (disk_sector_t) -1)
      continue;

    if (!free_map_allocate (1, &sector))
      return false;

    make_sector_info (length, &sector_info);

    if (!register_sector (inode_disk, sector, sector_info))
      return false;

    if (!bc_write (sector, zeros, 0, DISK_SECTOR_SIZE, 0))
      return false;
  }

  return true;
}

static void free_sectors (disk_sector_t sector)
{
  int index;
  struct inode_indirect_disk disk;
  bc_read (sector, &disk, 0, sizeof (struct inode_indirect_disk), 0);

  for (index = 0; index < (signed)INDIRECT_DISK_ENTRIES; index++)
  {
    if (disk.map_table[index] == (disk_sector_t) -1)
      return;
    free_map_release (disk.map_table[index], 1);
  }
}

static void free_inode_sectors (struct inode_disk *inode_disk)
{
  int index;

  for (index = 0; index < DIRECT_DISK_ENTRIES; index++)
  {
    if (inode_disk->direct_map_table[index] == (disk_sector_t) -1)
      return;

    free_map_release (inode_disk->direct_map_table[index], 1);
  }

  if (inode_disk->indirect_disk_sec == (disk_sector_t) -1)
    return;

  free_sectors (inode_disk->indirect_disk_sec);
  free_map_release (inode_disk->indirect_disk_sec, 1);

  if (inode_disk->double_indirect_disk_sec == (disk_sector_t) -1)
    return;

  struct inode_indirect_disk disk;

  bc_read (inode_disk->double_indirect_disk_sec, &disk, 0, sizeof (struct inode_indirect_disk), 0);

  for (index = 0; index < DIRECT_DISK_ENTRIES; index++)
  {
    if (disk.map_table[index] == (disk_sector_t) -1)
      return;

    free_sectors (disk.map_table[index]);
    free_map_release (disk.map_table[index], 1);
  }

  free_map_release (inode_disk->double_indirect_disk_sec, 1);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk inode_disk;

  bc_read (inode->sector, &inode_disk, 0, DISK_SECTOR_SIZE, 0);

  return inode_disk.length;
}

bool is_inode_dir (const struct inode *inode)
{
  struct inode_disk inode_disk;

  if (inode->removed)
    return false;

  if (!get_disk_inode (inode, &inode_disk))
    return false;

  return inode_disk.is_dir;
}

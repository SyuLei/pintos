#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/init.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "threads/vaddr.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include <string.h>


static void syscall_handler (struct intr_frame *);

static bool is_valid_ptr(const void *vaddr);
static void check_usable_ptr(const void *vaddr);
static void get_args(struct intr_frame *f, int *args, int num);
static int user_to_kernel_address(const void *);

/* implemented in project2 */
static void sys_halt(void);
static int sys_exec(const char *cmd_line);
static int sys_wait (int pid);
static bool sys_create(const char *file, unsigned initial_size);
static bool sys_remove(const char *file);
static int sys_open(const char *file);
static int sys_filesize(int fd);
static int sys_read(int fd, void *buffer, unsigned size);
static int sys_write(int fd, const void *buffer, unsigned size);
static void sys_seek(int fd, unsigned position);
static unsigned sys_tell(int fd);
static void sys_close(int fd);

/* implemented in project4 */
static bool sys_chdir (const char *);
static bool sys_mkdir (const char *);
static bool sys_readdir (int, char *);
static bool sys_isdir (int);
static int sys_inumber (int);

struct lock filesys_lock;


void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  check_usable_ptr((const void *)f->esp);

  int syscall_number = *(int *)(f->esp);
  int num_of_args[20] = {0, 1, 1, 1, 2, 1, 1, 1, 3, 3, 2, 1, 1, 2, 1, 1, 1, 2, 1, 1};
  int args[3];

  //printf ("(system call) sysnum : %d\n", syscall_number);

  if(syscall_number != SYS_HALT)
    get_args(f, &args[0], num_of_args[syscall_number]);


  switch(syscall_number)
  {
    case SYS_HALT:
	    sys_halt();
	    break;
    case SYS_EXIT:
	    sys_exit(args[0]);
	    break;
    case SYS_EXEC:
	    f->eax = sys_exec((const char *)user_to_kernel_address((const void *)args[0]));
	    break;
    case SYS_WAIT:
	    f->eax = sys_wait(args[0]);
	    break;
    case SYS_CREATE:
	    f->eax = sys_create((const char *)user_to_kernel_address((const void *)args[0]), (unsigned)args[1]);
	    break;
    case SYS_REMOVE:
	    f->eax = sys_remove((const char *)user_to_kernel_address((const void *)args[0]));
	    break;
    case SYS_OPEN:
	    f->eax = sys_open((const char *)user_to_kernel_address((const void *)args[0]));
	    break;
    case SYS_FILESIZE:
	    f->eax = sys_filesize(args[0]);
	    break;
    case SYS_READ:
	    f->eax = sys_read(args[0], (void *)user_to_kernel_address((const void *)args[1]), (unsigned)args[2]);
	    break;
    case SYS_WRITE:
	    f->eax = sys_write(args[0], (const void *)user_to_kernel_address((const void *)args[1]), (unsigned)args[2]);
	    break;
    case SYS_SEEK:
	    sys_seek(args[0], (unsigned)args[1]);
	    break;
    case SYS_TELL:
	    f->eax = sys_tell(args[0]);
	    break;
    case SYS_CLOSE:
	    sys_close(args[0]);
	    break;
    case SYS_MMAP:
    case SYS_MUNMAP:
      printf("This system call is for project3! (%d)\n", *(int *)f->esp);
      break;
    case SYS_CHDIR:
	    f->eax = sys_chdir ((const char *)args[0]);
	    break;
    case SYS_MKDIR:
	    f->eax = sys_mkdir ((const char *)args[0]);
	    break;
    case SYS_READDIR:
	    f->eax = sys_readdir ((int)args[0], (char *)args[1]);
	    break;
    case SYS_ISDIR:
	    f->eax = sys_isdir ((int)args[0]);
	    break;
    case SYS_INUMBER:
	    f->eax = sys_inumber ((int)args[0]);
	    break;
    default:
	    printf("Undefined system call!\n");
	    break;
  }
}

static void sys_halt(void)
{
  power_off();
}

void sys_exit(int status)
{
  struct thread *t = thread_current();

  t->exit_status = status;

  int i;

  for(i = t->next_fd - 1;i > 1;i--)
  {
    struct file *f = get_file(i);

    if(f != NULL)
      file_close(f);
  }

  printf("%s: exit(%d)\n", t->name, status);

  thread_exit();
}

static int sys_exec(const char *cmd_line)
{
  int pid = process_execute(cmd_line);

  if(pid == -1)
    return -1;

  struct thread *child = get_child(pid);

  ASSERT(child != NULL);

  child->p_tid = thread_tid(); 

  sema_down(&(child->load_sema));

  if(!child->load_result)
    return -1;

  return pid;
}

static int sys_wait (int pid)
{
  return process_wait(pid);
}

static bool sys_create(const char *file, unsigned initial_size)
{
  lock_acquire(&filesys_lock);
  bool result = filesys_create(file, initial_size);
  lock_release(&filesys_lock);

  return result;
}

static bool sys_remove(const char *file)
{
  lock_acquire(&filesys_lock);
  bool result = filesys_remove(file);
  lock_release(&filesys_lock);
 
  return result;
}

static int sys_open(const char *file)
{
  lock_acquire(&filesys_lock);

  struct file *f = filesys_open(file);

  if(f == NULL)
  {
    lock_release(&filesys_lock);

    return -1;
  }

  int fd = add_file(f);

  lock_release(&filesys_lock);

  return fd;
}

static int sys_filesize(int fd)
{
  lock_acquire(&filesys_lock);
 
  struct file *f = get_file(fd);

  if(f == NULL)
  {
    lock_release(&filesys_lock);

    return -1;
  }

  int size = file_length(f);

  lock_release(&filesys_lock);	

  return size;
}

static int sys_read(int fd, void *buffer, unsigned size)
{
  if(buffer == NULL)
    sys_exit(-1);

  if(fd == STDIN_FILENO)
  {
    unsigned i;

    uint8_t *local_buf = (uint8_t *)buffer;

    for(i = 0;i < size;i++)
      local_buf[i] = input_getc();

    return size;
  }

  lock_acquire(&filesys_lock);
 
  struct file *f = get_file(fd);

  if(f == NULL)
  {
    lock_release(&filesys_lock);

    return -1;
  }

  int bytes = file_read(f, buffer, size);

  lock_release(&filesys_lock);

  return bytes;
}

static int sys_write(int fd, const void *buffer, unsigned size)
{
  if(buffer == NULL)
    sys_exit(-1);
  
  if(fd == STDOUT_FILENO)
  {
    putbuf(buffer, size);

    return size;
  }

  lock_acquire(&filesys_lock);
 
  struct file *f = get_file(fd);

  if(f == NULL)
  {
    lock_release(&filesys_lock);

    return -1;
  }

  int bytes = file_write(f, buffer, size);

  lock_release(&filesys_lock);

  return bytes;
}

static void sys_seek(int fd, unsigned position)
{
  lock_acquire(&filesys_lock);
 
  struct file *f = get_file(fd);

  if(f == NULL)
  {
    lock_release(&filesys_lock);

    return;
  }

  file_seek(f, position);

  lock_release(&filesys_lock);
}

static unsigned sys_tell(int fd)
{
  lock_acquire(&filesys_lock);
 
  struct file *f = get_file(fd);

  if(f == NULL)
  {
    lock_release(&filesys_lock);

    return -1;
  }

  off_t pos = file_tell(f);

  lock_release(&filesys_lock);

  return pos;
}

static void sys_close(int fd)
{
  lock_acquire(&filesys_lock);

  remove_file(fd);

  lock_release(&filesys_lock);
}

static bool sys_chdir (const char *path_o)
{
  char path[PATH_MAX_LEN + 1];
  strlcpy (path, path_o, PATH_MAX_LEN);
  strlcat (path, "/0", PATH_MAX_LEN);

  char name[PATH_MAX_LEN + 1];
  struct dir *dir = parse_path (path, name);

  if (!dir)
    return false;

  dir_close (thread_current ()->dir);
  thread_current ()->dir = dir;

  return true;
}

static bool sys_mkdir (const char *dir)
{
  return filesys_create_dir (dir);
}

static bool sys_readdir (int fd, char *name)
{
  struct file *f = get_file (fd);

  if (f == NULL)
    sys_exit (-1);

  struct inode *inode = file_get_inode (f);

  if (inode == NULL || !is_inode_dir (inode))
    return false;

  struct dir *dir = dir_open (inode);

  if (dir == NULL)
    return false;

  int i;
  bool result = true;
  off_t *pos = (off_t *)f + 1;

  for (i = 0; (i <= *pos) && result; i++)
    result = dir_readdir (dir, name);

  if (i > *pos)
    (*pos)++; 

  return result;
}

static bool sys_isdir (int fd)
{
  struct file *f = get_file (fd);

  if (f == NULL)
    sys_exit (-1);

  return is_inode_dir (file_get_inode (f));
}

static int sys_inumber (int fd)
{
  struct file *f = get_file (fd);

  if (f == NULL)
    sys_exit (-1);

  return inode_get_inumber (file_get_inode (f));
}

static bool is_valid_ptr(const void *vaddr)
{
  return is_user_vaddr(vaddr + 3) && vaddr >= (void *)0x08048000;
}

static void check_usable_ptr(const void *vaddr)
{
  if(!is_valid_ptr(vaddr))
    sys_exit(-1);

  int i;

  for(i = 0;i < 4;i++) 
  {
    void *ptr = pagedir_get_page(thread_current()->pagedir, (const void *)((int)vaddr + i));

    if(ptr == NULL)
      sys_exit(-1);
  }
}



void get_args(struct intr_frame *f, int *args, int num)
{
  int i;
  void *vaddr;

  for(i = 0;i < num;i++)
  {
    vaddr = (f->esp + (sizeof(void *) * (i + 1)));

    check_usable_ptr((const void *)vaddr);
    
    //printf("esp[%d] : %08x\n", i, (unsigned)(f->esp + (sizeof(void *) * (i + 1))));
    args[i] = *(int *)(f->esp + (sizeof(void *) * (i + 1)));
    // printf("args[%d] : %08x\n", i, args[i]);
  }
}

static int user_to_kernel_address(const void *vaddr)
{
  check_usable_ptr(vaddr);

  //printf("vaddr : %08x\n", (unsigned)vaddr);

  void *ptr = pagedir_get_page(thread_current()->pagedir, vaddr);

  //printf("ptr : %08x\n", (unsigned)ptr);

  if(ptr == NULL)
    sys_exit(-1);

  return (int)ptr;
}

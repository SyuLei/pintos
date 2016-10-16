#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/init.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "threads/vaddr.h"


static void syscall_handler (struct intr_frame *);

static bool is_valid_ptr(const void *vaddr);
static void get_args(struct intr_frame *f, int *args, int num);
static int user_to_kernel_address(const void *);
static int add_file(struct file *f);
static void remove_file(int fd);
static struct file* get_file(int fd);

/* implemented in project2 */
static void sys_halt(void);
static void sys_exit(int status);
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

struct lock filesys_lock;

struct file_struct {
  struct file *f;
  int fd;
  struct list_elem elem;
};


void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_number = *(int *)(f->esp);
  int num_of_args[13] = {0, 1, 1, 1, 2, 1, 1, 1, 3, 3, 2, 1, 1};
  int args[3];

  if(syscall_number != SYS_HALT)
    get_args(f, &args[0], num_of_args[syscall_number]);


  //printf ("system call(%d)!\n", syscall_number);

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
	    f->eax = sys_write(args[0], (const void *)args[1], (unsigned)args[2]);
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
    default:
	    printf("Undefined system call!\n");
	    break;
  }
}

static void sys_halt(void)
{
  power_off();
}

static void sys_exit(int status)
{
  struct thread *t = thread_current();

  struct thread *p_thread = get_thread(t->p_tid);

  if(p_thread != NULL && p_thread->status != THREAD_DYING)
    p_thread->status = status;

  printf("%s: exit(%d)\n", t->name, status);

  thread_exit();
}

static int sys_exec(const char *cmd_line)
{
	//I have to do more
  int pid = process_execute(cmd_line);

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

static bool is_valid_ptr(const void *vaddr)
{
  return is_user_vaddr(vaddr) && vaddr >= (void *)0x08048000;
}



static void get_args(struct intr_frame *f, int *args, int num)
{
  int i;

  for(i = 0;i < num;i++)
  {
    //printf("esp[%d] : %08x\n", i, (unsigned)(f->esp + (sizeof(void *) * (i + 1))));
    args[i] = *(int *)(f->esp + (sizeof(void *) * (i + 1)));
   // printf("args[%d] : %08x\n", i, args[i]);
  }
}

static int user_to_kernel_address(const void *vaddr)
{
  if(!is_valid_ptr(vaddr))
    sys_exit(-1);

  //printf("vaddr : %08x\n", (unsigned)vaddr);

  void *ptr = pagedir_get_page(thread_current()->pagedir, vaddr);

  //printf("ptr : %08x\n", (unsigned)ptr);

  if(ptr == NULL)
    sys_exit(-1);

  return (int)ptr;
}

static int add_file(struct file *f)
{
  struct thread *t = thread_current();
  struct file_struct *fs = malloc(sizeof(struct file_struct));

  fs->f = f;
  fs->fd = t->next_fd;
  t->next_fd++;
  list_push_back(&(t->file_list), &(fs->elem));

  return fs->fd;
}

static struct file* get_file(int fd)
{
  struct thread *t = thread_current();
  struct list_elem *e;

  for(e = list_begin(&(t->file_list));e != list_end(&(t->file_list));e = list_next(e))
  {
    struct file_struct *fs = list_entry(e, struct file_struct, elem);

    if(fs->fd == fd)
      return fs->f;
  }

  return NULL;
}

static void remove_file(int fd)
{
  struct thread *t = thread_current();
  struct list_elem *e;

  for(e = list_begin(&(t->file_list));e != list_end(&(t->file_list));e = list_next(e))
  {
    struct file_struct *fs = list_entry(e, struct file_struct, elem);

    if(fs->fd == fd)
      list_remove(e);
  }
}

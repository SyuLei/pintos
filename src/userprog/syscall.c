#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");

  switch()
  {
    case SYS_HALT:
	    break;
    case SYS_EXIT:
	    break;
    case SYS_EXEC:
	    break;
    case SYS_WAIT:
	    break;
    case SYS_CREATE:
	    break;
    case SYS_REMOVE:
	    break;
    case SYS_OPEN:
	    break;
    case SYS_READ:
	    break;
    case SYS_WRITE:
	    break;
    case SYS_SEEK:
	    break;
    case SYS_TELL:
	    break;
    case SYS_CLOSE:
	    break;
    default:
	    break;
  }
}

void halt(void)
{
}

void exit(int status)
{
}

pid_t exec(const char *cmd_line)
{
}

int wait (pid_t pid)
{
  return 0;
}

bool create(const char *file, unsigned initial_size)
{
  return true;
}

bool remove(const char *file)
{
  return true;
}

int open(const char *file)
{
  return 0;
}

int filesize(int fd)
{
  return 0;
}

int read(int fd, void *buffer, unsigned size)
{
  return 0;
}

int write(int fd, const void *buffer, unsigned size)
{
  return 0;
}

void seek(int fd, unsigned position)
{
  return 0;
}

unsigned tell(int fd)
{
  return 0;
}

void close(int fd)
{
}


#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool
is_valid_user_pointer(const void * charlie)
{
	return is_user_vaddr(charlie) && pagedir_get_page(thread_current()->pagedir, charlie) != NULL;
}

static void
userprog_fail(struct intr_frame *f)
{
	f->eax = -1;
	exit(-1);
}

static void
syscall_handler (struct intr_frame *f) 
{
	if(f->esp == NULL || !is_valid_user_pointer((const void *)f->esp))
	{
		userprog_fail(f);
	}

	switch(* (int *)(f->esp))
	{
		case SYS_HALT:
		{
			halt();
			break;
		}
		case SYS_EXIT:
		{
			if(!is_valid_user_pointer(f->esp + 4))
				userprog_fail(f);
			int status = *((int *)f->esp + 1);
			f->eax = status;
			exit(status);
			break;
		}
		case SYS_EXEC:
		{
			char * file = *((char **)f->esp + 1);
			pid_t pid = process_execute(file);
			f->eax = pid;
			break;
		}
		case SYS_WAIT:
		{
			pid_t pid = *(int *)(f->esp + 1);

			int status = process_wait(pid);
			f->eax = status;
			
			break;
		}
		case SYS_CREATE:
		{
			char * file = *((char **)f->esp + 1);
			if(file == NULL || !is_valid_user_pointer((const void *) file))
				userprog_fail(f);
			unsigned initial_size = *((unsigned *)f->esp + 2);
			bool ret = filesys_create(file, initial_size);
			f->eax = ret;

			break;
		}
		case SYS_REMOVE:
		{
			char * file = *((char **)f->esp + 1);

			bool ret = filesys_remove(file);
			f->eax = ret;

			break;
		}
		case SYS_OPEN:
		{
			char * file = *((char **)f->esp + 1);
			struct thread_file * tf = (struct thread_file *)malloc(sizeof (struct thread_file));
			tf->fdfile = filesys_open(file);
			tf->pos = 0;
			struct thread * t = thread_current();
			lock_acquire(t->last_fd_lock);
			t->last_fd++;
			tf->fd = t->last_fd;
			lock_release(t->last_fd_lock);

			// TODO: Do we have to sync here?
			list_push_back(&t->files, &tf->elem);

			f->eax = tf->fd;

			break;
		}
		case SYS_FILESIZE:
		{
			break;
		}
		case SYS_READ:
		{
			break;
		}
		case SYS_WRITE:
		{
			int fd = *((int *)f->esp + 1);
			void * buf = *((void **)f->esp + 2);
			unsigned size = *((unsigned *)f->esp + 3);
			// printf("%d: %x, %x, %d\n", SYS_WRITE, fd, &buf, size);
			if(fd == STDOUT_FILENO)
			{
				putbuf(buf, size);
			}

			break;
		}
		case SYS_SEEK:
		{
			break;
		}
		case SYS_TELL:
		{
			break;
		}
		case SYS_CLOSE:
		{
			break;
		}
	}
}

static void
halt (void)
{
	shutdown_power_off();
}

static void
exit (int status)
{
	printf("%s: exit(%d)\n", thread_current()->name, status);
	thread_exit();
}

/*
pid_t exec (const char *file);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
 */

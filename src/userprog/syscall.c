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
static void sys_halt (void);
static void sys_exit (int status);
struct thread_file * get_thread_file (int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool
is_valid_user_pointer (const void * charlie)
{
	return is_user_vaddr (charlie) && pagedir_get_page (thread_current()->pagedir, charlie) != NULL;
}

static void
userprog_fail (struct intr_frame *f)
{
	f->eax = -1;
	sys_exit(-1);
}

static void
syscall_handler (struct intr_frame *f) 
{
	if (f->esp == NULL || !is_valid_user_pointer ((const void *) f->esp))
	{
		userprog_fail (f);
	}

	switch (* (int *)(f->esp))
	{
		case SYS_HALT:
		{
			sys_halt ();
			break;
		}
		case SYS_EXIT:
		{
			if (!is_valid_user_pointer (f->esp + 4))
				userprog_fail (f);

			int status = *((int *) f->esp + 1);
			f->eax = status;
			sys_exit (status);

			break;
		}
		case SYS_EXEC:
		{
			char * file = *((char **) f->esp + 1);
			pid_t pid = process_execute (file);
			f->eax = pid;
			break;
		}
		case SYS_WAIT:
		{
			pid_t pid = *((int *) f->esp + 1);

			int status = process_wait (pid);
			f->eax = status;
			
			break;
		}
		case SYS_CREATE:
		{
			char * file = *((char **) f->esp + 1);
			if (file == NULL || !is_valid_user_pointer ((const void *) file))
				userprog_fail (f);

			unsigned initial_size = *((unsigned *) f->esp + 2);
			bool ret = filesys_create (file, initial_size);
			f->eax = ret;

			break;
		}
		case SYS_REMOVE:
		{
			char * file = *((char **)f->esp + 1);

			if (file == NULL || !is_valid_user_pointer (file))
				userprog_fail (f);

			bool ret = filesys_remove(file);
			f->eax = ret;

			break;
		}
		case SYS_OPEN:
		{
			char * file_name = *((char **) f->esp + 1);

			if (file_name == NULL || !is_valid_user_pointer (file_name))
				userprog_fail (f);


			struct thread_file * tf = (struct thread_file *)malloc(sizeof (struct thread_file));
			tf->fdfile = filesys_open (file_name);

			if(tf->fdfile == NULL)
				userprog_fail(f);

			tf->pos = 0;
			struct thread * current= thread_current ();
			lock_acquire(current->last_fd_lock);
			current->last_fd++;
			tf->fd = current->last_fd;
			lock_release (current->last_fd_lock);

			// TODO: Do we have to sync here?
			list_push_back(&current->thread_files, &tf->elem);

			f->eax = tf->fd;

			break;
		}
		case SYS_FILESIZE:
		{
			int fd = *((int *) f->esp + 1);
			struct thread_file * current_tf = get_thread_file (fd);

			if (current_tf != NULL)
				f->eax = file_length (current_tf->fdfile);

			break;
		}
		case SYS_READ:
		{
			int fd = *((int *)f->esp + 1);
			void * buf = *((void **)f->esp + 2);

			if(!is_valid_user_pointer(buf))
				userprog_fail(f);

			unsigned size = *((unsigned *)f->esp + 3);
			struct thread_file * current_tf = get_thread_file (fd);

			if (current_tf != NULL)
			{
				f->eax = file_read (current_tf->fdfile, buf, size);
			}
			else if(fd == STDIN_FILENO)
			{
				input_getc();
				f->eax = size;
			}

			break;
		}
		case SYS_WRITE:
		{
			int fd = *((int *)f->esp + 1);
			void * buf = *((void **)f->esp + 2);

			if(!is_valid_user_pointer(buf))
				userprog_fail(f);

			unsigned size = *((unsigned *)f->esp + 3);
			struct thread_file * current_tf = get_thread_file (fd);

			if (current_tf != NULL)
			{
				f->eax = file_write (current_tf->fdfile, buf, size);
			}
			else if(fd == STDOUT_FILENO)
			{
				putbuf(buf, size);
				f->eax = size;
			}

			break;
		}
		case SYS_SEEK:
		{
			int fd = *((int *)f->esp + 1);
			unsigned position = *((unsigned *) f->esp + 2);
			struct thread_file * current_tf = get_thread_file (fd);

			if (current_tf != NULL)
				file_seek (current_tf->fdfile, position);

			break;
		}
		case SYS_TELL:
		{
			int fd = *((int *)f->esp + 1);
			struct thread_file * current_tf = get_thread_file (fd);

			if (current_tf != NULL)
				f->eax = file_tell (current_tf->fdfile);

			break;
		}
		case SYS_CLOSE:
		{
			int fd = *((int *)f->esp + 1);
			struct thread_file * current_tf = get_thread_file (fd);

			if (current_tf != NULL)
			{
				list_remove(&current_tf->elem);
				file_close(current_tf->fdfile);
				free(current_tf);
			}

			break;
		}
	}
}

static void
sys_halt (void)
{
	shutdown_power_off ();
}

static void
sys_exit (int status)
{
	printf ("%s: exit(%d)\n", thread_current()->name, status);
	thread_exit ();
}

struct
thread_file * get_thread_file (int fd)
{
	struct thread *current = thread_current ();
	struct list_elem * e;
	struct thread_file * current_tf;

	//TODO: fix kernel panic
	/*for (e = list_begin (&current->thread_files); e != list_end (&current->thread_files);
		e = list_next (e))
	{
		current_tf = list_entry (e, struct thread_file, elem);

		if (current_tf->fd == fd)
			return current_tf;
	}*/

	return NULL;
}
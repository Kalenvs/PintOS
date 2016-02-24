#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"


struct lock file_lock;

struct process_file {
  struct file *file;
  int fd;
  struct list_elem elem;
};

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

static void syscall_handler (struct intr_frame *f UNUSED) 
{
  int * callvalue =(int*) f->esp;  //syscall value
  int args[3];
  int numOfArgs;				   //diff per each syscall

  //##Using the number find out which system call is being used
  // numOfArgs = number of args that system call uses {0,1,2,3}
	
  //copy_in (args, (uint32_t *) f->esp + 1, sizeof (*args) * numOfArgs);
  
  check_pointer(callvalue);
  switch(*callvalue)
  {
	  case SYS_HALT:
	  {
		shutdown_power_off();
		break;
	  }
	  case SYS_EXIT:
	  {
		copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1);  
		exit(args[0]);
		break;
	  }
	  case SYS_WRITE:
	  { 
		 copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 3);
		 f->eax = write(args[0], (const void *) args[1], (unsigned) args[2]);
		 break;
	  }
  }
};

/*checks if the current thread has a live parent, and changes own status
if so. After, prints details then exits */
void exit (int status)		
{		
  struct thread *cur = thread_current();		
  if (thread_alive(cur->parent))		
    {		
      cur->cp->status = status;		
    }		
  printf ("%s: exit(%d)\n", cur->name, status);		
  thread_exit();		
}

int write (int fd, const void *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO)
    {
      putbuf(buffer, size);
      return size;
    }
  lock_acquire(&file_lock);
  struct file *f = process_get_file(fd);
  if (!f)
    {
      lock_release(&file_lock);
      return -1;
    }
  int bytes = file_write(f, buffer, size);
  lock_release(&file_lock);
  return bytes;
}


/* checks if pointer is not a user vaddr by vaddr.h and is greater than
 * or equal to the Bottom of user data in syscall.h*/
void check_pointer (const void *pointer)		
{		
  if (!is_user_vaddr(pointer) || pointer < USER_DATA_BOTTOM)		
    {		
      exit(-1);		
    }		
}

/* Copies SIZE bytes from user address USRC to kernel address
   DST.
   Call thread_exit() if any of the user accesses are invalid. */
void
copy_in (void *dst_, const void *usrc_, size_t size) 
{
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;
 
  for (; size > 0; size--, dst++, usrc++) 
    if (usrc >= (uint8_t *) PHYS_BASE || !get_user (dst, usrc)) 
      thread_exit ();
};

/* Copies a byte from user address USRC to kernel address DST.
   USRC must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
inline bool
get_user (uint8_t *dst, const uint8_t *usrc)
{
  int eax;
  asm ("movl $1f, %%eax; movb %2, %%al; movb %%al, %0; 1:"
       : "=m" (*dst), "=&a" (eax) : "m" (*usrc));
  return eax != 0;
};

struct file* process_get_file (int fd)
{
  struct thread *t = thread_current();
  struct list_elem *e;

  for (e = list_begin (&t->files); e != list_end (&t->files);
       e = list_next (e))
        {
          struct process_file *pf = list_entry (e, struct process_file, elem);
          if (fd == pf->fd)
	    {
	      return pf->file;
	    }
        }
  return NULL;
}


/*get's child process struct by looping through the current thread's 
 and returns the cp in which the child proccesses match. 
 returns Null if no match found */
struct child_process* get_child (int pid)
{
  struct thread *t = thread_current();
  struct list_elem *e;

  for (e = list_begin (&t->children); e != list_end (&t->children); e = list_next (e))
  {
       struct child_process *cp = list_entry (e, struct child_process, elem);
       if (pid == cp->pid)
	      return cp;
   }
  return NULL;
}

//removes child elem from list, then free's cp space.
void remove_child (struct child_process *cp)
{
  list_remove(&cp->elem);
  free(cp);
}

//creates child process stuct and adds it to current threads child list
// For load, 0 = not loaded, - 1 = fail to load, 1 = loaded 
struct child_process* child_proc (int pid)
{
  struct child_process* cp = malloc(sizeof(struct child_process));
  cp->pid = pid;
  cp->load = 0;
  cp->wait = false;
  cp->exit = false;
  lock_init(&cp->wait_lock);
  list_push_back(&thread_current()->children, &cp->elem);
  return cp;
}

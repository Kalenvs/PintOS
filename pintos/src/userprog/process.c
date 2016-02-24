#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "threads/malloc.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline,void (**eip) (void), void ** esp, char ** tokr_pntr);
//static void * push (uint8_t *kpage, size_t *offset, const void *buf, size_t size);
//static bool setup_stack_helper (const char * cmd_line, uint8_t * kpage, uint8_t * upage, void ** esp);

tid_t
process_execute (const char *file_name) 
{
  char *cmd_line;
  char * throw_away;
  tid_t tid;

  
  //##Set exec file name here
  cmd_line = palloc_get_page(0);
  if(cmd_line == NULL)
	return TID_ERROR;
	
  strlcpy(cmd_line, file_name, PGSIZE);
  //##loading semaphore already initialized in thread_create

  
  //change file_name to reflect actual file name
  //##Program name is the first token of file_name
  file_name = strtok_r(file_name, " ", &throw_away);
	
  /*Create a new thread to execute FILE_NAME.
  file_name becomes thread name, cmd_line is passed as pointer arg to
  start_process function.*/
  tid = thread_create (file_name, PRI_DEFAULT, start_process, cmd_line); 
  //if the thread_create failed, free up the page at file_name
  if (tid == TID_ERROR)
	palloc_free_page(cmd_line);
	
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char * tokr_pointer;
  char *file_name;
  struct intr_frame if_;
  bool success;
  struct thread* t;
  
  file_name = strtok_r(file_name_, " ", &tokr_pointer);
  
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp, &tokr_pointer);
  
  t = thread_current();
  if(success)
	t->cp->load = 1;
  else
    t->cp->load = -1;
    
  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
	/*get process with tid from thread_list
	if not existant, then error
	if the child is being waited on, then error
	 if it's waiting, busy loop
	else continue and remove child from child list. */
	int status;
	struct child_process* cp = get_child(child_tid);
	if(!cp)
		return -1;
	else if(cp->wait)
		return -1;
	cp->wait = true;
	while(cp->exit == false)
	{	
		barrier();
	}
	
	status = cp->status;
	remove_child(cp);
    return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  
  /*sets exit status of cp struct in thread so anything waiting on it 
  will be free */
  if (thread_alive(cur->parent))
    {
      cur->cp->exit = true;
    }
  
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp,char * file_name, char ** tokr_pntr);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void ** esp, char ** tokr_pntr) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();
  
  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp,file_name, tokr_pntr))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  //file close moved to process exit
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp,char * file_name, char ** tokr_pntr) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
      {
        palloc_free_page (kpage);
        return success;
	  }
    }
    //success = setup_stack_helper(cmd_ptr, kpage, ((uint8_t *) PHYS_BASE) - PGSIZE, esp);
  char *token;		
  char **argv = malloc(2*sizeof(char *));		
  int i, argc = 0, argv_size = 2;		
  
  // Push args onto stack		
  for (token = file_name; token != NULL;		
       token = strtok_r (NULL, " ", tokr_pntr))		
    {		
      *esp -= strlen(token) + 1;		
      argv[argc] = *esp;		
      argc++;		
      // Resize argv		
      if (argc >= argv_size)		
	{		
	  argv_size *= 2;		
	  argv = realloc(argv, argv_size*sizeof(char *));		
	}		
      memcpy(*esp, token, strlen(token) + 1);		
    }		
  argv[argc] = 0;		
  // Align to word size (4 bytes)		
  i = (size_t) *esp % 4;		
  if (i)		
    {		
      *esp -= i;		
      memcpy(*esp, &argv[argc], i);		
    }		
  // Push argv[i] for all i		
  for (i = argc; i >= 0; i--)		
    {		
      *esp -= sizeof(char *);		
      memcpy(*esp, &argv[i], sizeof(char *));		
    }
  // Push argv		
  token = *esp;		
  *esp -= sizeof(char **);		
  memcpy(*esp, &token, sizeof(char **));		
  // Push argc		
  *esp -= sizeof(int);		
  memcpy(*esp, &argc, sizeof(int));		
  // Push fake return addr		
  *esp -= sizeof(void *);		
  memcpy(*esp, &argv[argc], sizeof(void *));		
  // Free argv		
  free(argv);
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

//------------------------------------------------------------------------
/*
//## push (kpage, &ofs, &x, sizeof x), kpage is created in setup_stack....
//## x is all the values argv, argc, and null (you need a null on the stack!)
//## Be careful of hte order of argv! Check the stack example

   Pushes the SIZE bytes in BUF onto the stack in KPAGE, whose
   page-relative stack pointer is *OFS, and then adjusts *OFS
   appropriately.  The bytes pushed are rounded to a 32-bit
   boundary.

   If successful, returns a pointer to the newly pushed object.
   On failure, returns a null pointer. 
static void *
push (uint8_t *kpage, size_t *offset, const void *buf, size_t size) 
{
  size_t padsize = ROUND_UP (size, sizeof (uint32_t));
  
  if (*offset < padsize){
    return NULL;
  }

  *offset -= padsize;
  
  memcpy (kpage + *offset + (padsize - size), buf, size);
  
  return kpage + *offset + (padsize - size);
}

static bool setup_stack_helper (char ** cmd_line, uint8_t * kpage, uint8_t * upage, void ** esp) 
{
  size_t ofs = PGSIZE; //##Used in push!
  char * const null = NULL; //##Used for pushing nulls
  char *ptr = (char *) cmd_line; //##strtok_r usage
  //##Probably need some other variables here as well...
  char ** argv = malloc(2 * sizeof(char*));

  //##Parse and put in command line arguments, push each value
  while(ptr != NULL)
  {
	  ptr = strtok_r(NULL, " ", cmd_line);
	  if(!push(kpage, &ofs, ptr, sizeof(ptr))
		return false;
      
  }
  //##if any push() returns NULL, return false

  //##push() a null (more precisely &null).
  //##if push returned NULL, return false


  //##Push argv addresses (i.e. for the cmd_line added above) in reverse order
  //##See the stack example on documentation for what "reversed" means
  //##Push argc, how can we determine argc?
  //##Push &null
  //##Should you check for NULL returns?

  //##Set the stack pointer. IMPORTANT! Make sure you use the right value here...
  *esp = upage + ofs;

  //##If you made it this far, everything seems good, return true
}


*/




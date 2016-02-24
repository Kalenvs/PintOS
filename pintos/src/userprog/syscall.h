#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

#define USER_DATA_BOTTOM ((void *) 0x08048000)

//struct used to keep track of what the child process values are, and 
//what state it is in.
struct child_process {
  int pid;
  int load;
  bool wait;
  bool exit;
  int status;
  struct lock wait_lock;
  struct list_elem elem;
};

void syscall_init (void);

void exit (int status);
void halt (void);
int write (int fd, const void *buffer, unsigned size);

void check_pointer (const void *pointer);

void copy_in (void *dst_, const void *usrc_, size_t size);

inline bool get_user (uint8_t *dst, const uint8_t *usrc);

struct file* process_get_file (int fd);

struct child_process* get_child (int pid);
struct child_process* child_proc (int pid);
void remove_child (struct child_process *cp);

#endif /* userprog/syscall.h */

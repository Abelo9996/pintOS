#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "process.h"

void syscall_init(void);
struct lock global_lock;

typedef char lock_t;
typedef char sema_t;

struct pthread_exec_info {
  struct process* pcb;
  stub_fun sf;
  pthread_fun tf;
  void* arg;
  struct semaphore finished;
  bool success;
};

struct file_struct {
  /* File descriptor */
  int fd;
  /* File struct */
  struct file* struct_file;
  /* list_elem for position */
  struct list_elem elem;
};

struct lock_struct {
  lock_t lock;
  int acquired;
  int initialized;
  struct list_elem elem_one;
  tid_t tid; 
};

struct sema_struct {
  sema_t sema;
  int val;
  int initialized;
  struct list_elem elem_two;
};

#endif /* userprog/syscall.h */
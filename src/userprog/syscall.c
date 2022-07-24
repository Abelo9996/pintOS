#include <stdlib.h>
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "lib/float.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <string.h>
#include "filesys/file.h"
#include "devices/input.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "threads/palloc.h"
#include "userprog/process.h"

static void syscall_handler(struct intr_frame*);

void fail_sys(void);

struct file_struct* find_file(int fd);

void null_check(char* ptr);

void valid_ptr_check(void* ptr);

void validate_file(char* ptr);

void validate_buffer(void* ptr, size_t size);

tid_t sys_pthread_create(stub_fun sfun, pthread_fun tfun, const void* arg);

tid_t sys_pthread_join(tid_t tid);

static bool install_page(void* upage, void* kpage, bool writable);

static void start_pthread(void* pthread_info);

struct lock_struct* find_lock(lock_t lock);

struct lock global_lock;

void sys_pthread_exit(void);

struct file_struct elem;

static bool setup_stack(void** esp, pthread_fun tfun, const void* arg);

static bool install_page(void* upage, void* kpage, bool writable) {
  struct thread* t = thread_current();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page(t->pcb->pagedir, upage) == NULL &&
          pagedir_set_page(t->pcb->pagedir, upage, kpage, writable));
}

struct lock_struct* find_lock(lock_t lock){
  struct list_elem* e;
  for (e = list_begin(&thread_current()->pcb->user_locks);e != list_end(&thread_current()->pcb->user_locks);
       e = list_next(e)) {
    if (list_entry(e, struct lock_struct, elem_one)->lock == lock){
      return list_entry(e, struct lock_struct, elem_one);
    }
  }
  return NULL;
}

struct join_status* find_join(tid_t tid){
  struct list_elem* e;
  for (e = list_begin(&thread_current()->pcb->join_statuses);e != list_end(&thread_current()->pcb->join_statuses);
       e = list_next(e)) {
    if (list_entry(e, struct join_status, element)->tid == tid){
      return list_entry(e, struct join_status, element);
    }
  }
  return NULL;
}

struct sema_struct* find_sema(sema_t sema){
  struct list_elem* e;
  for (e = list_begin(&thread_current()->pcb->user_semas);e != list_end(&thread_current()->pcb->user_semas);
       e = list_next(e)) {
    if (list_entry(e, struct sema_struct, elem_two)->sema == sema){
      return list_entry(e, struct sema_struct, elem_two);
    }
  }
  return NULL;
}

static bool setup_stack(void** esp, pthread_fun tfun, const void* arg) {
    /*Saving arguments to the stack*/
    //individual argv addresses on stack
    *esp = *esp - sizeof(void *);
    // (*(void**)*esp) = &arg;
    memcpy(*esp, &arg, sizeof(void *));
    *esp = *esp - sizeof(void *);
    // (*(void**)*esp) = &tfun;
    memcpy(*esp, &tfun, sizeof(void *));

    //fake return address
    *esp = *esp - sizeof(int);
    (*(void**)*esp) = 0;
  return true;
}

static void start_pthread(void* pthread_info) {
  struct pthread_exec_info* pt_info = (struct pthread_exec_info*) pthread_info;
  struct thread* t = thread_current();
  t->pcb = pt_info->pcb;
  struct intr_frame if_;
  process_activate();
  uint8_t* kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  uint8_t* upage = PHYS_BASE - PGSIZE;
  while (!(pagedir_get_page(t->pcb->pagedir, upage))){
	  upage-=PGSIZE;
  }

  install_page(upage, kpage, true);

  memset(&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  if_.eip = pt_info->sf;
  if_.esp = upage+PGSIZE;
  t->page_pointer = kpage;
  setup_stack(&if_.esp, pt_info->tf,pt_info->arg);

  // FPU stack
  uint8_t temp_fpu[108];
  asm volatile("fsave (%0); fninit; fsave (%1); frstor (%2)"
                :
                : "g"(&temp_fpu), "g"(&(if_.fpu_stack)), "g"(&temp_fpu));

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  // sema_up(&pt_info->finished);
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}

tid_t sys_pthread_create(stub_fun sfun, pthread_fun tfun, const void* arg){
  tid_t tid;
  struct pthread_exec_info *pthread_info = malloc(sizeof(struct pthread_exec_info));
  pthread_info->sf = sfun;
  pthread_info->tf = tfun;
  pthread_info->arg = arg;
  pthread_info->pcb = thread_current()->pcb;
  pthread_info->success = 0;
  // sema_init(&pthread_info->finished, 0);

  char name[16];
  snprintf(name, sizeof  name, "thread %d", thread_current()->pcb->tid_cnt);
  tid = thread_create(name, PRI_DEFAULT, start_pthread, pthread_info);
  
  struct join_status* join_status = malloc(sizeof(struct join_status));
  join_status->join = 0;
  join_status->tid = tid;
  sema_init(&join_status->wait_semaphore, 0);
  list_push_back(&thread_current()->pcb->join_statuses, &join_status->element);


  if (tid != TID_ERROR){
    thread_current()->pcb->tid_cnt++;
  }
  // sema_down(&pthread_info->finished);
  return tid;
}

struct file_struct* find_file(int fd) {
  struct list_elem* e;
  for (e = list_begin(&thread_current()->files_open); e != list_end(&thread_current()->files_open);
       e = list_next(e)) {
    if (list_entry(e, struct file_struct, elem)->fd == fd) {
      return list_entry(e, struct file_struct, elem);
    }
  }
  return NULL;
}

void syscall_init(void) {
  lock_init(&global_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void fail_sys(void) {
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
  process_exit();
}

void null_check(char* ptr) {
  if (ptr == NULL) {
    fail_sys();
  }
}

void valid_ptr_check(void* ptr) {
  if (!is_user_vaddr(ptr)) {
    fail_sys();
  } else if (pagedir_get_page(thread_current()->pcb->pagedir, ptr) == NULL) {
    fail_sys();
  }
}

void validate_file(char* ptr) {
  while (1) {
    valid_ptr_check(ptr);
    if (ptr[0] == '\0') {
      break;
    }
    ptr++;
  }
}

void validate_buffer(void* ptr, size_t size) {
  for (int i = 0; i < size; i++) {
    valid_ptr_check(ptr);
    ptr = (char*)ptr + 1;
  }
}

tid_t sys_pthread_join(tid_t tid) {
  struct join_status* join_status= find_join((tid_t) tid);
  if (join_status == NULL){
    return TID_ERROR;
  }else if (join_status->join == 1){
    return TID_ERROR;
  }

  if (tid == thread_current()->pcb->main_thread->tid){
    thread_current()->pcb->join_main = 1;
  }

  sema_down(&join_status->wait_semaphore);
  join_status->join = 1;
  return tid;
}

void sys_pthread_exit(void) {
  struct thread* thread = thread_current();
  tid_t tid = thread->tid;
  if (tid != thread->pcb->main_thread->tid){
	  palloc_free_page(thread->page_pointer);
	  struct join_status* join_status= find_join((tid_t) thread->tid);
	  sema_up(&join_status->wait_semaphore);
	  thread_exit();
  }else{
	  struct join_status* join_status = find_join((tid_t) tid);
	  sema_up(&join_status->wait_semaphore);
  	struct list_elem* e;
  	for (e = list_begin(&thread->pcb->join_statuses);e != list_end(&thread->pcb->join_statuses);e = list_next(e)) {
    		struct join_status* join_status_im = list_entry(e, struct join_status, element);
    		if (join_status_im->tid != tid && join_status_im->join == 0){
      			sys_pthread_join(join_status_im->tid);
    		}
    }
    process_exit();
  }
}

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* Validate syscall number argument address */
  valid_ptr_check(&args[0]);
  valid_ptr_check(&args[0] + sizeof(uint32_t));

  if (args[0] == SYS_EXIT) {
    thread_current()->wait_connect->exit_code = args[1];
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
    process_exit();
  }

  if (args[0] == SYS_PT_CREATE) {
    tid_t tid = sys_pthread_create((stub_fun) args[1],(pthread_fun) args[2],(const void*)args[3]);
    if (tid >= 0){
      f->eax = tid;
    }else{
      f->eax = TID_ERROR;
    }
  }

  if (args[0] == SYS_PT_JOIN) {
    tid_t ret_val = sys_pthread_join((tid_t) args[1]);
    f->eax = ret_val;
  }


  if (args[0] == SYS_PT_EXIT) {
    sys_pthread_exit();
  }

  if (args[0] == SYS_LOCK_INIT){
    if (args[1] == NULL){
      f->eax = 0;
    }else{
      struct lock_struct* lock_struct= find_lock(args[1]);
      if (lock_struct == NULL){
        struct lock_struct* lock_struct = malloc(sizeof(struct lock_struct));
        lock_struct->lock = args[1];
        lock_struct->acquired = 0;
        lock_struct->initialized = 1;
        lock_struct->tid = NULL;
        list_push_back(&thread_current()->pcb->user_locks, &lock_struct->elem_one);
      }
      f->eax = 1;
    }
  }

  if (args[0] == SYS_SEMA_INIT){
    if ((int)args[2] < 0 || args[1] == NULL){
      f->eax = 0;
    }else{
      struct sema_struct* sema_struct= find_sema(args[1]);
      if (sema_struct == NULL){
        struct sema_struct* sema_struct = malloc(sizeof(struct sema_struct));
        sema_struct->sema = args[1];
        sema_struct->val = args[2];
        sema_struct->initialized = 1;
        list_push_back(&thread_current()->pcb->user_semas, &sema_struct->elem_two);
      }
      f->eax = 1;
    }
  }
  
  if (args[0] == SYS_LOCK_ACQUIRE){
    struct lock_struct* lock_struct= find_lock(args[1]);
    if (lock_struct == NULL){
      f->eax = 0;
    }else if (lock_struct->initialized != 1 || thread_current()->tid == lock_struct->tid){
      f->eax = 0;
    }else{
      lock_struct->acquired = 1;
      lock_struct->tid = thread_current()->tid;
      f->eax = 1;
    }
  }

  if (args[0] == SYS_LOCK_RELEASE){
    struct lock_struct* lock_struct= find_lock(args[1]);
    if (lock_struct == NULL){
      f->eax = 0;
    }else if (lock_struct->initialized != 1 || thread_current()->tid != lock_struct->tid){
      f->eax = 0;
    }else{
      lock_struct->acquired = 0;
      lock_struct->tid = NULL;
      f->eax = 1;
    }
  }

  if (args[0] == SYS_SEMA_UP){
    struct sema_struct* sema_struct= find_sema(args[1]);
    if (sema_struct == NULL){
      f->eax = 0;
    }else if (sema_struct->initialized != 1){
      f->eax = 0;
    }else{
      sema_struct->val++;
      f->eax = 1;
    }
  }

  if (args[0] == SYS_SEMA_DOWN){
    struct sema_struct* sema_struct= find_sema(args[1]);
    if (sema_struct == NULL){
      f->eax = 0;
    }else if (sema_struct->initialized != 1){
      f->eax = 0;
    }else{
      sema_struct->val--;
      f->eax = 1;
    }
  }

  if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
  }
  if (args[0] == SYS_COMPUTE_E) {
    if (args[1] > 1) {
      f->eax = sys_sum_to_e(args[1]);
    }
  }
  if (args[0] == SYS_HALT) {
    shutdown_power_off();
  }

  if (args[0] == SYS_EXEC) {
    null_check((char*)args[1]);
    validate_file((char*)args[1]);
    f->eax = process_execute((char*)args[1]);
  }

  if (args[0] == SYS_WAIT) {
    f->eax = process_wait((pid_t)args[1]);
  }

  if (args[0] == SYS_CREATE) {
    null_check((char*)args[1]);
    validate_file(args[1]);
    lock_acquire(&global_lock);
    f->eax = filesys_create((const char*)args[1], (size_t)args[2]);
    lock_release(&global_lock);
  }

  if (args[0] == SYS_REMOVE) {
    lock_acquire(&global_lock);
    f->eax = filesys_remove((const char*)args[1]);
    lock_release(&global_lock);
  }

  if (args[0] == SYS_OPEN) {
    null_check((char*)args[1]);
    validate_file(args[1]);
    struct file* new_file = filesys_open((const char*)args[1]);
    if (new_file == NULL) {
      f->eax = -1;
    } else {
      int fd = thread_current()->count;
      thread_current()->count++;
      struct file_struct* file_input = malloc(sizeof(struct file_struct));
      file_input->struct_file = new_file;
      file_input->fd = fd;
      list_push_back(&thread_current()->files_open, &file_input->elem);
      f->eax = fd;
    }
  }

  if (args[0] == SYS_FILESIZE) {
    if (find_file(args[1]) == NULL) {
      f->eax = -1;
    } else {
      f->eax = file_length(find_file(args[1])->struct_file);
    }
  }

  if (args[0] == SYS_READ) {
    validate_buffer(args[2], (size_t)args[3]);
    if (args[1] == 0) {
      uint8_t* buffer = (uint8_t*)args[2];
      unsigned i;
      for (i = 0; i < args[3]; (unsigned)i++) {
        buffer[i] = input_getc();
      }
      f->eax = i;
    } else {
      if (find_file(args[1]) == NULL) {
        f->eax = -1;
      } else {
        f->eax = file_read(find_file(args[1])->struct_file, (void*)args[2], (unsigned)args[3]);
      }
    }
  }

  if (args[0] == SYS_WRITE) {
    validate_buffer(args[2], (size_t)args[3]);
    if (args[1] == 1) {
      lock_acquire(&global_lock);
      putbuf((const char*)args[2], (size_t)args[3]);
      f->eax = (int)args[3];
      lock_release(&global_lock);
    } else {
      if (find_file(args[1]) == NULL) {
        f->eax = -1;
      } else {
        lock_acquire(&global_lock);
        f->eax = file_write(find_file(args[1])->struct_file, (void*)args[2], (unsigned)args[3]);
        lock_release(&global_lock);
      }
    }
  }

  if (args[0] == SYS_SEEK) {
    if (find_file(args[1]) == NULL) {
      f->eax = -1;
    } else {
      file_seek(find_file(args[1])->struct_file, (unsigned)args[2]);
    }
  }

  if (args[0] == SYS_TELL) {
    if (find_file(args[1]) == NULL) {
      f->eax = -1;
    } else {
      f->eax = file_tell(find_file(args[1])->struct_file);
    }
  }

  if (args[0] == SYS_CLOSE) {
    if (find_file(args[1]) == NULL) {
      fail_sys();
    } else {
      struct file_struct* file_to_close = find_file(args[1]);
      file_close(file_to_close->struct_file);
      list_remove(&file_to_close->elem);
      free(file_to_close);
    }
  }
}
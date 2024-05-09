#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "vm/spage.h"
#include "vm/frame.h"
#include "vm/mmap.h"
#include <round.h>
#include "userprog/pagedir.h"
#include "threads/malloc.h"

static void syscall_handler (struct intr_frame *);

#define FILE_NAME_MAX 512
#define CMD_LINE_MAX 512
#define FD_MAX 128

#define MAX(a,b) ((a)>(b)?(a):(b))

/** syscall functions*/
static void sys_halt(void);
// void sys_exit(int code_);
static int sys_write(int fd,const void* buffer,unsigned size);
static tid_t sys_exec(const char* cmd_line);
static int sys_wait(tid_t pid);
static bool sys_create(const char* file,unsigned initial_size);
static bool sys_remove(const char* file);
static int sys_open (const char *file_name);
static int sys_filesize(int fd);
static int sys_read(int fd,void* buffer,unsigned size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close(int fd);

#ifdef VM
static mapid_t sys_mmap (int fd, void *addr);
static void sys_munmap (mapid_t mapping);
static void preload_pages(const void* buffer, unsigned size);
static void unpin_pages(const void* buffer, unsigned size);
#endif

/** utils functions*/
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static void read_user(void* kaddr_,const void* uaddr_,int n);

static inline void
check_user_ptr(const void* uaddr){
  if(uaddr>=PHYS_BASE||uaddr==NULL){
    sys_exit(-1);
  }
  int result=get_user(uaddr);
  if(result==-1){
    sys_exit(-1);
  }
}

static inline void
check_user_string(const char* str,int maxlen){
  for(int i=0;i<maxlen;++i){
    int result=get_user((const uint8_t*)str+i);
    if(result==-1){
      sys_exit(-1);
    }
    char c=result&0xff;
    if(c=='\0'){
      return;
    }
  }
  sys_exit(-1);
}

static inline void
check_user_address(const void* uaddr,int len){
  if(len<0){
    sys_exit(-1);
  }
  void* start_page_addr=pg_round_down(uaddr);
  void* end_page_addr=pg_round_down(uaddr+len-1);
  for(void* addr=start_page_addr;addr<=end_page_addr;addr+=PGSIZE){
    check_user_ptr(addr);
  }
}

static inline void
check_user_address_debug(const void* uaddr,int len){
  if(len<0){
    sys_exit(-1);
  }
  void* start_page_addr=pg_round_down(uaddr);
  void* end_page_addr=pg_round_down(uaddr+len-1);
  for(void* addr=start_page_addr;addr<=end_page_addr;addr+=PGSIZE){
    //printf("start check addr:%p\n",addr);
    check_user_ptr(addr);
    //printf("check addr success:%p\n",addr);
  }
}


void
syscall_init (void){
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f){

  void* u_ptr=f->esp;
  thread_current()->user_esp=f->esp;
  
  int syscall_num;
  read_user(&syscall_num,u_ptr,sizeof(int));
  u_ptr+=sizeof(int);
  
  switch (syscall_num){

    case SYS_HALT:
    {
      sys_halt();

      NOT_REACHED();
    }
    case SYS_EXIT:
    {
      int exit_code;
      read_user(&exit_code,u_ptr,sizeof(int));
      u_ptr+=sizeof(int);
      sys_exit(exit_code);
      
      NOT_REACHED();
    }
    case SYS_EXEC:
    {
      char* cmd_line;
      read_user(&cmd_line,u_ptr,sizeof(char*));
      u_ptr+=sizeof(char*);
      f->eax=sys_exec(cmd_line);
      break;
    }
    case SYS_WAIT:
    {
      tid_t wait_pid;
      read_user(&wait_pid,u_ptr,sizeof(tid_t));
      u_ptr+=sizeof(tid_t);
      f->eax=sys_wait(wait_pid);
      break;
    }
    case SYS_CREATE:
    {
        const char* file;
        unsigned initial_size;
        read_user(&file,u_ptr,sizeof(char*));
        u_ptr+=sizeof(char*);
        read_user(&initial_size,u_ptr,sizeof(unsigned));
        u_ptr+=sizeof(unsigned);
        f->eax=sys_create(file,initial_size);
        break;
    }
    case SYS_REMOVE:
    {
        const char* file;
        read_user(&file,u_ptr,sizeof(char*));
        u_ptr+=sizeof(char*);
        f->eax=sys_remove(file);
        break;
    }
    case SYS_OPEN:
    {
        const char* file;
        read_user(&file,u_ptr,sizeof(char*));
        u_ptr+=sizeof(char*);
        f->eax=sys_open(file);
        break;
    }
    case SYS_FILESIZE:
    {
      int fd;
      read_user(&fd,u_ptr,sizeof(int));
      u_ptr+=sizeof(int);
      
      f->eax=sys_filesize(fd);
      break;
    }
    case SYS_READ:
    {
      int fd;
      void* buffer;
      unsigned size;
      read_user(&fd,u_ptr,sizeof(int));
      u_ptr+=sizeof(int);
      read_user(&buffer,u_ptr,sizeof(void*));
      u_ptr+=sizeof(void*);
      read_user(&size,u_ptr,sizeof(unsigned));
      u_ptr+=sizeof(unsigned);

      f->eax=sys_read(fd,buffer,size);
      break;
    }
    case SYS_WRITE:
    {
      int fd;
      const void* buffer;
      unsigned size;
      read_user(&fd,u_ptr,sizeof(int));
      u_ptr+=sizeof(int);
      read_user(&buffer,u_ptr,sizeof(void*));
      u_ptr+=sizeof(void*);
      read_user(&size,u_ptr,sizeof(unsigned));
      u_ptr+=sizeof(unsigned);

      f->eax=sys_write(fd,buffer,size);
      break;
    }
    case SYS_SEEK:
    {
      int fd;
      unsigned position;
      read_user(&fd,u_ptr,sizeof(int));
      u_ptr+=sizeof(int);
      read_user(&position,u_ptr,sizeof(unsigned));
      u_ptr+=sizeof(unsigned);
      
      sys_seek(fd,position);
      break;
    }
    case SYS_TELL:
    {
      int fd;
      read_user(&fd,u_ptr,sizeof(int));
      u_ptr+=sizeof(int);
      
      f->eax=sys_tell(fd);
      break;
    }
    case SYS_CLOSE:
    {
      int fd;
      read_user(&fd,u_ptr,sizeof(int));
      u_ptr+=sizeof(int);
      
      sys_close(fd);
      break;
    }
    
    #ifdef VM
    case SYS_MMAP:
    {
      int fd;
      void* addr;
      read_user(&fd,u_ptr,sizeof(int));
      u_ptr+=sizeof(int);
      read_user(&addr,u_ptr,sizeof(void*));
      u_ptr+=sizeof(void*);
      f->eax=sys_mmap(fd,addr);
      break;
    }
    case SYS_MUNMAP:
    {
      mapid_t mapping;
      read_user(&mapping,u_ptr,sizeof(mapid_t));
      u_ptr+=sizeof(mapid_t);
      sys_munmap(mapping);
      break;
    }
    #endif
    
    default:
    {
      printf("system call %d is not implemented!\n",syscall_num);
      sys_exit(-1);
    }
  }
}

/******************* implementation of syscall functions****************/

static void
sys_halt(){
  shutdown_power_off();
}

void
sys_exit(int code_){
  printf ("%s: exit(%d)\n", thread_current()->name,code_);
  struct process_control_block* pcb=thread_current()->pcb;
  if(pcb!=NULL){
    pcb->exit_status=code_;
  }

  /** destroy the spage_table along with frame allocated previous*/
#ifdef VM
  mmap_destroy();
  spage_destroy();
#endif

  //when the thread_exit is done, all the lock acquired by the thread will be released
  //Thus no need for releasing the lock here
  if(!lock_held_by_current_thread(&filesys_lock)){
    lock_acquire(&filesys_lock);
  }
  thread_exit();
  NOT_REACHED();
}
 
static tid_t
sys_exec(const char* cmd_line){
  check_user_string(cmd_line,CMD_LINE_MAX);

  lock_acquire(&filesys_lock);
  tid_t pid = process_execute(cmd_line);
  lock_release(&filesys_lock);
  return pid;
}

static int
sys_wait(tid_t pid){
  return process_wait(pid);
}

static bool
sys_create(const char* file,unsigned initial_size){
  check_user_string(file,FILE_NAME_MAX);
  
  lock_acquire(&filesys_lock);
  bool success = filesys_create(file,initial_size);
  lock_release(&filesys_lock);
  return success;
}

static bool
sys_remove(const char* file){
  check_user_string(file,FILE_NAME_MAX);
  // check_user_address(file,strlen(file)+1);
  
  lock_acquire(&filesys_lock);
  bool success = filesys_remove(file);
  lock_release(&filesys_lock);
  return success;
}

static int
sys_open (const char *file_name){
  check_user_string(file_name,FILE_NAME_MAX);
  // check_user_address(file_name,strlen(file_name)+1);
  struct thread* cur=thread_current();
  
  lock_acquire(&filesys_lock);
  
  struct file* file=filesys_open(file_name);
  if(file==NULL){
    lock_release(&filesys_lock);
    return -1;
  }
  int next_fd=cur->next_fd;
  for(int fd=2;fd<=FD_MAX;++fd){
    if(cur->descriptor_table[fd]==NULL){
      cur->descriptor_table[fd]=file;
      cur->next_fd=MAX(next_fd,fd+1);
      lock_release(&filesys_lock);
      return fd;
    }
  }
  lock_release(&filesys_lock);
  return -1;
}

static int
sys_filesize(int fd){
  if(fd<0||fd>=FD_MAX){
    sys_exit(-1);
  }

  struct file* file=thread_current()->descriptor_table[fd];
  if(file==NULL){
    sys_exit(-1);
  }
  lock_acquire(&filesys_lock);
  int ret = file_length(file);
  lock_release(&filesys_lock);
  return ret;
}

static int
sys_read(int fd,void* buffer,unsigned size_){
  if(fd<0||fd>=FD_MAX){
    sys_exit(-1);
  }
  check_user_address(buffer,size_);
  //check_user_address_debug(buffer,size_);

  int size;
  lock_acquire(&filesys_lock);
  if(fd==0){
    for(unsigned i=0;i<size_;++i){
      int result=put_user(buffer+i,input_getc());
      if(!result){
        lock_release(&filesys_lock);
        sys_exit(-1);
      }
    }
    lock_release(&filesys_lock);
    return size_;
  }
  else{
    struct file* file=thread_current()->descriptor_table[fd];
    if(file==NULL){
      lock_release(&filesys_lock);
      return -1;
    }
#ifdef VM
    preload_pages(buffer, size_);
#endif
    size=file_read(file,buffer,size_);
#ifdef VM
    unpin_pages(buffer, size_);
#endif

    lock_release(&filesys_lock);
    return size;
  }
}

static int
sys_write(int fd,const void* buffer,unsigned size_){
  if(fd<0||fd>=FD_MAX){
    sys_exit(-1);
  }
  // check_user_ptr(buffer);
  check_user_address_debug(buffer,size_);
  
  lock_acquire(&filesys_lock);

  int size;
  /** write to stdout*/
  if(fd==1){
    putbuf(buffer,size_);
    lock_release(&filesys_lock);
    return size_;
  }
  else{
    struct file* file=thread_current()->descriptor_table[fd];
    if(file==NULL){
      lock_release(&filesys_lock);
      return -1;
    }
#ifdef VM
    preload_pages(buffer, size_);
#endif
    size = file_write(file,buffer,size_);
#ifdef VM
    unpin_pages(buffer, size_);
#endif

    lock_release(&filesys_lock);
    return size;
  }
}

static void
sys_seek(int fd,unsigned pos){
  if(fd<0||fd>=FD_MAX){
    sys_exit(-1);
  }
  
  struct file* file=thread_current()->descriptor_table[fd];
  if(file==NULL){
    return;//???????????
  }
  lock_acquire(&filesys_lock);
  file_seek(file,pos);
  lock_release(&filesys_lock);
}

static unsigned
sys_tell(int fd){
  if(fd<0||fd>=FD_MAX){
    sys_exit(-1);
  }
  
  struct file* file=thread_current()->descriptor_table[fd];
  if(file==NULL){
    return 0;//????????????
  }
  lock_acquire(&filesys_lock);
  unsigned ret = file_tell(file);
  lock_release(&filesys_lock);
  return ret;
}

static void
sys_close(int fd){
  if(fd<0||fd>=FD_MAX){
    sys_exit(-1);
  }
  
  struct file* file=thread_current()->descriptor_table[fd];
  if(file==NULL){
    return;
  }
  lock_acquire(&filesys_lock);
  file_close(file);
  lock_release(&filesys_lock);
  thread_current()->descriptor_table[fd]=NULL;
}

#ifdef VM
static mapid_t 
sys_mmap (int fd, void *uaddr){
  
  /* check validity*/
  if(((unsigned)uaddr&0xfff)!=0|| uaddr == 0){
    return -1;
  }
  struct thread* cur = thread_current();
  /* the sys_filesize already acquire lock. Thus there's no need for sys_mmap to acquire*/
  size_t read_bytes = sys_filesize(fd);
  size_t zero_bytes = ROUND_UP(read_bytes, PGSIZE)-read_bytes;
  if(read_bytes == 0 ){
    return -1;
  }
  struct file* file=file_reopen(cur->descriptor_table[fd]);
  if(file == NULL){
    return -1;
  }
  off_t ofs = 0;
  /* check overlapping*/
  void* upper_page = pg_round_up(uaddr+read_bytes);
  for(void* addr = uaddr; addr < upper_page; addr += PGSIZE){
    struct spage_table_entry* se = spage_find(cur,addr);
    if(se != NULL){
      return -1;
    }
  }

  /* insert to mmap_table*/
  struct mmap_table_entry* me = (struct mmap_table_entry* )malloc(sizeof(struct mmap_table_entry));
  me->addr = uaddr;
  me->fd = assign_mapid();
  me->size = read_bytes;
  hash_insert(cur->mmap_table,&me->mmap_elem);

  /* start to install in spage_table*/
  while(read_bytes > 0 || zero_bytes > 0){
    size_t page_read_bytes = read_bytes<PGSIZE? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;
    bool success = spage_install_mmap(uaddr, file, ofs, page_read_bytes, page_zero_bytes, true);
    if(!success){
      ASSERT(0);
    }
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    uaddr += PGSIZE;
    ofs += PGSIZE;
  }
  
  return me->fd;
}

static void
sys_munmap (mapid_t mapping){

  struct thread* cur = thread_current();
  struct mmap_table_entry* me = mmap_find(cur,mapping);
  if(me == NULL){
    ASSERT(0);
    return;
  }

  void* upper_page = pg_round_up(me->addr + me->size);
  for(void* addr = me->addr;addr<upper_page;addr+=PGSIZE){
    struct spage_table_entry* se = spage_find(cur,addr);
    ASSERT(se&&se->type == MMAP);
    bool is_dirty = pagedir_is_dirty(cur->pagedir,addr);
    if(se->location == IN_FRAME){
      if(is_dirty){
        mmap_write_back(se,false);
      }
      frame_free(se->kpage,false);
    }
    spage_remove(se);
  }
  mmap_remove(me);
}

static void
preload_pages(const void* buffer, unsigned size){
  void* start_page_addr = pg_round_down(buffer);
  void* end_page_addr = pg_round_down(buffer + size - 1);
  for(void* addr = start_page_addr; addr <= end_page_addr; addr += PGSIZE){
    struct spage_table_entry* se = spage_find(thread_current(),addr);
    if(se->location != IN_FRAME){
      spage_load(se);
    }
    frame_pin(se->kpage);
  }
}

static void
unpin_pages(const void* buffer, unsigned size){
  void* start_page_addr = pg_round_down(buffer);
  void* end_page_addr = pg_round_down(buffer + size - 1);
  for(void* addr = start_page_addr; addr <= end_page_addr; addr += PGSIZE){
    struct spage_table_entry* se = spage_find(thread_current(),addr);
    frame_unpin(se->kpage);
  }
}
#endif

/******************** utils functions*************************/

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

/** read n bytes from uaddr_ to kaddr_
 * If invalid address, go to proc_terminate
*/
static void
read_user(void* kaddr_,const void* uaddr_,int n){
  ASSERT(n>0);
  unsigned char *kaddr = kaddr_;
  const uint8_t *uaddr=uaddr_;

  for(int i=0;i<n;++i){
    if((void* )uaddr>=PHYS_BASE||uaddr==NULL){
      sys_exit(-1);
    }
    int result=get_user(uaddr);
    if(result==-1){
      sys_exit(-1);
    }

    *kaddr=result&0xff;
    kaddr++;
    uaddr++;
  }
}

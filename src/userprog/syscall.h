#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void sys_exit(int code_);

/** lock for filesys*/
struct lock filesys_lock;

#endif /**< userprog/syscall.h */

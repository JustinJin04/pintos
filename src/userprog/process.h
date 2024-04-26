#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/** do not release until the process was retrieved by the parent process*/
struct process_control_block{
    /** initialized in thread_create*/
    tid_t pid;

    /** initialized in process_exec*/
    char cmd_line[128];
    bool has_exited;
    bool is_orphan;
    struct semaphore sema_wait; /* used for sys_wait*/

    int exit_status;    /* initialized to 0. modified when the kernel terminate the thread*/
    //bool terminated_by_kernel;

    struct list_elem elem;
};

#endif /**< userprog/process.h */

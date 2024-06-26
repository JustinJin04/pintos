#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "../devices/timer.h"
#include "threads/fixed-point.h"
#include "malloc.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

/** Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/** List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/** List of processes in sleep*/
static struct list sleep_list;

/** List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/** Idle thread. */
static struct thread *idle_thread;

/** Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/** Lock used by allocate_tid(). */
static struct lock tid_lock;

/** load average number used to calculate recent_cpu*/
static fixed_t load_avg;

/** Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /**< Return address. */
    thread_func *function;      /**< Function to call. */
    void *aux;                  /**< Auxiliary data for function. */
  };

/** Statistics. */
static long long idle_ticks;    /**< # of timer ticks spent idle. */
static long long kernel_ticks;  /**< # of timer ticks in kernel threads. */
static long long user_ticks;    /**< # of timer ticks in user programs. */

/** Scheduling. */
#define TIME_SLICE 4            /**< # of timer ticks to give each thread. */
static unsigned thread_ticks;   /**< # of timer ticks since last yield. */

/** If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
static bool compare_wake_ticks(const struct list_elem* a,const struct list_elem* b,void* aux UNUSED);
void list_debug(struct list* list);

/** Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  if(thread_mlfqs){
    load_avg=CONVERT_TO_FIXED(0);
  }

#ifdef USERPROG
  list_init(&initial_thread->children);
  initial_thread->parent=NULL;
  initial_thread->exec_file=NULL;
  initial_thread->pcb=NULL;
#endif
}

/** Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/** Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/** Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/** Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */ 
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

#ifdef USERPROG
  list_init(&t->children);
  t->parent=thread_current();
  t->descriptor_table=calloc(sizeof(struct file*),128);
  if(t->descriptor_table==NULL){
    palloc_free_page(t);
    return TID_ERROR;
  }
  t->next_fd=2;
  t->exec_file=NULL;
  // t->pcb=malloc(sizeof(struct process_control_block));
  // if(t->pcb==NULL){
  //   palloc_free_page(t);
  //   return TID_ERROR;
  // }
  //t->pcb->pid=t->tid;
  // t->pcb->has_exited=false;
  // t->pcb->is_orphan=false;
  // t->pcb->exit_status=0;
  // sema_init(&t->pcb->sema_wait,0);
  //list_push_back(&thread_current()->children,&t->pcb->elem);
#endif

  /* Add to run queue. */
  thread_unblock (t);

  /** check if preemption is needed*/
  thread_try_preempt();

  return tid;
}

/** Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/** Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered(&ready_list,&t->elem,compare_priority_more,NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);

  /**this function does not preempt the running thread, 
   * perhaps the caller still need to do other things
   * e.g. thread_wake()
  */
}

/** called after thread_unblock, ready_list insert, read_list elem modified, thread_create*/
void 
thread_try_preempt(void){
  enum intr_level old_level=intr_disable();
  if(list_empty(&ready_list)){
    intr_set_level(old_level);
    return;
  }
  struct thread* t=list_entry(list_front(&ready_list),struct thread,elem);
  
  /** running in external interrupt context and not need to disable again*/
  if(intr_context()){
    if(t->priority>thread_current()->priority){
      intr_yield_on_return();
    }
  }
  /** need to disable interrupt becauce of access to read_list*/
  else{
    if(t->priority>thread_current()->priority){
      thread_yield();
    }
  }
  intr_set_level(old_level);
}

/** Invoked by timer_sleep() in timer.c. Block the current thread until wake_ticks*/
void 
thread_sleep(int64_t wake_ticks){
  struct thread* t=thread_current();
  if(t==idle_thread){
    return;
  }
  t->wake_ticks=wake_ticks;
  enum intr_level old_level=intr_disable();
  list_insert_ordered(&sleep_list,&t->elem,compare_wake_ticks,NULL);
  thread_block();
  intr_set_level(old_level);
}

/** Invoked by timer_interrupt(). Wake the sleep threads in sleep_list
 *  Warning: an struct list_elem type object in struct thread can be only appeared in ONE 
 * struct list,i.e., the state of a thread can be one of THREAD_BLOCKED(in sleep_list), THREAD_READY(in read_list),
 * THREAD_RUNNING.
 * For this, struct list_elem elem can appear either in ready_list or sleep_list.
*/
void 
thread_wake(){
  struct list_elem *e;
  for (e = list_begin(&sleep_list); e != list_end(&sleep_list);) {
    struct thread *t = list_entry(e, struct thread, elem);
    if (t->wake_ticks <= timer_ticks()) {
      e=list_remove(e);
      thread_unblock(t);
    } 
    else break;
  }
}

/** Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/** Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/** Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/** Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /** release holding locks*/
  struct thread* cur=thread_current();
  while(!list_empty(&cur->holding_locks)){
    struct lock* l=list_entry(list_pop_front(&cur->holding_locks),struct lock,elem);
    lock_release(l);
  }

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/** Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread){
    list_insert_ordered(&ready_list,&cur->elem,compare_priority_more,NULL);
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/** Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/** Called by lock_acquire. Implement donation along the lock waited by thread t*/
void thread_donate(struct thread *t){
  ASSERT(t->waiting_lock!=NULL&&t->waiting_lock->holder!=NULL);
  enum intr_level old_level=intr_disable();
  
  struct lock* wait_lock=t->waiting_lock;
  struct thread* pa=wait_lock->holder;

  if(wait_lock->lock_priority>=t->priority){
    intr_set_level(old_level);
    return;
  }
  wait_lock->lock_priority=t->priority;
  
  if(pa->priority>=t->priority){
    intr_set_level(old_level);
    return;
  }
  pa->priority=t->priority;
  
  while(pa->waiting_lock!=NULL){
    t=pa;
    wait_lock=t->waiting_lock;
    pa=pa->waiting_lock->holder;
    
    if(wait_lock->lock_priority>=t->priority)break;
    wait_lock->lock_priority=t->priority;
    
    if(pa->priority>=t->priority)break;
    pa->priority=t->priority;
  }
  list_sort(&ready_list,compare_priority_more,NULL);
  intr_set_level(old_level);
}

/** Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if(thread_mlfqs)return;

  struct thread* t=thread_current();
  enum intr_level old_level=intr_disable();
  
  /** check the lock_holding list*/
  if(!list_empty(&t->holding_locks)){
    struct lock* l=list_entry(list_front(&t->holding_locks),struct lock,elem);
    if(l->lock_priority>new_priority){
      t->orig_priority=new_priority;
      intr_set_level(old_level);
      return;
    }
  }
  t->priority=t->orig_priority=new_priority;
  intr_set_level(old_level);
  
  /** check possible preemption*/
  thread_try_preempt();
}

/** change other thread's priority
 * remember to resort the list if thread is in ready list
*/
void
thread_change_priority(struct thread* t,int new_priority){
  t->priority=new_priority;
  if(t->status==THREAD_READY){
    list_remove(&t->elem);
    list_insert_ordered(&ready_list,&t->elem,compare_priority_more,NULL);
  }
}


/** Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/** Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  thread_current()->nice=nice;
}

/** Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current()->nice;
}

/** Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return CONVERT_TO_INT_NEAREST(MUL_FIXED_INT(load_avg,100));
}

/** Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return CONVERT_TO_INT_NEAREST(MUL_FIXED_INT(thread_current()->recent_cpu,100));
}

void 
thread_update_priority(struct thread *t,void* aux UNUSED){
  thread_change_priority(t,PRI_MAX-CONVERT_TO_INT_ZERO(DIV_FIXED_INT(t->recent_cpu,4))-t->nice*2);
  //t->priority = PRI_MAX-CONVERT_TO_INT_ZERO(DIV_FIXED_INT(t->recent_cpu,4))-t->nice*2;
}

void
thread_update_load_avg(void){
  int ready_threads=list_size(&ready_list);
  
  if(thread_current()!=idle_thread){
    ready_threads++;
  }
  load_avg = ADD_FIXED_FIXED(
    MUL_FIXED_FIXED(DIV_FIXED_INT(CONVERT_TO_FIXED(59),60),load_avg),
    MUL_FIXED_INT(DIV_FIXED_INT(CONVERT_TO_FIXED(1),60),ready_threads)
    );
}

void 
thread_update_recent_cpu(struct thread *t,void* aux UNUSED){
  fixed_t load_avg_2=MUL_FIXED_INT(load_avg,2);
  t->recent_cpu = ADD_FIXED_INT(
    MUL_FIXED_FIXED(DIV_FIXED_FIXED(load_avg_2,ADD_FIXED_INT(load_avg_2,1)),t->recent_cpu),
    t->nice
    );
}

void
thread_increment_recent_cpu(void){
  struct thread* t=thread_current();
  if(t==idle_thread)return;
  t->recent_cpu=ADD_FIXED_INT(t->recent_cpu,1);
}

static void thread_update_priority_single(struct thread* t,void* aux UNUSED){
  t->priority = PRI_MAX-CONVERT_TO_INT_ZERO(DIV_FIXED_INT(t->recent_cpu,4))-t->nice*2;
}

void 
thread_update_all_priority(void){
  thread_foreach(thread_update_priority_single,NULL);
  list_sort(&ready_list,compare_priority_more,NULL);
}

void
thread_update_all_recent_cpu(void){
  thread_foreach(thread_update_recent_cpu,NULL);
}

/** Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/** Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /**< The scheduler runs with interrupts off. */
  function (aux);       /**< Execute the thread function. */
  thread_exit ();       /**< If function() returns, kill the thread. */
}

/** Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/** Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/** Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  
  if(thread_mlfqs){
    if(t==initial_thread){
      t->nice=0;
      t->recent_cpu=CONVERT_TO_FIXED(0);
    }
    else{
      struct thread* cur=thread_current();
      t->nice=cur->nice;
      t->recent_cpu=cur->recent_cpu;
    }
    thread_update_priority(t,NULL);
  }
  else{
    t->priority=t->orig_priority=priority;
    t->waiting_lock=NULL;
    list_init(&t->holding_locks);
  }
  
  t->wake_ticks=0;
  t->magic = THREAD_MAGIC;

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/** Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/** Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  ASSERT(intr_get_level()==INTR_OFF);
  if (list_empty (&ready_list)){
    return idle_thread;
  }
  else{
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
    //struct thread* next = list_entry(list_pop_max(&ready_list,compare_priority_less,NULL),struct thread,elem);
    //return next;
  }

}

/** Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/** Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/** Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/** Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/** used in sleep_list*/
static bool 
compare_wake_ticks(const struct list_elem* a,const struct list_elem* b,void* aux UNUSED){
  struct thread *t_a = list_entry(a, struct thread, elem);
  struct thread *t_b = list_entry(b, struct thread, elem);
  return t_a->wake_ticks<t_b->wake_ticks;
}

/** used in ready_list*/
// d

bool 
compare_priority_more(const struct list_elem* a,const struct list_elem* b,void* aux UNUSED){
  struct thread *t_a = list_entry(a, struct thread, elem);
  struct thread *t_b = list_entry(b, struct thread, elem);
  return t_a->priority>t_b->priority;
}

bool 
compare_priority_less(const struct list_elem* a,const struct list_elem* b,void* aux UNUSED){
  struct thread *t_a = list_entry(a, struct thread, elem);
  struct thread *t_b = list_entry(b, struct thread, elem);
  return t_a->priority<t_b->priority;
}

void 
list_debug(struct list* list){
  if(list==NULL)list=&ready_list;
  if(list_empty(list))return;
  // struct list_elem* e;
  // for(e = list_begin(list); e != list_end(list); e = list_next(e)){
  //   struct thread *t = list_entry(e, struct thread, elem);
  //   printf("%s %d\n",t->name,t->priority);
  // }
  int a=list_entry(list_front(list),struct thread,elem)->priority;
  int b=list_entry(list_max(list,compare_priority_less,NULL),struct thread,elem)->priority;
  ASSERT(a==b);
}
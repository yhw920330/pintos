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
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"

#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

//prj2_ 10_31 function for debug////////////////
void print_all_thread(void );
struct thread * search_by_tid (tid_t tid);
bool thread_check_already_exist(const char *name);




/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list ready_ml[64];

static int load_avg;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
{
	void *eip;                  /* Return address. */
	thread_func *function;      /* Function to call. */
	void *aux;                  /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
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

/* Initializes the threading system by transforming the code
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

	//@@@prm2_2 atomic하게 만들 단계 쓰일 lock 변수 
	lock_init( &my_lock);
	///@@@
	lock_init (&tid_lock);  //allocate_tid에서 release
	list_init (&ready_list);
	list_init (&all_list);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;

	//@@@prj2_2
	initial_thread->fork_cnt =0;//main thread가 자식 0으로 시작
	///  @@@///
	initial_thread->tid = allocate_tid ();
	///prj1 11.25
	list_init( &sleep_list);
	if(thread_mlfqs)
	{

		printf("init!\n");
		int i;
		for(i=0; i<64; i++)
		{
			list_init( &ready_ml[i]);
		}
		printf("thread_init");
		//print_all();
	}
	load_avg=0;


}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
	void
thread_start (void) 
{
	/* Create the idle thread. */
	struct semaphore start_idle;
	sema_init (&start_idle, 0);
	thread_create ("idle", PRI_MIN, idle, &start_idle);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&start_idle); //idle함수에서 sema_up
}

/* Called by the timer interrupt handler at each timer tick.
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

#ifndef USERPROG
	if(thread_aging_flag==true)
		thread_aging();
#endif
}

/* Prints thread statistics. */
	void
thread_print_stats (void) 
{
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
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
	enum intr_level old_level;

	ASSERT (function != NULL);

	//@@@ multi -oom에서 30개 까지라고 하였으므로 30으로 한계를 두었다
	if( thread_current() -> fork_cnt >30 )
	{
		return TID_ERROR;
	}


	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	//@prj2_2@@create 생성시 부모지정
#ifdef USERPROG
	t->parent = thread_current();
#endif

	/* Prepare thread for first run by initializing its stack.
	   Do this atomically so intermediate values for the 'stack' 
	   member cannot be observed. */
	old_level = intr_disable ();




	/////prj2_1 2015.10.30 12:57////////////////////////////////////
#ifdef USERPROG
	///prj2_1 2015:10:32 14:59 /update !! 이걸써줘야된대 (동기가)////
	struct list_elem *e;  
	/////////prj2_2 2015.11.07.............
	t->fork_cnt = t->parent ->fork_cnt +1; //forknum 
	t->exit_accepted =0;

	if(t->parent-> init_childlist_flag ==0){
		list_init( & t->parent->child);
		t->parent ->init_childlist_flag=1;
	}

	list_init( & t->file_list);
	list_push_back( &(t->parent->child), &(t->gene)); //부모의 child list마지막에 날 포함

	//.....................................


	//////--------------------------------////

#endif  


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

	intr_set_level (old_level);


	/* Add to run queue. */
	thread_unblock (t);
	///prj1
	if(thread_mlfqs)
	{
		// priority 조정
		cal_ml_pri(t);
	}

	if(thread_current() != idle_thread)
		thread_yield();
	///
	return tid;
}
void cal_ml_pri(struct thread *t){
	//int tp = PRI_MAX- f_to_i_round(div_fi(t->recent,4))- t-> nice *2;
	int tp;
	int64_t a,b,c,d;
	a=PRI_MAX;
	b=f_to_i_round(div_fi(t->recent_cpu,4));
	c= a-b;
	d= 2*t->nice;
	tp=c-d;

	if (tp> PRI_MAX)
		t->priority= PRI_MAX;
	else if ( tp<PRI_MIN)
		t->priority =PRI_MIN;
	else
		t->priority = tp;
}
void update_pri(struct thread *t, void *aux)
{
	int tp;
	int64_t a,b,c,d;
	a=PRI_MAX;
	b=f_to_i_round(div_fi(t->recent_cpu,4));
	c= a-b;
	d= 2*t->nice;
	tp=c-d;

	if (tp> PRI_MAX)
		t->priority= PRI_MAX;
	else if ( tp<PRI_MIN)
		t->priority =PRI_MIN;
	else
		t->priority = tp;
}
void update_recent_cpu( struct thread *t, void *aux)
{
	t->recent_cpu= add_fi(mul_ff(div_ff(2*load_avg,add_fi(2*load_avg,1)),t->recent_cpu),t->nice);

}
void cal_ml_recent_cpu(struct thread *t)
{
	int64_t a,b,c,d;
	int64_t res;
	a=mul_fi(load_avg,2);
	b= add_fi(a,1);
	c= div_ff(a,b);
	d= mul_fi(c,(t->recent_cpu));
	res=add_fi(d,t->nice);
	t->recent_cpu= res;


}
/* Puts the current thread to sleep.  It will not be scheduled
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
	//prj1
	list_sort(&ready_list,cmp_pri,NULL);
}

/* Transitions a blocked thread T to the ready-to-run state.
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
	//prj1
	// list_push_back (&ready_list, &t->elem);
	if(thread_mlfqs)
	{
		printf("unblock!\n");
		list_push_back(&ready_ml[t->priority],&t->elem);
		printf("unblock2\n");
	}
	else
	{
		list_insert_ordered(&ready_list, &t->elem, cmp_pri, NULL);
	}
	t->status = THREAD_READY;

	//prj1

	if( thread_current() != idle_thread &&  thread_current() ->priority < t->priority)
		thread_yield();
	////////////////////////

	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
	const char *
thread_name (void) 
{
	return thread_current ()->name;
}

/* Returns the running thread.
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

	//prj2 10:31 for debug///////////////
	if(t==NULL )
		print_all_thread();
	if( t->magic != THREAD_MAGIC)
		printf("threadmagic!\n");
	///////////
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
	tid_t
thread_tid (void) 
{
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
	void
thread_exit (void) 
{
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Remove thread from all threads list, set our status to dying,
	   and schedule another process.  That process will destroy us
	   when it calls thread_schedule_tail(). */
	intr_disable ();
	thread_current ()->status = THREAD_DYING;
	schedule ();
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
	void
thread_yield (void) 
{
	struct thread *cur = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (cur != idle_thread)
	{
		if(thread_mlfqs){
			printf("thread_yield\n");
			printf("!!! %s(%d) \n", cur->name,cur->priority);
			//print_all();
			list_insert_ordered(&ready_ml[cur->priority], &cur->elem,cmp_pri,NULL);
			printf("thrad_yiled2\n");
		}
		//    list_push_back (&ready_list, &cur->elem);
		else{
			list_insert_ordered(&ready_list, & cur->elem, cmp_pri, NULL);
		}
	}
	printf("yield3\n");
	printf("yield33\n");
	cur->status = THREAD_READY;
	printf("yiedld3_1123123123123\n");
	schedule ();
	printf("yeild4\n");
	intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
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

/* Sets the current thread's priority to NEW_PRIORITY. */
	void
thread_set_priority (int new_priority) 
{
	enum intr_level old_level=intr_disable();

	thread_current ()->priority = new_priority;
	if(!thread_mlfqs){
		if(thread_current() != idle_thread)
			thread_yield();
		//donate도 여기에구현해야할듯
	}

	else{//mlfqs
		cal_ml_pri(thread_current());
		thread_yield();
	}

	intr_set_level(old_level);
}

/* Returns the current thread's priority. */
	int
thread_get_priority (void) 
{
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
	void
thread_set_nice (int nice UNUSED) 
{
	/* Not yet implemented. */
	enum intr_level old_level =intr_disable();
	thread_current()->nice= nice;
	cal_ml_recent_cpu(thread_current());
	cal_ml_pri(thread_current());
	intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
	int
thread_get_nice (void) 
{
	/* Not yet implemented. */
	return thread_current() ->nice;
}

/* Returns 100 times the system load average. */
	int
thread_get_load_avg (void) 
{
	/* Not yet implemented. */
	return f_to_i_round( mul_fi(load_avg,100));
}

/* Returns 100 times the current thread's recent_cpu value. */
	int
thread_get_recent_cpu (void) 
{
	/* Not yet implemented. */
	int res= thread_current()->recent_cpu;
	res= f_to_i_round( mul_fi(res,100));
	return res;
}

/* Idle thread.  Executes when no other thread is ready to run.

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

/* Function used as the basis for a kernel thread. */
	static void
kernel_thread (thread_func *function, void *aux) 
{
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
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

/* Returns true if T appears to point to a valid thread. */
	static bool
is_thread (struct thread *t)
{

	return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
	static void
init_thread (struct thread *t, const char *name, int priority)
{
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->stack = (uint8_t *) t + PGSIZE;
	//t->priority = priority;
	if(!thread_mlfqs)
	{
		t->priority = priority;
	}
	else{
		t->nice=0;
		if( t== initial_thread)
			t->recent_cpu=0;
		else
		{
			t->recent_cpu= thread_current()->recent_cpu;
		}
	}



	t->magic = THREAD_MAGIC;
	////prj2//////////////////////////
	//////prj2_2 11/07 //////////
	t->init_childlist_flag=0;
	t->exit_accepted =0; 
	t->exit_status = -1;

	sema_init( &t->sema_wait,0);
	sema_init( &t->sema_orphan,0);
	/////////////////////

	list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
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

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
	static struct thread *
next_thread_to_run (void) 
{
	if(! thread_mlfqs){
		if (list_empty (&ready_list))
			return idle_thread;
		else
			return list_entry (list_pop_front (&ready_list), struct thread, elem);
	}
	else{ //mlfqs
		printf("next_thread_to_run\n");
		int contain_level=64;
		int b=PRI_MAX;
		for(b=63; b>=0; b--)
		{
			if(!list_empty( &ready_ml[b]))
			{
				contain_level=b;
				break;
			}
		}
		if(contain_level==64)
		{
			return idle_thread;

		}
		else {
			return list_entry(list_pop_front(&ready_ml[contain_level]),struct thread, elem);
		}


	}
}

/* Completes a thread switch by activating the new thread's page
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

/* Schedules a new process.  At entry, interrupts must be off and
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
printf("schedul1\n");
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (cur->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
printf("scehdule2\n");
	if (cur != next)
		prev = switch_threads (cur, next);
printf("sechduel3\n");
	thread_schedule_tail (prev);
printf("schedule4\n");
}

/* Returns a tid to use for a new thread. */
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

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);





/////debugging///////////////////////////////////////////////////////////
void print_all_thread()
{
	struct list_elem *e;
	printf("===print_all_thread================\n");
	for(e=list_begin(&all_list) ; e!=list_end(&all_list); e=list_next(e))
	{
		struct thread *t =list_entry(e, struct thread, allelem);
		printf("[%d] : %s :status-%d\n",t->tid, t->name,t->status);
	}

}
void print_ready_ml()
{
	int i;
	for(i=PRI_MAX; i>=0;i--){
		struct list_elem *e;
		printf("bucket: %d\n",i);
		for(e=list_begin(&ready_ml[i]) ; e!=list_end(&ready_ml[i]); e=list_next(e))
		{
			struct thread *t =list_entry(e, struct thread, allelem);
			printf("[%d] :%s  ||  ",t->tid, t->name,t->status);
		}
	}
}
void print_ready()
{
	struct list_elem *e;
	printf("===print_ready_queue================\n");
	for(e=list_begin(&ready_list) ; e!=list_end(&ready_list); e=list_next(e))
	{
		struct thread *t =list_entry(e, struct thread, allelem);
		printf("[%d] : %s :status-%d\n",t->tid, t->name,t->status);
	}

}
void print_sleep()
{
	struct list_elem *e;
	printf("===print_sleep_thread================\n");
	for(e=list_begin(&sleep_list) ; e!=list_end(&sleep_list); e=list_next(e))
	{
		struct thread *t =list_entry(e, struct thread, allelem);
		printf("[%d] : %s :status-%d\n",t->tid, t->name,t->status);
	}

}
void print_all()
{
	print_all_thread();

	if(!thread_mlfqs)
		print_ready();
	else
		print_ready_ml();
	print_sleep();

	printf("---------------------end!!!\n");
}
struct thread * search_by_tid (tid_t tid)
{
	struct list_elem *e;

	struct thread *tp=NULL;
	for( e=list_begin (&all_list); e!= list_end (& all_list ) ; e=list_next(e))
	{
		tp= list_entry( e, struct thread, allelem);
		if( tp->tid == tid)
		{
			return tp;
		}

	}

	return NULL;
}
///////////////////////////////////////////////////////////////////

bool thread_check_already_exist(const char *name)
{
	struct list_elem *e;
	struct thread *tp = NULL;
	for(e=list_begin ( &all_list) ; 
			e!= list_end( &all_list) ;
			e= list_next(e))
	{
		tp =list_entry ( e, struct thread, allelem);
		if(!(strcmp(name, tp->name)))
			return true;
	}

	return false;



}

//////////////////
//prj1_1
bool cmp_time(const struct list_elem *a,const struct list_elem *b, void *aux UNUSED){

	struct thread *x= list_entry(a, struct thread, elem);
	struct thread *y=list_entry(b, struct thread, elem);
	if( x->sleep_time < y->sleep_time)
		return true;
	else
		return false;
}

bool cmp_pri(const struct list_elem *a,const struct list_elem *b, void *aux UNUSED){

	struct thread *x= list_entry(a, struct thread, elem);
	struct thread *y=list_entry(b, struct thread, elem);
	if( x->priority > y->priority)
		return true;
	else
		return false;
}

void thread_sleep(int64_t waketime){

	enum intr_level old_level;
	struct thread *cur= thread_current();
	if( cur != idle_thread)
	{
		cur->sleep_time= waketime;
		//cur->status =THREAD_BLOCKED;
		printf("sleep1\n");
		list_insert_ordered ( &sleep_list, & cur->elem, cmp_time, NULL);
		printf("sleep2\n");
		thread_block();
		printf("sleep3\n");
		//	schedule();
		intr_set_level(old_level);
	}
}
void thread_wakeup(){

	struct thread *t;
	struct list_elem *e;

	if(thread_mlfqs){
		int all_empty=0;
		int i=63;
		while(1)
		{
			break;			
		}

		printf("mflqse!!\n");
	}
	else{
		while(!list_empty(&sleep_list))
		{
			t=list_entry(list_front(&sleep_list), struct thread, elem);
			if( t->sleep_time <= timer_ticks())
			{
				list_pop_front(&sleep_list);
				thread_unblock(t);

				//		list_insert_ordered(&ready_list,&t->elem, cmp_pri, NULL);
			}
			else{
				break;
			}
		}
	}
}

void test_preempt()
{
}

void thread_aging(void)
{
	struct thread *tp;
	struct list_elem *e;
	if( timer_ticks() % (TIME_SLICE*100)==0)
	{
		for(e=list_begin( &ready_list) ; e!=list_end( &ready_list); e=list_next(e))
		{
			tp=list_entry( e, struct thread, elem);
			if( tp->priority <PRI_MAX)
			{
				tp->priority ++;
			}

		}
	}
}

void cal_load_avg(void)
{
	struct list_elem *e;
	struct thread *t;
	int cnt=1; //running thread 포함
	int64_t a,b;
	for(e=list_begin(&all_list) ; e!= list_end(&all_list); e = list_next(e))
	{
		t=list_entry(e, struct thread, allelem);
		if( t!= idle_thread)
		{
			if( t->status == THREAD_READY)
				cnt++;
		}
	}
	a=div_fi( i_to_f(59),60);
	a=mul_ff(a,load_avg);
	b=mul_fi(div_fi(i_to_f(1),60),cnt);
	b= add_ff(a,b);
	load_avg=b;




}
//////////////////
int i_to_f(int n){
	return n*F;
}
int f_to_i(int x){
	return x/F;
}
int f_to_i_round(int x){
	if(x>=0)
		return (x+F/2)/F;
	else
		return (x-F/2)/F;
}
int add_ff(int x, int y){
	return x+y;
}
int add_fi(int x, int y)
{
	return x+i_to_f(y);
}
int sub_ff(int x, int y){
	return x-y;
}
int sub_fi(int x, int y){
	return x- i_to_f(y);
}

int mul_ff(int x, int y){
	return ((int64_t) x * y)/F;
}
int mul_fi(int x, int y){
	return x*y;
}
int div_ff(int x, int y){
	return ((int64_t)x*F)/y;
}
int div_fi(int x, int y){
	return x/y;
}
////////////////////////////////////////////////////////////////

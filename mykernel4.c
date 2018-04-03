/*   	Umix thread package
 *
 */

#include <setjmp.h>
#include <strings.h>

#include "aux.h"
#include "umix.h"
#include "mykernel4.h"

static int MyInitThreadsCalled = 0;	// 1 if MyInitThreads called, else 0

static int currThread = 0;
static int prevThread = -1;

static struct thread {			// thread table
	int valid;			// 1 if entry is valid, else 0
	jmp_buf env_sp;			// Stack Pointer
	jmp_buf env_pc;			// Program Counter for each thread
	void (*func)();
	int param;
	int id;
} thread[MAXTHREADS];

static int queue[MAXTHREADS-1];
static int head = 0;
static int tail = 0;
static int size = 0;

static int lastThreadIndex = 0;		// Keeps track of last used index in the threads array

#define STACKSIZE	65536		// maximum size of thread stack
#define MAXTHREADS_Q	9

//------------------ QUEUE FUNCTIONS -----------------------------//
void shiftQueue(int pos);

/*
 * Checks if the thread id queue is empty
 */
int isEmpty() {
	return (size == 0);
}


/*
 * Checks if the thread id queue is full
 */
int isFull() {
	return (size == MAXTHREADS_Q);
}


/*
 * Adds the given id to the front of the thread id queue
 */
void addFront(int id) {

	if(isFull()) return;

	if(isEmpty())	queue[head] = id;
	else {
		--head;
		if(head < 0) head = MAXTHREADS_Q - 1;
		if(head == tail) {
			head = ++head % MAXTHREADS_Q;
			return;
		}
		
		queue[head] = id;
	}

	size++;
}


/*
 * Adds the given id to the back of the thread id queue
 */
void addBack(int id) {

	if(isFull()) return;

	if(isEmpty())	queue[tail] = id;
	else {
		tail = ++tail % MAXTHREADS_Q ;
		if(head == tail) {
			--tail;
			if(tail < 0) tail = MAXTHREADS_Q - 1;
			return;
		}
		
		queue[tail] = id;
	}

	size++;
}


/*
 * Removes the given id from the thread id queue
 */
int remove(int id) {
	
	if(isEmpty()) return -1;

	if(head == tail) { 
		queue[head] = -1;
	}

	else if(queue[head] == id) {
		queue[head] = -1;		
		head = ++head % MAXTHREADS_Q;
	}
	else if(queue[tail] == id)	{
		queue[tail] = -1;		
		tail--;
		if(tail < 0)	tail = MAXTHREADS_Q - 1;
	}  else {
		int i;
		int pos = -1;
		for(i=0; i < MAXTHREADS_Q; i++) {
			if(queue[i] == id) pos = i;
		}

		if(pos != -1)	shiftQueue(pos);
	}

	size--;
	return id;
}


/*
 * Shifts the queue to the left
 */
void shiftQueue(int pos) {
	
	int delete;
	int curr = (pos + 1) % MAXTHREADS_Q;
	int count = 0;
	
	while(pos != tail) {
		pos = ++pos % MAXTHREADS_Q;
		count++;
	}

	while(count > 0) {
		delete = curr - 1;
		if(delete < 0) delete = MAXTHREADS_Q - 1;

		queue[delete] = queue[curr];
		curr = ++curr % MAXTHREADS_Q;

		count--;
	}

	queue[(delete+1) % MAXTHREADS_Q] = -1;
	tail--;
	if(tail < 0) tail = MAXTHREADS_Q - 1; 
}


//----------------- END OF QUEUE FUNCTINS ------------------------//


/* 	MyInitThreads () initializes the thread package. Must be the first
 *  	function called by any user program that uses the thread package.  
 */

void MyInitThreads ()
{
	int i;
	
	if (MyInitThreadsCalled) {		// run only once
		Printf ("MyInitThreads: should be called only once\n");
		Exit ();
	}

	for (i = 0; i < MAXTHREADS; i++) {	// initialize thread table
		thread[i].valid = 0;
	}

	for(i = 0; i < MAXTHREADS_Q; i++) {
		queue[i] = -1;
	}

	// Setting initial values, thread 0 running
	
	thread[0].valid = 1;
	MyInitThreadsCalled = 1;
	
	// Saves all threads in their appropiate SP. Also executes their functions when they are called
	for(i = 0; i < MAXTHREADS; i++) {
		char stack[i*STACKSIZE+100];

	        if(setjmp(thread[i].env_sp) == 0) {
			if (((int) &stack[STACKSIZE-1]) - ((int) &stack[0]) + 1 != STACKSIZE) {
				Printf ("Stack space reservation failed\n");
				Exit ();
			}
		} else {
			(*thread[MyGetThread()].func) (thread[MyGetThread()].param);
			MyExitThread ();
		}	
	}
}

/*  	MyCreateThread (f, p) creates a new thread to execute
 * 	f (p), where f is a function with no return value and
 *  	p is an integer parameter.  The new thread does not begin
 * 	executing until another thread yields to it. 
 */

int MyCreateThread (f, p)
	void (*f)();			// function to be executed
	int p;				// integer parameter
{
	int i;	
	int threadID;

	if(isFull()) return -1;

	if (! MyInitThreadsCalled) {
		Printf ("MyCreateThread: Must call MyInitThreads first\n");
		Exit ();
	}

	// Finding next available thread, if any

	int counter = 0;
	int index = (lastThreadIndex + 1) % MAXTHREADS;
	
	for(; counter < MAXTHREADS; counter++) {
		
		if(!thread[index].valid) {
			threadID = index;
			thread[index].valid = 1;
			thread[index].id = threadID;
			addBack(threadID);
			lastThreadIndex = index;
			break;
		}
			
		index = ++index % MAXTHREADS;
	}

	if(threadID < 0 || threadID >= MAXTHREADS) return -1;

	thread[threadID].func = f;
	thread[threadID].param = p;
	memcpy(thread[threadID].env_pc, thread[threadID].env_sp, sizeof(jmp_buf));
		
	return threadID;		// done, return new thread ID
}

/*   	MyYieldThread (t) causes the running thread, call it T, to yield to
 * 	thread t.  Returns the ID of the thread that yielded to the calling
 *  	thread T, or -1 if t is an invalid ID. Example: given two threads
 *  	with IDs 1 and 2, if thread 1 calls MyYieldThread (2), then thread 2
 * 	will resume, and if thread 2 then calls MyYieldThread (1), thread 1
 *  	will resume by returning from its call to MyYieldThread (2), which
 * 	will return the value 2.
 */

int MyYieldThread (t)
	int t;				// thread being yielded to
{	
	if(MyGetThread() == t) return t;	
 	
	if (! MyInitThreadsCalled) {
		Printf ("MyYieldThread: Must call MyInitThreads first\n");
		Exit ();
	}

	if (t < 0 || t >= MAXTHREADS) {
		Printf ("MyYieldThread: %d is not a valid thread ID\n", t);
		return (-1);
	}
	if (! thread[t].valid) {
		Printf ("MyYieldThread: Thread %d does not exist\n", t);
		return (-1);
	}

	// Removes thread t from the queue and runs it. It moves the
	// current thread to the back of the queue if valid

	remove(t);
	if(thread[MyGetThread()].valid)	addBack(MyGetThread()); 

        if (setjmp (thread[MyGetThread()].env_pc) == 0) {
		prevThread = MyGetThread();
		currThread = t;
                longjmp (thread[t].env_pc, 1);
        }	

	return prevThread;
}

/*   	MyGetThread () returns ID of currently running thread. 
 */

int MyGetThread ()
{
	if (! MyInitThreadsCalled) {
		Printf ("MyGetThread: Must call MyInitThreads first\n");
		Exit ();
	}

	return currThread;

}

/* 	MySchedThread () causes the running thread to simply give up the
 *  	CPU and allow another thread to be scheduled.  Selecting which
 *  	thread to run is determined here.  Note that the same thread may
 * 	be chosen (as will be the case if there are no other threads). 
 */

void MySchedThread ()
{
	if (! MyInitThreadsCalled) {
		Printf ("MySchedThread: Must call MyInitThreads first\n");
		Exit ();
	}

	if(isEmpty()) return;
	MyYieldThread(queue[head]);
}

/*  	MyExitThread () causes the currently running thread to exit.  
 */
void MyExitThread ()
{
	if (! MyInitThreadsCalled) {
		Printf ("MyExitThread: Must call MyInitThreads first\n");
		Exit ();
	}

	thread[MyGetThread()].valid = 0;
	if(!isEmpty())	MySchedThread();
	else Exit();	
}

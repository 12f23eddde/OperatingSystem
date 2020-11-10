// scheduler.h 
//	Data structures for the thread dispatcher and scheduler.
//	Primarily, the list of threads that are ready to run.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "copyright.h"
#include "list.h"
#include "thread.h"

// The following class defines the scheduler/dispatcher abstraction -- 
// the data structures and operations needed to keep track of which 
// thread is running, and which threads are ready but not running.

enum SchedulerPolicy { NAIVE, STATICPRIORTY, ROUND_ROBIN };

class Scheduler {
  public:
    Scheduler(SchedulerPolicy _policy=NAIVE, int _duration=200);			// Initialize list of ready threads 
    ~Scheduler();			// De-allocate ready list

    void ReadyToRun(Thread* thread);	// Thread can be dispatched.
    Thread* FindNextToRun();		// Dequeue first thread on the ready 
					// list, if any, and return thread.
    void Run(Thread* nextThread);	// Cause nextThread to start running
    void Print();			// Print contents of ready list
   
    // [lab2] manually set policy
    void setPolicy(SchedulerPolicy newpolicy);
    // [lab2] rr
    void setSwitchDuration(int newduration);
    static void handleThreadTimeUp(int ptr_int);
    
  private:
    List *readyList;  		// queue of threads that are ready to run, but not running
    // [lab2] add policy to scheduler   
    SchedulerPolicy policy;

    // [lab2] Round-Robin
    int switchDuration;   // switchDuration may not be accurate
    int lastCalledTick;
    void inline resetCalledTick();
};

#endif // SCHEDULER_H

// synch.cc 
//	Routines for synchronizing threads.  Three kinds of
//	synchronization routines are defined here: semaphores, locks 
//   	and condition variables (the implementation of the last two
//	are left to the reader).
//
// Any implementation of a synchronization routine needs some
// primitive atomic operation.  We assume Nachos is running on
// a uniprocessor, and thus atomicity can be provided by
// turning off interrupts.  While interrupts are disabled, no
// context switch can occur, and thus the current thread is guaranteed
// to hold the CPU throughout, until interrupts are reenabled.
//
// Because some of these routines might be called with interrupts
// already disabled (Semaphore::V for one), instead of turning
// on interrupts at the end of the atomic operation, we always simply
// re-set the interrupt state back to its original value (whether
// that be disabled or enabled).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "synch.h"
#include "system.h"

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	Initialize a semaphore, so that it can be used for synchronization.
//
//	"debugName" is an arbitrary name, useful for debugging.
//	"initialValue" is the initial value of the semaphore.
//----------------------------------------------------------------------

Semaphore::Semaphore(char* debugName, int initialValue)
{
    name = debugName;
    value = initialValue;
    queue = new List;
}

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	De-allocate semaphore, when no longer needed.  Assume no one
//	is still waiting on the semaphore!
//----------------------------------------------------------------------

Semaphore::~Semaphore()
{
    delete queue;
}

//----------------------------------------------------------------------
// Semaphore::P
// 	Wait until semaphore value > 0, then decrement.  Checking the
//	value and decrementing must be done atomically, so we
//	need to disable interrupts before checking the value.
//
//	Note that Thread::Sleep assumes that interrupts are disabled
//	when it is called.
//----------------------------------------------------------------------

void
Semaphore::P()
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);	// disable interrupts
    
    while (value == 0) { 			// semaphore not available
	queue->Append((void *)currentThread);	// so go to sleep
	currentThread->Sleep();
    } 
    value--; 					// semaphore available, 
						// consume its value
    
    (void) interrupt->SetLevel(oldLevel);	// re-enable interrupts
}

//----------------------------------------------------------------------
// Semaphore::V
// 	Increment semaphore value, waking up a waiter if necessary.
//	As with P(), this operation must be atomic, so we need to disable
//	interrupts.  Scheduler::ReadyToRun() assumes that threads
//	are disabled when it is called.
//----------------------------------------------------------------------

void
Semaphore::V()
{
    Thread *thread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    thread = (Thread *)queue->Remove();
    if (thread != NULL)	   // make thread ready, consuming the V immediately
	scheduler->ReadyToRun(thread);
    value++;
    (void) interrupt->SetLevel(oldLevel);
}

// Dummy functions -- so we can compile our later assignments 
// Note -- without a correct implementation of Condition::Wait(), 
// the test case in the network assignment won't work!

Lock::Lock(char* debugName) {
    name = debugName;
    mutex = new Semaphore(debugName, 1);  // init with val 1
//    owner = NULL;
}

Lock::~Lock() {
    delete mutex;
}

// [lab3] atomic funtion Acquire
// to simplify things, we settle with P/V
// YOU NEED TO PROTECT OWNER!
void Lock::Acquire() {
    DEBUG('-t', "Acquiring lock\n");
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    // begin critical zone
    mutex->P();
//    owner = currentThread;
    // end critical zone
    (void) interrupt->SetLevel(oldLevel);
    DEBUG('-t', "Acquired lock\n");
}

// [lab3] atomic funtion Release
// to simplify things, we settle with P/V
// YOU NEED TO PROTECT OWNER!
void Lock::Release() {
    DEBUG('-t', "Releasing lock\n");
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    // begin critical zone
    mutex->V();
//    owner = NULL;
    // end critical zone
    (void) interrupt->SetLevel(oldLevel);
    DEBUG('-t', "Released lock\n");
}

//bool Lock::isHeldByCurrentThread() {
//    return currentThread == owner;
//}

Condition::Condition(char* debugName) { 
    name = debugName;
    queue = new List();
}
Condition::~Condition() { 
    delete queue;
}

// [lab3] atomic funtion Wait
// release the lock, relinquish the CPU until signaled, then re-acquire the lock
void Condition::Wait(Lock* conditionLock) { 
    DEBUG('-t',"Condition::Wait\n");
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    // begin critical zone
    conditionLock->Release();  // release the lock
    // from Semaphore::P()  // relinquish the CPU until signaled
    queue->Append((void *)currentThread);
	currentThread->Sleep();
    conditionLock->Acquire(); // re-acquire the lock
    // end critical zone
    (void) interrupt->SetLevel(oldLevel);
}

// [lab3] atomic function Signal
// wake up a thread, if there are any waiting on the condition
void Condition::Signal(Lock* conditionLock) { 
    Thread* thread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    // begin critical zone
    
//    ASSERT(conditionLock->isHeldByCurrentThread());
    // from Semaphore::V()
    thread = (Thread *)queue->Remove();
    if (thread != NULL)	 { 
        scheduler->ReadyToRun(thread);
    }  
    // end critical zone
    (void) interrupt->SetLevel(oldLevel);
}

// [lab3] atomic function Broadcast
// wake up all threads waiting on the condition
void Condition::Broadcast(Lock* conditionLock) { 
    Thread* thread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    // begin critical zone
//    ASSERT(conditionLock->isHeldByCurrentThread());
    // from Semaphore::P()
    thread = (Thread*) queue->Remove();
    while (thread != NULL)	 {
        scheduler->ReadyToRun(thread);
        thread = (Thread *)queue->Remove();
    }  
    // end critical zone
    (void) interrupt->SetLevel(oldLevel);
}

Barrier::Barrier(int _threadsToWait){
    threadsToWait = _threadsToWait;
    threadsArrived = 0;
    mutex = new Lock("barrierLock");
    barrier = new Condition("barrierCondition");
}

Barrier::~Barrier(){
    delete mutex, barrier;
}

// [lab3] atomic function arrive_and_wait
void Barrier::arrive_and_wait(){
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    mutex->Acquire();
    // begin critical zone
    
    threadsArrived++;
    if(threadsArrived == threadsToWait){
        barrier->Broadcast(mutex);
        threadsArrived=0;
        printf("[Barrier::arrive_and_wait] Waking up threads\n");
    }else{
        barrier->Wait(mutex);
    }

    // end critical zone
    mutex->Release();
    (void) interrupt->SetLevel(oldLevel);
}
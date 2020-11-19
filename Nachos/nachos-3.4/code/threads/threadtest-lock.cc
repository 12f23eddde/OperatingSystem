// threadtest.cc 
//	Simple test case for the threads assignment.
//
//	Create two threads, and have them context switch
//	back and forth between themselves by calling Thread::Yield, 
//	to illustratethe inner workings of the thread system.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "elevatortest.h"
#include "synch.h"

// testnum is set in main.cc
int testnum = 1;

//----------------------------------------------------------------------
// SimpleThread
// 	Loop 5 times, yielding the CPU to another ready thread 
//	each iteration.
//
//	"which" is simply a number identifying the thread, for debugging
//	purposes.
//----------------------------------------------------------------------

void
SimpleThread(int which)
{
    int num;
    
    for (num = 0; num < 5; num++) {
	printf("*** thread %d looped %d times\n", which, num);
        currentThread->Yield();
    }
}


//----------------------------------------------------------------------
// ThreadTest1
// 	Set up a ping-pong between two threads, by forking a thread 
//	to call SimpleThread, and then calling SimpleThread ourselves.
//----------------------------------------------------------------------

void
ThreadTest1()
{
    DEBUG('t', "Entering ThreadTest1");

    Thread *t = new Thread("forked thread");

    t->Fork(SimpleThread, (void*)1);
    SimpleThread(0);
}

// ---- LAB1 ---- //

void Lab1Thread(int uid, int tid){
	printf("Created thread %d@%d\n", tid, uid);
}

void Lab1Test1(){
    int test_uids[4] = {114, 514, 1919, 810};
    for (int tc = 0; tc < 114; tc++){
        DEBUG('t', "Entering Lab1Thread uid=%d", test_uids[tc%4]);
        Thread *t = new Thread("Lab1Thread");
        t->setUserId(test_uids[tc%4]);
        Lab1Thread(t->getUserId(), t->getThreadId());
        if(tc%4==0) t->~Thread();
    }
}

void Lab1Test2(){
    int test_uids[4] = {114, 514, 1919, 810};
    for (int tc = 0; tc < 129; tc++){
        DEBUG('t', "Entering Lab1Thread uid=%d", test_uids[tc%4]);
        Thread *t = new Thread("Lab1Thread");
        t->setUserId(test_uids[tc%4]);
        Lab1Thread(t->getUserId(), t->getThreadId());
    }
}

void Lab1Test3(){
    int test_uids[4] = {114, 514, 1919, 810};
    for (int tc = 0; tc < 10; tc++){
        DEBUG('t', "Entering Lab1Thread uid=%d", test_uids[tc%4]);
        Thread *t = new Thread("Lab1Thread");
        t->setUserId(test_uids[tc%4]);
        Lab1Thread(t->getUserId(), t->getThreadId());
        if(tc%4==0) t->~Thread();
    }
    printThreadsList();
}

// ---- LAB2 ---- //

// NOTE: with the implementation of fork()
// thread must take exactly ONE void* ARG
// however, VoidFunctionPtr requires ONE int ARG

// 0 - Do nothing
// 1 - Yield
// 2 - Sleep
// 3 - Finish
void Lab2Thread(int dummy){
    printf("[%d] name=\"%s\" pr=%d\n", currentThread->getThreadId(), currentThread->getName(),currentThread->getPriority());
    currentThread->Finish(); 
}

void Lab2Test1(){
    Thread *t1 = new Thread("create pr=114", 114);

    Thread *t2 = new Thread("set pr=19");
    t2->setPriority(19);

    Thread *t3 = new Thread("default pr=0");

    t1->Fork(Lab2Thread, (void*)1);
    t2->Fork(Lab2Thread, (void*)1);
    t3->Fork(Lab2Thread, (void*)1);

    Lab2Thread(1);

    printThreadsList();
}


void Lab2Thread2(int pr){
    printf("(%d) [%d] name=%s Forking...\n", stats->totalTicks, currentThread->getThreadId(), currentThread->getName());
    interrupt->OneTick();  // extend life for 10 ticks
    if (pr/2>=4){
        Thread *t = new Thread("Thread", pr/2);
        t->Fork(Lab2Thread2, (void*)(pr/2));
    }
    interrupt->OneTick();  // extend life for 10 ticks
    printf("(%d) [%d] name=%s Exiting...\n", stats->totalTicks, currentThread->getThreadId(), currentThread->getName());
}

void Lab2Test2(){
    scheduler->setPolicy(STATICPRIORTY);

    Thread *t1 = new Thread("Thread0", 114);
    t1->Fork(Lab2Thread2, (void*)114);

    scheduler->Print();

    currentThread->Finish();  // mark for destroy
}


// NOTE: NachOS's timer
// Time got stuck in NachOS
// Unless you call OneTick() or setLevel()
void Lab2Thread3(int ticks){
    int cnt_ticks = 0;
    while(ticks--){
        printf("(%d) [%d] name=%s Running for %d ticks\n", 
            stats->totalTicks, currentThread->getThreadId(), currentThread->getName(), 10*cnt_ticks++);
        interrupt->OneTick();  // extend life for 10 ticks
    }

    currentThread->Finish(); 
}

// NOTE: priority not implemented in RR
void Lab2Test3(){
    scheduler->setPolicy(ROUND_ROBIN);
    scheduler->setSwitchDuration(100);

    Thread *t0 = new Thread("some");
    Thread *t1 = new Thread("times");
    Thread *t2 = new Thread("naive");

    t0->Fork(Lab2Thread3, (void*)30);
    t1->Fork(Lab2Thread3, (void*)8);
    t2->Fork(Lab2Thread3, (void*)20);
}


//  ----------- [lab3] Thread Test ----------  //
//  ----------- 12f23eddde  Nov 18 ----------  //

// [lab3] to avoid stack overflow

class Buffer{
    public:
        Buffer(int bufferSize);
        ~Buffer();
        void insertItem(int newelement);
        int popItem();
        unsigned int bufferUsed(){return buffer->NumInList();}
        unsigned int bufferTotal(){return bufferSize;}
        void printBuffer();
    private:
        List *buffer;  // Using linked list List* to avoid stack overflow
        // semaphores
        Semaphore *empty;
        Semaphore *full;
        // locks
        Lock* mutex;
        // vars for debug
        unsigned int bufferSize;
};

Buffer::Buffer(int bufferSize):bufferSize(bufferSize){
    buffer = new List;
    empty = new Semaphore("empty", bufferSize);
    full = new Semaphore("full", 0);
    mutex = new Lock("bufferLock");
}

Buffer::~Buffer(){
    delete empty, full, buffer;
}

// [lab3] bufferPrint
// depends on what the buffer is
void bufferPrint(int ptr){
    printf("%d,",ptr);
}

void Buffer::printBuffer(){
    printf("used=%d/%d elements=[",bufferUsed(),bufferSize);
    buffer->Mapcar(bufferPrint); // apply bufferPrint to all elements in buffer
    printf("]\n");
}

void Buffer::insertItem(int newelement){
    empty->P();
    mutex->Acquire();

    if ((void*)newelement == NULL){
        printf("[Buffer::insertItem] You may not insert NULL to a List\n");
        ASSERT(FALSE);
    }

    buffer->Append((void*)newelement);

    unsigned int _bufferUsed = bufferUsed();
    if(_bufferUsed > bufferSize){
        printf("[Buffer::insertItem] buffer overflow: %d/%d\n",_bufferUsed, bufferSize);
        ASSERT(FALSE);
    }

    mutex->Release();
    full->V();
}

int Buffer::popItem(){
    full->P();
    mutex->Acquire();
    
    void* temp = buffer->Remove();
    
    if(temp == NULL){
        printf("[Buffer::insertItem] No Element in Buffer\n");
        ASSERT(FALSE);
    }
    int topelement = (int) temp;

    mutex->Release();
    empty->V();

    return topelement;
}

void producerThread(int ptr){
    int times = 5;
    while(times--){
        Buffer *currBuffer = (Buffer*) ptr;
        int item = Random()%114514;  // generate item
        printf("(%s) attemting to insert %d to list\n", currentThread->getName(), item);
        currBuffer->insertItem(item);
        printf("(%s) inserted item %d to list, Buffer ",currentThread->getName(), item);
        currBuffer->printBuffer();

        interrupt->OneTick(); // make time going
    }
}

void consumerThread(int ptr){
    int times = 5;
    while(times--){
        Buffer *currBuffer = (Buffer*) ptr;
        printf("(%s) trying to pop item\n",currentThread->getName());
        int item = currBuffer->popItem();
        printf("(%s) got item %d from list, Buffer ",currentThread->getName(), item);
        currBuffer->printBuffer();

        interrupt->OneTick(); // make time going
    }
}

void Lab3Test1(){
    printf("Entering Test8\n");
    Buffer *currBuffer = new Buffer(3);

    Thread *p1 = new Thread("Producer 1");
    Thread *p2 = new Thread("Producer 2");
    Thread *c1 = new Thread("Consumer 1");
    Thread *c2 = new Thread("Consumer 2");

    p1->Fork(producerThread, (void*) currBuffer);
    p2->Fork(producerThread, (void*) currBuffer);
    c2->Fork(consumerThread, (void*) currBuffer);
    c1->Fork(consumerThread, (void*) currBuffer);
}

void Lab3Challenge1(){

}

//----------------------------------------------------------------------
// ThreadTest
// 	Invoke a test routine.
//----------------------------------------------------------------------

void
ThreadTest()
{
    switch (testnum) {
        case 1: ThreadTest1();break;
        case 2: Lab1Test1();break;
        case 3: Lab1Test2();break;
        case 4: Lab1Test3();break;
        case 5: Lab2Test1();break;
        case 6: Lab2Test2();break;
        case 7: Lab2Test3();break;
        case 8: Lab3Test1();break;
        case 9: Lab3Challenge1();break;
        default: printf("No test specified.\n");break;
    }
}


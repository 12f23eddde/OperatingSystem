// progtest.cc 
//	Test routines for demonstrating that Nachos can load
//	a user program and execute it.  
//
//	Also, routines for testing the Console hardware device.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

///// Just for hint /////
///// comment when running /////
// # define USER_PROGRAM
// # define FILESYS_NEEDED


#include "copyright.h"
#include "system.h"
#include "console.h"
#include "addrspace.h"
#include "synch.h"

//----------------------------------------------------------------------
// StartProcess
// 	Run a user program.  Open the executable, load it into
//	memory, and jump to it.
//----------------------------------------------------------------------

# define numInstances 1

// [lab4] starting a userprog thread
void RunSingleProcess(int threadNo){
    DEBUG('u', "\033[1;33m[RunSingleProcess] Starting %d-userProg\033[0m\n",threadNo);
    // load registers to machine
    currentThread->space->InitRegisters();
    // load page table to machine
    currentThread->space->RestoreState();
    // run!
    machine->Run();
    // Not Reached
    printf("\033[1;31m[RunSingleProcess] Hit Buttom! \n\033[0m");
    ASSERT(FALSE);
}

// [lab4] Modified to run multiple instances
void
StartProcess(char *filename)
{
    OpenFile *executable = fileSystem->Open(filename);
    AddrSpace *space;

    if (executable == NULL) {
	    printf("Unable to open file %s\n", filename);
	    return;
    }

    Thread *t [10];

    for(int i = 1; i <= numInstances; i++){
        char* name = new char[10];
        name[0] = (char)(i + '0');
        strcat(name, "-userProg");
        t[i-1] = new Thread(name, i);
        t[i-1]->space = new AddrSpace(executable, i);
        machine->printMem(machine->mainMemory);
        t[i-1]->Fork(RunSingleProcess, (void*)i);
    }

    // Preemution is disabled!
    // Manually yield the thread
    currentThread->Yield();

    // delete executable;			// close file

    // space->InitRegisters();		// set the initial register values
    // space->RestoreState();		// load page table register

    // machine->Run();			// jump to the user progam
    // ASSERT(FALSE);			// machine->Run never returns;
	// 				// the address space exits
	// 				// by doing the syscall "exit"
}

// Data structures needed for the console test.  Threads making
// I/O requests wait on a Semaphore to delay until the I/O completes.

static Console *console;
static Semaphore *readAvail;
static Semaphore *writeDone;

//----------------------------------------------------------------------
// ConsoleInterruptHandlers
// 	Wake up the thread that requested the I/O.
//----------------------------------------------------------------------

static void ReadAvail(int arg) { readAvail->V(); }
static void WriteDone(int arg) { writeDone->V(); }

//----------------------------------------------------------------------
// ConsoleTest
// 	Test the console by echoing characters typed at the input onto
//	the output.  Stop when the user types a 'q'.
//----------------------------------------------------------------------

void 
ConsoleTest (char *in, char *out)
{
    char ch;

    console = new Console(in, out, ReadAvail, WriteDone, 0);
    readAvail = new Semaphore("read avail", 0);
    writeDone = new Semaphore("write done", 0);
    
    for (;;) {
	readAvail->P();		// wait for character to arrive
	ch = console->GetChar();
	console->PutChar(ch);	// echo it!
	writeDone->P() ;        // wait for write to finish
	if (ch == 'q') return;  // if q, quit
    }
}

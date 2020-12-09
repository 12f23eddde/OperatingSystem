// exception.cc 
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.  
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

///////////    Just to enable code hint    ///////////
// # define USE_TLB
// # define USER_PROGRAM
// # define USE_FIFO
// # define USE_CLOCK

#include "copyright.h"
#include "system.h"
#include "syscall.h"

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2. 
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions 
//	are in machine.h.
//----------------------------------------------------------------------


//////////    [lab4] Just for now   //////////  
int TLBToCheck = 0;

// [lab4] Real Page Fault
void handlePageFault(unsigned int vpn){
    DEBUG('V', "[handlePageFault] vpn=%d\n", vpn);

    // PPN might not exist; require a new page
    int newPPN = machine->allocBit();

    // if (newPPN == -1){
    //      printf("\033[1;31m[handlePageFault] Not Implemented @vpn=%d\n\033[0m", vpn);
    //      ASSERT(FALSE);
    // }

    // machine->printTE(machine->pageTable, machine->pageTableSize);

    if(newPPN >= 0) DEBUG('V', "[handlePageFault] Found available ppn=%d\n", newPPN);

    if (newPPN == -1) {  // all physical page used up,,,
        for (int i = 0; i < machine->pageTableSize, i!=vpn; i++){
            // if(i == vpn) continue;
            if(machine->pageTable[i].valid && !machine->pageTable[i].dirty){
                DEBUG('V', "[handlePageFault] Found non-dirty vpn=%d ppn=%d\n", i, machine->pageTable[i].physicalPage);
                machine->pageTable[i].valid = FALSE;  // Declared!
                newPPN = machine->pageTable[i].physicalPage;  // It's Mine!
                break;
            }
        }
    }

    if (newPPN == -1) {  // all physical pages written,,,
        for (int i = 0; i < machine->pageTableSize, i!=vpn; i++){
            // if(i == vpn) continue;
            if(machine->pageTable[i].valid && machine->pageTable[i].dirty){
                DEBUG('V', "[handlePageFault] Found dirty vpn=%d ppn=%d\n", i, machine->pageTable[i].physicalPage);
                currentThread->space->dumpPageToVM(i);  // Written, need to write back
                machine->pageTable[i].valid = FALSE;  // Declared!
                newPPN = machine->pageTable[i].physicalPage;  // It's Mine!
                break;
            }
        }
    }

    if (newPPN == -1) {  // wtf,,,
        // DEBUG
        printf("\033[1;31m[handlePageFault] No valid entry found, but somehow mem went full. Switching to other threads\n");
        machine->printTE(machine->pageTable, machine->pageTableSize);
        // ASSERT(FALSE);
        // Don't load, wait for other threads to finish then try again
        currentThread->Yield();
        return;  
    }

    // submit our result
    machine->pageTable[vpn].physicalPage = newPPN;
    machine->pageTable[vpn].valid = TRUE;
    machine->pageTable[vpn].use = FALSE;
    machine->pageTable[vpn].dirty = FALSE;
    machine->pageTable[vpn].readOnly = FALSE;

    // [lab4] Hack: our vm is as big as AddrSpace
    // load it from vm = create a new page
    // load page / create new
    if(currentThread->space->loadPageFromVM(vpn) != NoException){
        printf("\033[1;31m[handlePageFault] failed loading page @vpn=%d\n\033[0m", vpn);
    }

    DEBUG('V', "[handlePageFault] Finished! ppn=%d\n", newPPN);
}

// nextPC = PC in this case
void handleTLBMiss(unsigned int virtAddr){
    unsigned int vpn = (unsigned) virtAddr / PageSize;
    unsigned int offset = (unsigned) virtAddr % PageSize;
    int TLBToReplace = -1;
    // try to find a empty entry
    for (int i = 0; i < TLBSize; i++){
        if (!machine->tlb[i].valid) {
            TLBToReplace = i;
            break;
        }
    }
    // TLB full
    if(TLBToReplace == -1){
        #ifdef USE_FIFO
            // use last entry
            for (int i = 0; i < TLBSize - 1; i++){
                machine->tlb[i] = machine->tlb[i+1];
            }
            TLBToReplace = TLBSize - 1;
        #endif
        #ifdef USE_CLOCK
            for(int j = 0; j < 2; j++){
                // check for r=0/1, m=0
                for (int i = 0; i < TLBSize; i++){  // choose TLBToCheck
                    if(!machine->tlb[TLBToCheck].use&&!machine->tlb[TLBToCheck].dirty){
                        TLBToReplace = TLBToCheck;
                        break;
                    }
                    TLBToCheck = (TLBToCheck + 1) % TLBSize;
                }
                // success
                if(TLBToReplace != -1) break;

                // check for r=0/1, m=1
                for (int i = 0; i < TLBSize; i++){
                    if(!machine->tlb[TLBToCheck].use&&machine->tlb[TLBToCheck].dirty){  // choose TLBToCheck
                        TLBToReplace = TLBToCheck;
                        break;
                    }else{
                        machine->tlb[TLBToCheck].use = false; // set use bit
                    }
                    TLBToCheck = (TLBToCheck + 1) % TLBSize;
                }
                // success
                if(TLBToReplace != -1) break;
            }
        #endif
    }

    // check if we got something wrong
    if (0 > TLBToReplace || TLBToReplace >= TLBSize){
        printf("\033[1;31m[handleTLBMiss] invalid TLB entry to replace %d\n\033[0m", TLBToReplace);
        ASSERT(FALSE);
    }

    // Check if we got an actual page fault
    if (!machine->pageTable[vpn].valid) handlePageFault(vpn);
    
    // update TLB entry
    machine->tlb[TLBToReplace] = machine->pageTable[vpn];

    // Final Check
    if (machine->tlb[TLBToReplace].virtualPage < 0 || machine->tlb[TLBToReplace].virtualPage >= NumPhysPages){
        printf("\033[1;31m[handleTLBMiss] You are trying to insert invalid item %x\n\033[0m", machine->tlb[TLBToReplace].virtualPage);
        ASSERT(FALSE);
    }
}

void printTLBMisses(){
    printf("[TLB] Miss Rate=%.2f% (%d/%d)", 
        (100*machine->TLBMissed)/(float)machine->TLBUsed, machine->TLBMissed, machine->TLBUsed);
}

void ExceptionHandler(ExceptionType which){
    int type = machine->ReadRegister(2);

    //////////    [lab4] REMEMBER TO BREAK!!!!   //////////   
    switch (which){
        case SyscallException:{
            if (type == SC_Halt) {
                printf("\033[1;33m[Halt] Halt from %s\n\033[0m", currentThread->getName());
                printTLBMisses();
                interrupt->Halt();
            }else if (type == SC_Exit){
                printf("\033[1;33m[Exit] Exit from %s\n\033[0m", currentThread->getName());
                #ifdef USER_PROGRAM
                    // DEBUG
                    // if(type!=0) ASSERT(FALSE);

                    // free everything in current thread's address space
                    if (currentThread->space != NULL) {
                        machine->freeAllMem(); 
                        delete currentThread->space;
                        currentThread->space = NULL;
                    }
                    machine->printMem(machine->mainMemory);
                #endif
                currentThread->Finish(); // Finish current thread
            }else if (type == SC_Yield){
                printf("\033[1;33m[Yield] Yield from %s\n\033[0m", currentThread->getName());
                #ifdef USER_PROGRAM
                    machine->printTE(machine->pageTable, machine->pageTableSize);
                    machine->printMem(machine->mainMemory);
                #endif

                // increase PC
                int nextPC = machine->ReadRegister(NextPCReg);
                machine->WriteRegister(PCReg, nextPC);

                currentThread->Yield(); // Yield current thread
            }
        }
        break;
        case PageFaultException:{
            int badVAddr = machine->ReadRegister(BadVAddrReg);
            DEBUG('a', "[PF] PageFaultException at &%x\n", badVAddr);
            if (machine->tlb == NULL){
                // raised by page table -> invalid entry
                printf("[PF] Unexpected: invalid page table entry\n");
                ASSERT(FALSE);
            }else{
                // raised by tlb miss
                // machine->printTE(machine->tlb, TLBSize);
                handleTLBMiss(badVAddr);
            }
        }
        break;
        default:{
            printf("Unexpected user mode exception %d %d\n", which, type);
            ASSERT(FALSE);
        }
        break;
    }

    // if ((which == SyscallException) && (type == SC_Halt)) {
    //     DEBUG('a', "Shutdown, initiated by user program.\n");
    //     interrupt->Halt();
    // } else if (which == PageFaultException){
    //     int badvaddr = machine->ReadRegister(BadVAddrReg);
    //     DEBUG('a', "[exception] PageFaultException at %x\n", badvaddr);
    // } else {
    //     printf("Unexpected user mode exception %d %d\n", which, type);
    //     ASSERT(FALSE);
    // }
}

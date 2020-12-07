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
    // update TLB entry
    machine->tlb[TLBToReplace] = machine->pageTable[vpn];
    if (machine->tlb[TLBToReplace].virtualPage < 0 || machine->tlb[TLBToReplace].virtualPage >= NumPhysPages){
        printf("\033[1;31m[handleTLBMiss] You are trying to insert invalid item %x\n\033[0m", machine->tlb[TLBToReplace].virtualPage);
        ASSERT(FALSE);
    }
}

void printTLBMisses(){
    printf("[TLB] Miss Rate=%.2f% (%d/%d), Miss at:\n[", 
        (100*machine->TLBMissed)/(float)machine->TLBUsed, machine->TLBMissed, machine->TLBUsed);
    // for(int i = 0; i < 1024; i++){
    //     if(machine->TLBMisses[i] >= 0){
    //         printf("%d, ", machine->TLBMisses[i]);
    //     }
    //     if (i % 20 == 19) printf("\n");
    // }
    // printf("]\n");
}

void ExceptionHandler(ExceptionType which){
    int type = machine->ReadRegister(2);

    //////////    [lab4] REMEMBER TO BREAK!!!!   //////////   
    switch (which){
        case SyscallException:{
            if (type == SC_Halt) {
                printTLBMisses();
                printf("\033[1;33m[Halt] Halt from user program\n\033[0m");
                interrupt->Halt();
            }else if (type==SC_Exit){
                #ifdef USER_PROGRAM
                    printf("\033[1;33m[Exit] Exit from user program\n\033[0m");
                    // free everything in current thread's address space
                    if (currentThread->space != NULL) {
                        machine->freeAllMem(); 
                        delete currentThread->space;
                        currentThread->space = NULL;
                    }
                    machine->printMem();
                #endif
                currentThread->Finish(); // Finish current thread
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

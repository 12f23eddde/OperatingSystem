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
 # define USER_PROGRAM
 # define FILESYS_NEEDED
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


//////////    [lab4] TLBMISS & PAGEFAULT Just for now   //////////
int TLBToCheck = 0;

// [lab4] Real Page Fault
void handlePageFault(unsigned int vpn){
    DEBUG('V', "[handlePageFault] vpn=%d\n", vpn);

    if (vpn >= machine->pageTableSize){
         printf("\033[1;31m[handlePageFault] Invalid vpn=%d/%d\n", vpn, machine->pageTableSize);
    }

    // PPN might not exist; require a new page
    int newPPN = machine->allocBit();

    // if (newPPN == -1){
    //      printf("\033[1;31m[handlePageFault] Not Implemented @vpn=%d\n\033[0m", vpn);
    //      ASSERT(FALSE);
    // }

    // machine->printTE(machine->pageTable, machine->pageTableSize);

    if(newPPN >= 0) DEBUG('V', "[handlePageFault] Found available ppn=%d\n", newPPN);

    if (newPPN == -1) {  // all physical page used up,,,
        for (int i = 0; i < machine->pageTableSize; i++){
            if(i == vpn) continue;
            if(machine->pageTable[i].valid && !machine->pageTable[i].dirty){
                DEBUG('V', "[handlePageFault] Found non-dirty vpn=%d ppn=%d\n", i, machine->pageTable[i].physicalPage);
                
                // since Translate() only checks TLB, you have to manully sync it
                #ifdef USE_TLB
                for (int j = 0; j < TLBSize; j++){
                    if (machine->tlb[j].valid && (machine->tlb[j].virtualPage == i)) {
                        machine->tlb[j].valid = FALSE;		// FOUND!
                        break;
                    }
                }
                #endif

                machine->pageTable[i].valid = FALSE;  // Declared!
                newPPN = machine->pageTable[i].physicalPage;  // It's Mine!
                break;
            }
        }
    }

    if (newPPN == -1) {  // all physical pages written,,,
        for (int i = 0; i < machine->pageTableSize; i++){
            if(i == vpn) continue;
            if(machine->pageTable[i].valid && machine->pageTable[i].dirty){
                DEBUG('V', "[handlePageFault] Found dirty vpn=%d ppn=%d\n", i, machine->pageTable[i].physicalPage);
                
                // since Translate() only checks TLB, you have to manully sync it
                #ifdef USE_TLB
                for (int j = 0; j < TLBSize; j++){
                    if (machine->tlb[j].valid && (machine->tlb[j].virtualPage == i)) {
                        machine->tlb[j].valid = FALSE;		// FOUND!
                        break;
                    }
                }
                #endif
                
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
    
    // // write back replaced TLB entry
    // if (machine->tlb[TLBToReplace].valid) {
    //     int oldVPN = machine->tlb[TLBToReplace].virtualPage;
    //     DEBUG('V', "[handleTLBMiss] write back tlb[%d]", oldVPN);
    //     machine->pageTable[vpn] = machine->tlb[TLBToReplace];
    // }

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

//////////    [lab6] User syscalls   //////////

// [lab6] increase PC for syscall -> Next instr
void increasePC(){
    machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
    machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
    machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg) + 4);
}

// [lab6] read name from r4
char* getNameFromAddr(){
    int addr = machine->ReadRegister(4);
    char* name = new char[252];  // Max FileNameLen is 251
    bzero(name, sizeof(char[252]));
    int pos = 0;
    int temp = 0;
    for(; pos < 252; pos++){
        machine->ReadMem(addr + pos, 1, &temp);
        if (temp == 0) break;
        name[pos] = (char) temp;
    }
    if (pos > 252 || temp != 0){
        printf("\033[31m[getNameFromAddr] exception occured reading 0x%x\n\033[0m", addr);
        ASSERT(FALSE);
    }
    DEBUG('C',"[getNameFromAddr] %s\n", name);
    return name;
}

// [lab4] starting a userprog thread
void RunProcess(int ptr){
//    OpenFile* executable = (OpenFile*) ptr;
//    currentThread->space = new AddrSpace(executable);
//    machine->printMem(machine->mainMemory);  // debug
    DEBUG('C', "\033[1;33m[RunProcess] Starting %s\033[0m\n", currentThread->getName());
    // shall be safe to close file
//    delete executable;
    // load registers to machine
    currentThread->space->InitRegisters();
    // load page table to machine
    currentThread->space->RestoreState();
    // run!
    machine->Run();
    // Not Reached
    printf("\033[1;31m[RunProcess] Hit Buttom! \n\033[0m");
    ASSERT(FALSE);
}

// [lab6] starting forked function in current thread
void RunFunc(int funcPC){
    DEBUG('C', "\033[1;33m[RunProcess] Starting %s @PC=%d\033[0m\n", currentThread->getName(), funcPC);
    // set pc to current func
    machine->WriteRegister(PCReg, funcPC);
    machine->WriteRegister(NextPCReg, funcPC + 4);
    // save altered registers
    currentThread->SaveUserState();
    // Run!
    machine->Run();
    // Not Reached
    printf("\033[1;31m[RunFunc] Hit Buttom! \n\033[0m");
    ASSERT(FALSE);
}

// [lab6] lookup thread in threadsList
Thread* findThread(int tid){
    for(int i = 0;i < MAX_THREADS; i++){
        if(threadsList[i] && threadsList[i]->getThreadId() == tid){
            return threadsList[i];
        }
    }
    return NULL;
}

// [lab6] wait thread to finish
void waitThread(int tid){
    lockList[tid]->Acquire();
    lockList[tid]->Release();
}

void ExceptionHandler(ExceptionType which){
    int type = machine->ReadRegister(2);

    //////////    [lab4] REMEMBER TO BREAK!!!!   //////////
    // [lab6] refactor to switch :) at least looks better
    switch (which){
        case SyscallException:{
            switch (type) {
                case SC_Create:{  // void Create(char *name);
                    DEBUG('C', "\033[33m[Create] Create from %s\n\033[0m", currentThread->getName());
                    char* name = getNameFromAddr();
                    bool res = fileSystem->Create(name, 0);
                    if(res){
                        printf("[Create] Successfully created %s\n", name);
                    }else{
                        printf("[Create] Failed to create %s\n", name);
                    }
                    delete name;
                    increasePC();  // -> next instr
                }
                break;
                case SC_Open:{  // OpenFileId Open(char *name);
                    DEBUG('C', "\033[33m[Open] Open from %s\n\033[0m", currentThread->getName());
                    // [lab6] to make life simpler, just return ptr
                    char* name = getNameFromAddr();
                    OpenFile* file = fileSystem->Open(name);
                    if(file) printf("[Open] Successfully open %s @0x%x\n", name, file);
                    else printf("[Open] Failed to open %s\n", name);
                    // submit result ro r2
                    machine->WriteRegister(2, (int)file);
                    delete name;
                    increasePC();  // -> next instr
                }
                break;
                case SC_Close:{  // void Close(OpenFileId id);
                    DEBUG('C', "\033[33m[Close] Close from %s\n\033[0m", currentThread->getName());
                    int addr = machine->ReadRegister(4);  // 1st arg
                    OpenFile* file = (OpenFile*) addr;
                    ASSERT(file);  // no one wants to encounter nullptr here
                    delete file;
                    increasePC();  // -> next instr
                }
                break;
                case SC_Read:{  // int Read(char *buffer, int size, OpenFileId id);
                    DEBUG('C', "\033[33m[Read] Read from %s\n\033[0m", currentThread->getName());
                    int into = machine->ReadRegister(4);  // into is not real mem addr
                    int size = machine->ReadRegister(5);
                    int addr = machine->ReadRegister(6);
                    OpenFile* file = (OpenFile*) addr;

                    // in case of nullptr or too big size
                    if(!file || size < 0 || size > 1024){
                        machine->WriteRegister(2, size);
                        increasePC();
                        break;
                    }

                    // read to buffer
                    char* buffer = new char[size];
                    int res = file->Read(buffer, size);
                    if(res == size) {
                        printf("[Read] Successfully read %d bytes\n", size);
                        DEBUG('C', "[Read] %s\n", buffer);
                    }
                    else printf("[Write] Failed to read (%d/%d) bytes\n", res, size);

                    // flush buffer to user program
                    for(int pos = 0; pos < size; pos++){
                        machine->WriteMem(into + pos, 1, buffer[pos]);
                    }

                    // submit result ro r2
                    machine->WriteRegister(2, size);
                    delete[] buffer;
                    increasePC();  // -> next instr
                }
                break;
                case SC_Write:{ // void Write(char *buffer, int size, OpenFileId id);
                    DEBUG('C', "\033[33m[Write] Write from %s\n\033[0m", currentThread->getName());
                    int from = machine->ReadRegister(4);  // from is not real mem addr
                    int size = machine->ReadRegister(5);
                    int addr = machine->ReadRegister(6);
                    OpenFile* file = (OpenFile*) addr;

                    // in case of nullptr or too big size
                    if(!file || size < 0 || size > 1024){
                        machine->WriteRegister(2, size);
                        increasePC();
                        break;
                    }

                    // read buffer from user program
                    char* buffer = new char[size];
                    for(int pos = 0; pos < size; pos++){
                        machine->ReadMem(from + pos, 1, (int*)&buffer[pos]);
                    }

                    DEBUG('C', "[Write] %s\n", buffer);

                    // write from buffer
                    file->Write(buffer, size);
                    printf("[Write] Written %d bytes\n", size);

                    delete[] buffer;
                    increasePC();  // -> next instr
                }
                break;
                case SC_Halt:{
                    DEBUG('C', "\033[33m[Halt] Halt from %s\n\033[0m", currentThread->getName());
                    printTLBMisses();
                    interrupt->Halt();
                }
                break;
                case SC_Exit:{  // void Exit(int status);
//                    DEBUG('C', "\033[33m[Exit] Exit from %s\n\033[0m", currentThread->getName());
                    #ifdef USER_PROGRAM
                        // free everything in current thread's address space
//                        if (currentThread->space != NULL) {
//                            machine->freeAllMem();
//                            delete currentThread->space;
//                            currentThread->space = NULL;
//                        }
//                        machine->printMem(machine->mainMemory);
                    #endif
                    int exitcode = machine->ReadRegister(4);
                    printf("\033[33m[Exit] Exit %d from %s\n\033[0m", exitcode, currentThread->getName());
                    // [join] keep exit status
                    exitStatus[currentThread->getThreadId()] = exitcode;
                    // [join] release lock on exit
                    lockList[currentThread->getThreadId()]->Release();
                    currentThread->Finish(); // Finish current thread
                }
                break;
                case SC_Yield:{  // void Yield();
                    DEBUG('C', "\033[33m[Yield] Yield from %s\n\033[0m", currentThread->getName());
                    increasePC();  // -> next instr
                    currentThread->Yield(); // Yield current thread
                }
                break;
                case SC_Exec:{  // SpaceId Exec(char *name);  just return tid here
                    DEBUG('C', "\033[33m[Exec] Exec from %s\n\033[0m", currentThread->getName());
                    char* filename = getNameFromAddr();

                    OpenFile *executable = fileSystem->Open(filename);
                    if (executable == NULL) {
                        printf("Unable to open file %s\n", filename);
                        increasePC();
                        return;
                    }

                    Thread *t = new Thread(filename);
                    t->space = new AddrSpace(executable);
                    machine->printMem(machine->mainMemory);  // debug

                    t->Fork(RunProcess, (void*)executable);

                    delete executable;
                    // delete filename;  // causes wrong thread name
                    increasePC();

                    // submit result ro r2
                    DEBUG('C', "\033[33m[Exec] forked thread name=%s tid=%d\n\033[0m", t->getName(), t->getThreadId());
                    machine->WriteRegister(2, t->getThreadId());

                    // [join] Acquire lock on exec
                    lockList[t->getThreadId()]->Acquire();
                }
                break;
                case SC_Fork:{  // void Fork(void (*func)());
                    DEBUG('C', "\033[33m[Fork] Fork from %s\n\033[0m", currentThread->getName());
                    // [NOTE!] this PC is not a real mem addr
                    int funcPC = machine->ReadRegister(4);

                    char *name = new char[20];
                    strcpy(name, currentThread->getName());
                    strcat(name, "-fork");

                    Thread *t = new Thread(name);
                    // forked func share same addrSpace with currentThread
                    t->space = currentThread->space;
                    t->Fork(RunFunc, (void*)funcPC);
                    increasePC();
                }
                break;
                case SC_Join:{  // int Join(SpaceId id);
                    DEBUG('C', "\033[33m[Join] Join from %s\n\033[0m", currentThread->getName());
                    int tid = machine->ReadRegister(4);
                    // wait for thread to finish by polling
//                    while(findThread(tid)){
//                        DEBUG('C', "\033[33m[Join] %s: %d still running @%d\n\033[0m",
//                                currentThread->getName(), tid, stats->totalTicks);
//                        currentThread->Yield();
//                    }
                    waitThread(tid);
                    increasePC();
                    // submit exit status ro r2
                    machine->WriteRegister(2, exitStatus[tid]);
                    DEBUG('C', "\033[33m[Join] Thread %d exited: %d\n\033[0m", tid, exitStatus[tid]);
                }
                break;
                default:{
                    printf("\033[1;31m[ExceptionHandler] Unexpected Syscall %d\n\033[0m", type);
                    ASSERT(FALSE);
                }
            }

//            if (type == SC_Halt) {
//                printf("\033[1;33m[Halt] Halt from %s\n\033[0m", currentThread->getName());
//                printTLBMisses();
//                interrupt->Halt();
//            }else if (type == SC_Exit){
//                printf("\033[1;33m[Exit] Exit from %s\n\033[0m", currentThread->getName());
//                #ifdef USER_PROGRAM
//
//                    // free everything in current thread's address space
//                    if (currentThread->space != NULL) {
//                        machine->freeAllMem();
//                        delete currentThread->space;
//                        currentThread->space = NULL;
//                    }
//                    machine->printMem(machine->mainMemory);
//                #endif
//                currentThread->Finish(); // Finish current thread
//            }else if (type == SC_Yield){
//                printf("\033[1;33m[Yield] Yield from %s\n\033[0m", currentThread->getName());
//                #ifdef USER_PROGRAM
//                    machine->printTE(machine->pageTable, machine->pageTableSize);
//                    machine->printMem(machine->mainMemory);
//                #endif
//
//                // increase PC
//                int nextPC = machine->ReadRegister(NextPCReg);
//                machine->WriteRegister(PCReg, nextPC);
//
//                currentThread->Yield(); // Yield current thread
//            }
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

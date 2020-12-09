// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

////// enable code hint  //////
////// comment when testing //////
// #define USE_VM
// #define FILESYS_NEEDED
// #define USER_PROGRAM
// #define MULTITHREAD_SUPPORT
// # define REV_PAGETABLE

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#ifdef HOST_SPARC
#include <strings.h>
#endif

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void 
SwapHeader (NoffHeader *noffH)
{
	noffH->noffMagic = WordToHost(noffH->noffMagic);
	noffH->code.size = WordToHost(noffH->code.size);
	noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
	noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
	noffH->initData.size = WordToHost(noffH->initData.size);
	noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
	noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
	noffH->uninitData.size = WordToHost(noffH->uninitData.size);
	noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
	noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable, int count=1)
{
    NoffHeader noffH;
    unsigned int i, size;

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) && 
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
    	SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

// how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size 
			+ UserStackSize;	// we need to increase the size
						// to leave room for the stack
   
    # ifndef REV_PAGETABLE
        numPages = divRoundUp(size, PageSize);
    # else
        numPages = NumPhysPages;
    # endif
    size = numPages * PageSize;

    // check size of exectable
    // EVEN WITH VM: buffer is the exact same size as main memory
    ASSERT(numPages <= NumPhysPages);

    printf("\033[1;33m[AddrSpace] Allocating %d pages to user program\n\033[0m", numPages);

    DEBUG('a', "Initializing address space, num pages %d, size %d\n", 
					numPages, size);

    // Move Here
    // zero out the entire address space, to zero the unitialized data segment 
    // and the stack segment
    // bzero(machine->mainMemory, size);

    pageTable = new TranslationEntry[numPages];

# ifndef USE_VM
// [lab4] [NOT USING VM]
// first, set up the translation 
    for (i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;	// for now, virtual page # = phys page #
        pageTable[i].physicalPage = machine->allocBit();  // [lab4] point to a Physical Page
        pageTable[i].valid = TRUE;
        pageTable[i].use = FALSE;
        pageTable[i].dirty = FALSE;
        pageTable[i].readOnly = FALSE;  // if the code segment was entirely on 
					// a separate page, we could set its 
					// pages to be read-only
        # ifdef REV_PAGETABLE
            pageTable[i].tid = -1;
        # endif
        
        // [lab4] Check for errors
        if (pageTable[i].physicalPage < 0 || pageTable[i].physicalPage >= NumPhysPages){
            DEBUG('M', "\033[1;31m[AddrSpace] init pageTable[%d] with invalid PhyPage %d\n\033[0m", i, pageTable[i].physicalPage);
            pageTable[i].valid = FALSE;
        }
    }
    // [lab4] DEBUG
    machine->printTE(pageTable, numPages);

// then, copy in the code and data segments into memory

# ifdef MULTITHREAD_SUPPORT
    writeSegmentToMem(&(noffH.code), executable);
    writeSegmentToMem(&(noffH.initData), executable);
# else
    if (noffH.code.size > 0) {
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n", 
			noffH.code.virtualAddr, noffH.code.size);
        executable->ReadAt(&(machine->mainMemory[noffH.code.virtualAddr]),
			noffH.code.size, noffH.code.inFileAddr);
    }
    if (noffH.initData.size > 0) {
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n", 
			noffH.initData.virtualAddr, noffH.initData.size);
        executable->ReadAt(&(machine->mainMemory[noffH.initData.virtualAddr]),
			noffH.initData.size, noffH.initData.inFileAddr);
    }
#endif

#else
// [lab4] USING VM
    // get vm file ready
    vmname = new char[10];
    vmname[0] = (char)(count + '0');
    strcat(vmname, "-vm.bin");

    // remove file if exists
    fileSystem->Remove(vmname);
    // try to create file (size=AddrSpace)
    if(!fileSystem->Create(vmname, MemorySize)){
        printf("\033[1;31m[AddrSpace] VM: Failed to create %s: check permissions and your code\n\033[0m", vmname);
        ASSERT(FALSE);
    }
    
    // open vm file
    // OpenFile* vm;
    if(!(vm = fileSystem->Open(vmname))){
        printf("\033[1;31m[AddrSpace] VM: Failed to open %s: check permissions and your code\n\033[0m", vmname);
        ASSERT(FALSE);
    }

    DEBUG('V', "[AddrSpace] VM: Created vm file %s\n", vmname);
    
    // init page table
    // !!! valid -> in mem !!!
    // !!! invalid -> in vm !!!
    for (i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;
        pageTable[i].physicalPage = -1;  // [lab4] point to a invalid page
        pageTable[i].valid = FALSE;  // [lab4] not valid
        pageTable[i].use = FALSE;
        pageTable[i].dirty = FALSE;
        pageTable[i].readOnly = FALSE;  // if the code segment was entirely on 
					// a separate page, we could set its 
					// pages to be read-only
    }

    // [lab4] Make vm file a new AddrSpace
    char *buffer = new char[MemorySize];
    bzero (buffer, MemorySize);

    if (noffH.code.size > 0) {
        DEBUG('V', "Initializing code segment, at 0x%x, size %d\n", 
			noffH.code.virtualAddr, noffH.code.size);
        executable->ReadAt(&(buffer[noffH.code.virtualAddr]),
			noffH.code.size, noffH.code.inFileAddr);
    }

    if (noffH.initData.size > 0) {
        DEBUG('V', "Initializing data segment, at 0x%x, size %d\n", 
			noffH.initData.virtualAddr, noffH.initData.size);
        executable->ReadAt(&(buffer[noffH.initData.virtualAddr]),
			noffH.initData.size, noffH.initData.inFileAddr);
    }

    // [DEBUG]
    // machine->printMem(buffer);
    machine->printTE(pageTable, numPages);

    // dump temp mem -> vm
    vm->WriteAt(buffer, MemorySize, 0);    

    // delete vm;

#endif

}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
    delete vm;
    delete pageTable;
}

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);	

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

   // Set the stack register to the end of the address space, where we
   // allocated the stack; but subtract off a bit, to make sure we don't
   // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG('a', "Initializing stack register to %d\n", numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState() 
{
    // [lab4] if use tlb, then make tlb invalid
# ifdef USE_TLB
    DEBUG('T', "TLB -> invalid");
    for (int i = 0; i < TLBSize; i++){
        machine->tlb[i].valid = FALSE;
    }
# endif
}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() 
{
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
}

// [lab4] MyWriteMem
ExceptionType AddrSpace::writeWordToMem(int virtAddr, unsigned int data){
    unsigned int vpn = (unsigned) virtAddr / PageSize;
    unsigned int offset = (unsigned) virtAddr % PageSize;
    unsigned int pageFrame = pageTable[vpn].physicalPage;
    int physAddr = pageFrame * PageSize + offset;
    DEBUG('w', "[writeWordToMem] %x -> vaddr=%x, phyaddr=%x\n", data, virtAddr, physAddr);
    // Your word to memory
    *(unsigned int *) &machine->mainMemory[physAddr] = data;
    return NoException;
}

// [lab4] Write segment -> mainMemory (REVERSE ENDIAN)
ExceptionType AddrSpace::writeSegmentToMem(Segment* data, OpenFile* executable){
    DEBUG('w', "Initializing segment, at 0x%x, size %d\n", data->virtualAddr, data->size);
    unsigned char *buffer = new unsigned char[5];

    for(int curr = 0; curr < data->size; curr += 4){
        // read to buffer
        executable->ReadAt(buffer, 4, data->inFileAddr + curr);
        DEBUG('w', "[writeSegmentToMem] %x%x%x%x\n", buffer[3], buffer[2], buffer[1], buffer[0]);
        // write data to buffer (REVERSE ENDIAN)
        unsigned int yourdata = ((unsigned int)(buffer[3])) << 24;
        yourdata |= ((unsigned int)(buffer[2])) << 16;
        yourdata |= ((unsigned int)(buffer[1])) << 8;
        yourdata |= (unsigned int)buffer[0];
        writeWordToMem(data->virtualAddr + curr, yourdata);
    }
}

ExceptionType AddrSpace::loadPageFromVM(int vpn){

    // OpenFile *vm = fileSystem->Open(vmname);

    // ASSERT(vm != NULL);

    int pageFrame = pageTable[vpn].physicalPage;
    int physAddr = pageFrame * PageSize;
    int position = vpn * PageSize;

    DEBUG('V',"[loadPageFromVM] Loading vpn=%d ppn=%d from vm\n", vpn, pageFrame);

    // avoid ReadAt from crashing memory
    if (physAddr < 0 || physAddr > MemorySize - PageSize){
        printf("\033[1;31m[loadPageFromVM] invalid physAddr %x\n\033[0m", physAddr);
        return AddressErrorException;
    }

    // No need to check for size-position, ReadFile Already handled this
    int retVal=vm->ReadAt(&(machine->mainMemory[physAddr]), PageSize, position);

    DEBUG('V', "[loadPageFromVM] vmname=%s position %d/%d (%d) \n", vmname, position, vm->Length(), retVal);

    // mark it valid (machine->pageTable = pageTable)
    // pageTable[vpn].valid = TRUE;

    // delete vm;

    return NoException;
}

ExceptionType AddrSpace::dumpPageToVM(int vpn){
    if (!pageTable[vpn].valid) DEBUG('V',"[dumpPageToVM] You are trying to dump invalid vpage %d\n", vpn);

    // OpenFile *vm = fileSystem->Open(vmname);

    // ASSERT(vm != NULL);

    unsigned int pageFrame = pageTable[vpn].physicalPage;
    int physAddr = pageFrame * PageSize;
    int position = vpn * PageSize;

    // avoid ReadAt from crashing memory
    if (physAddr < 0 || physAddr > MemorySize - PageSize){
        printf("\033[1;31m[loadPageFromVM] invalid physAddr %x\n\033[0m", physAddr);
        return AddressErrorException;
    }

    // No need to check for size-position, WriteFile Already handled this
    vm->WriteAt(&(machine->mainMemory[physAddr]), PageSize, position);

    // mark it valid (machine->pageTable = pageTable)
    // pageTable[vpn].valid = FALSE;

    // delete vm;

    return NoException;
}
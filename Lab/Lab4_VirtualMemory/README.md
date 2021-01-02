任务完成情况

| Exercise 1 | Exercise 2 | Exercise 3 | Exercise 4 | Exercise 5 | Exercise 6 | Exercise 7 | Challenge 2 |
| ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ----------- |
| Y          | Y          | Y          | Y          | Y          | Y          | Y          | Y           |

#### **Exercise 1  源代码阅读**

##### **1.1**  code/userprog/progtest.cc

​	*着重理解nachos执行用户程序的过程，以及该过程中与内存管理相关的要点*	

- StartProcess

  StartProcess函数实现了NachOS中加载并执行用户程序的功能。
  
  ```cpp
  void StartProcess(char *filename){
    OpenFile *executable = fileSystem->Open(filename);
    if (executable == NULL) {
      printf("Unable to open file %s\n", filename);
      return;
  	}
  ```

  在开始执行程序前，NachOS需要首先从文件系统里加载程序。StartProcess从参数中获取文件名，然后调用`fileSystem->Open`，打开文件，返回一个OpenFile型的指针。如果指针为空，则说明打开文件失败，函数退出。
  
  ```cpp
  		AddrSpace *space;
      space = new AddrSpace(executable);    
      currentThread->space = space;
  
      delete executable;			// close file
  ```
  
  随后，StartProcess创建AddrSpace对象为进程分配地址空间。
  
  AddrSpace类定义在/userprog/addrspace.cc，完成以下工作：
  
  1. 初始化页表
  
2. 将地址空间对应的内存清零
  
  3. 将程序文件中的code segment，data segment复制到内存中
  
  地址空间分配完成后，关闭程序文件。
  
  ```cpp
      space->InitRegisters();		// set the initial register values
      space->RestoreState();		// load page table register
  
      machine->Run();			// jump to the user progam
      ASSERT(FALSE);			// machine->Run never returns;
  					// the address space exits
  					// by doing the syscall "exit"
  }
  ```
  
  InitRegisters初始化寄存器：
  
  1. 将通用寄存器设为0
  2. 设置PC与NextPC
  3. 将栈寄存器指向地址空间尾部（实际上减了16，防止意外情况）
  
  RestoreState将machine的页表置为当前地址空间的页表。
  
  最后，`machine->Run()`将控制流切换到用户进程（StartProcess不返回）

- ConsoleTest

  ConsoleTest函数用于控制台测试。（略过）

##### **1.2**  code/machine/machine.h(cc)

​	*理解当前Nachos系统所采用的TLB机制和地址转换机制*

- Machine

  在执行用户程序时，我们需要提供一个虚拟的执行环境，NachOS选择了模拟了一台MIPS计算机(R2/3000)。mipssim.h(cc)中模拟了一个MIPS处理器，而machine.h中的Machine类则包装了CPU寄存器，内存空间，页表等，构成了一台相对完整的计算机。

  ```cpp
  class Machine {
    public:
      Machine(bool debug);	// Initialize the simulation of the hardware for running user programs
      ~Machine();			// De-allocate the data structures
  // Routines callable by the Nachos kernel
      void Run();	 		// Run a user program
      int ReadRegister(int num);	// read the contents of a CPU register
      void WriteRegister(int num, int value);  // store a value into a CPU register
  // Routines internal to the machine simulation -- DO NOT call these 
      void OneInstruction(Instruction *instr); // Run one instruction of a user program.
      void DelayedLoad(int nextReg, int nextVal);  // Do a pending delayed load (modifying a reg)  
      bool ReadMem(int addr, int size, int* value);
      bool WriteMem(int addr, int size, int value); // Read or write 1, 2, or 4 bytes of virtual 
                                                    // memory (at addr).  Return FALSE if a 
  				                                  // correct translation couldn't be found. 
      ExceptionType Translate(int virtAddr, int* physAddr, int size,bool writing);
          // Translate an address, and check for alignment.  
  		    // Set the use and dirty bits in the translation entry appropriately,
      		// and return an exception code if the translation couldn't be completed.
      void RaiseException(ExceptionType which, int badVAddr);
  				// Trap to the Nachos kernel, because of a system call or other exception.  
      void Debugger();		// invoke the user program debugger
      void DumpState();		// print the user CPU and memory state 
  // Data structures -- all of these are accessible to Nachos kernel code.
      char *mainMemory;		// physical memory to store user program,
  				// code and data, while executing
      int registers[NumTotalRegs]; // CPU registers, for executing user programs
      TranslationEntry *tlb;		// this pointer should be considered "read-only" to Nachos kernel code
      TranslationEntry *pageTable;
      unsigned int pageTableSize;
    private:
      bool singleStep;		// drop back into the debugger after each simulated instruction
      int runUntilTime;		// drop back into the debugger when simulated time reaches this value
  };
  ```

  我们先关注Machine类含有的变量：

  **mainMemory** 指向该进程用户内存空间的指针（无论是页表还是TLB都不存放在mainMemory中）

  **registers** 指向寄存器数组的指针

  **tlb** 指向TLB的指针（NachOS中TLB的实现类似页表）

  **pageTable** 指向页表的指针

  **pageTableSize** 页表的大小

  如果tlb指针为空，NachOS会使用一个简单的线性页表（未修改时就是这样的情况）；如果tlb指针不为空，kernel就需要管理TLB中的内容。我们需要注意到，由于事实上的计算机只存在一个TLB，因此内核代码理应不能改动用户进程的TLB数据。

  singleStep  是否开启单步调试

  runUntilTime  定时进入Debug

  - Machine

    ```cpp
    Machine::Machine(bool debug){
        int i;
        for (i = 0; i < NumTotalRegs; i++)
            registers[i] = 0;
        mainMemory = new char[MemorySize];
        for (i = 0; i < MemorySize; i++)
          	mainMemory[i] = 0;
    #ifdef USE_TLB
        tlb = new TranslationEntry[TLBSize];
        for (i = 0; i < TLBSize; i++)
    			tlb[i].valid = FALSE;
        pageTable = NULL;
    #else	// use linear page table
        tlb = NULL;
        pageTable = NULL;
    #endif
      singleStep = debug;
        CheckEndian();
  }
    ```

    当我们初始化一个Machine实例的时候，首先会将寄存机和内存空间置0。如果使用TLB，构造函数会为其分配空间；如果使用线性页表，则tlb和pageTable都为空。如果参数debug为true，处理器则会被设置为单步模式。最后，构造函数会检查当前机器的大小端。

  - ~Machine

    ```cpp
    Machine::~Machine(){
        delete [] mainMemory;
      if (tlb != NULL)
            delete [] tlb;
  }
    ```

    当machine生命周期结束时，会释放mainMemory和tlb的内存空间。

  - Run

    Run函数的定义在code/machine/mipssim.cc中。
    
    ```cpp
    void Machine::Run(){
        Instruction *instr = new Instruction;  // storage for decoded instruction
    
        if(DebugIsEnabled('m'))
            printf("Starting thread \"%s\" at time %d\n",
    	       currentThread->getName(), stats->totalTicks);
        interrupt->setStatus(UserMode);
        for (;;) {
            OneInstruction(instr);
            interrupt->OneTick();
            if (singleStep && (runUntilTime <= stats->totalTicks))
              Debugger();
      }
    }
    ```
    
    当运行用户程序时，Run会：
    
    1. 创建用于存放指令的内存空间
    2. 将中断模式切到用户态
    3. 执行一条指令
    4. 时间流动
    5. 如果满足进入Debugger的条件，则进入Debugger
    6. 3-5循环执行
    
  - OneInstruction

    OneInstruction函数含有大量与MIPS指令相关的代码，我们只保留本文关心的部分。

    ```cpp
    Machine::OneInstruction(Instruction *instr){
        ...
        // 取值：将寄存器的值放到raw
        if (!machine->ReadMem(registers[PCReg], 4, &raw))
            return;			// 取值失败，可能是指令执行完毕?
        instr->value = raw;
        instr->Decode();
        ...
        // 计算下一个pc的值，为了处理触发异常的情况，等指令执行完成再赋值
        // MIPS指令定长，这里固定+4
        int pcAfter = registers[NextPCReg] + 4;
        ...
      // 更新PC
        registers[PrevPCReg] = registers[PCReg];	
      registers[PCReg] = registers[NextPCReg];
        registers[NextPCReg] = pcAfter;
    }
    ```

    OneInstruction函数模拟了一个单周期MIPS处理器执行一条指令的过程。Decode对指令进行解码，之后根据opCode对操作数进行运算，最后更新PC的值。

    如果在执行指令的过程中出现异常，OneInstruction会调用raiseException函数，随后控制流进入异常处理程序。

  - DelayedLoad

    ```cpp
    void Machine::DelayedLoad(int nextReg, int nextValue){
        registers[registers[LoadReg]] = registers[LoadValueReg];
        registers[LoadReg] = nextReg;
        registers[LoadValueReg] = nextValue;
        registers[0] = 0; 	// and always make sure R0 stays zero.
    }
    ```
    
    延迟加载一个值到寄存器（与MIPS模拟机制有关）
    
    DelayedLoad在指令运行完之后才执行（在OneInstruction中，更新PC前才会调用DelayedLoad函数）。
    
  - RaiseException

    在执行指令过程中触发一个异常，并将发生内存异常的地址badVAddr存入寄存器。

    ```cpp
    void Machine::RaiseException(ExceptionType which, int badVAddr){
        DEBUG('m', "Exception: %s\n", exceptionNames[which]);
        
    //  ASSERT(interrupt->getStatus() == UserMode);
      registers[BadVAddrReg] = badVAddr;
        DelayedLoad(0, 0);			// finish anything in progress
      interrupt->setStatus(SystemMode);
        ExceptionHandler(which);		// interrupts are enabled at this point
      interrupt->setStatus(UserMode);
    }
    ```
  
  - checkEndian
  
    检查当前宿主机的大/小端是否与编译时设置的宏开关一致。（不会吧不会吧，不会真有人0202年还在用大端机吧）
  
  - Debugger
  
    调试函数，可以在这里输出一些调试用信息。如果设置了单步模式或设置了runUntilTime，则会进入Debugger。
  
  - DumpState
  
    输出寄存器内的值
  
  - ReadRegister
  
    返回某一个寄存器内的值
  
  - WriteRegister
  
    设置某一个寄存器内的值

- Exception

  ```cpp
  enum ExceptionType { NoException,           // Everything ok!
  		     SyscallException,      // A program executed a system call.
  		     PageFaultException,    // No valid translation found
  		     ReadOnlyException,     // Write attempted to page marked "read-only"
  		     BusErrorException,     // Translation resulted in an invalid physical address
  		     AddressErrorException, // Unaligned reference or one that was beyond the end of the address space
  		     OverflowException,     // Integer overflow in add or sub.
  		     IllegalInstrException, // Unimplemented or reserved instr.
  		     NumExceptionTypes
  };
  ```

  NachOS中定义了以下几种异常（这里不区分中断与异常）：系统调用，缺页异常，写只读页，物理地址无效，虚拟地址无效，运算结果溢出，非法指令。

##### **1.3**  code/machine/translate.h(cc)

```cpp
class TranslationEntry {
  public:
    int virtualPage;  	// The page number in virtual memory.
    int physicalPage;  	// The page number in real memory (relative to the start of "mainMemory"
    bool valid;         // If this bit is set, the translation is ignored.
			                    // (In other words, the entry hasn't been initialized.)
    bool readOnly;	// If this bit is set, the user program is not allowed
			                    // to modify the contents of the page.
    bool use;       // This bit is set by the hardware every time the page is referenced or modified.
    bool dirty;     // This bit is set by the hardware every time the page is modified.
}
```

TranslationEntry类定义了一个页表项。页表项内包括以下属性：

**virtualPage** 虚拟地址空间中的页号

**physicalPage** 物理地址空间中的页号

**valid** 表项是否有效（可以将valid置为false，等效于清零操作）

**readOnly** 页是否为只读

**use** 页是否被读过或修改过

**dirty** 页是否被修改过

- WriteMem

  WriteMem(int addr, int size, int value)

  向虚拟内存地址addr写入值为value，大小为size字节的数据。

  由于NachOS模拟的MIPS 3000计算机是大端的，因此这里需要switch(size)语句根据size对数据进行大小端转换。

- ReadMem

  ReadMem(int addr, int size, int *value) 

  从虚拟内存地址addr读取大小为size字节的数据，并将其赋值给*value。

  由于NachOS模拟的MIPS 3000计算机是大端的，因此这里需要switch(size)语句根据size对数据进行大小端转换。

- Translate

  ```cpp
Machine::Translate(int virtAddr, int* physAddr, int size, bool writing){
      int i;
      unsigned int vpn, offset;
      TranslationEntry *entry;
      unsigned int pageFrame;
  
      DEBUG('a', "\tTranslate 0x%x, %s: ", virtAddr, writing ? "write" : "read");
  
      // 检查地址是否合法
    if (((size == 4) && (virtAddr & 0x3)) || ((size == 2) && (virtAddr & 0x1))){
  	DEBUG('a', "alignment problem at %d, size %d!\n", virtAddr, size);
  	return AddressErrorException;
      }
      
      // tlb和pageTable中有且仅有一个被启用
      ASSERT(tlb == NULL || pageTable == NULL);	
      ASSERT(tlb != NULL || pageTable != NULL);	
  
      // 计算VPN/OFFSET
      vpn = (unsigned) virtAddr / PageSize;
      offset = (unsigned) virtAddr % PageSize;
      
      if (tlb == NULL) {
          // 使用线性页表
          if (vpn >= pageTableSize) {
              DEBUG('a', "virtual page # %d too large for page table size %d!\n", 
                  virtAddr, pageTableSize);
              return AddressErrorException;
          } else if (!pageTable[vpn].valid) {
              DEBUG('a', "virtual page # %d too large for page table size %d!\n", 
                  virtAddr, pageTableSize);
              return PageFaultException;
          }
          entry = &pageTable[vpn];
      } else {
          // 使用TLB，在TLB中顺序查找页表项
          for (entry = NULL, i = 0; i < TLBSize; i++)
      	    if (tlb[i].valid && (tlb[i].virtualPage == vpn)) {
                  entry = &tlb[i];			// FOUND!
                  break;
  	        }
          // TLB中没找到VPN
  	    if (entry == NULL) {
      	    DEBUG('a', "*** no valid TLB entry found for this virtual page!\n");
      	    return PageFaultException;		// really, this is a TLB fault, the page may be in memory, but not in the TLB
  	    }
      }
  
      // 写只读页
      if (entry->readOnly && writing) {	// trying to write to a read-only page
          DEBUG('a', "%d mapped read-only at %d in TLB!\n", virtAddr, i);
          return ReadOnlyException;
      }
      pageFrame = entry->physicalPage;
  
      // 从页表中读出了错误的物理页号
      if (pageFrame >= NumPhysPages) { 
          DEBUG('a', "*** frame %d > %d!\n", pageFrame, NumPhysPages);
          return BusErrorException;
      }
  
      // 设置use, dirty位
      entry->use = TRUE;
      if (writing) entry->dirty = TRUE;
      *physAddr = pageFrame * PageSize + offset;
      ASSERT((*physAddr >= 0) && ((*physAddr + size) <= MemorySize));
      DEBUG('a', "phys addr = 0x%x\n", *physAddr);
      return NoException;
  }
  ```
  
  Translate函数实现了虚拟地址到物理地址的转换。其工作流程大致如下：
  
  1. 从虚拟地址中获得VPN和Offset
  2. 检查虚拟地址是否正确（若不正确，则会抛出AddressError异常）
  3. 若使用线性页表，检查页是否失效（若失效，则会抛出PageFault异常）
  4. 若使用TLB，在TLB中顺序查找VPN对应的页表项（若没有找到，则会抛出PageFault异常）
  5. 检查页是否可写（若只读，则会抛出ReadOnly异常）
  6. 检查物理页号是否正确（若不正确，则会抛出BusError异常）
  7. 修改页表中的use和dirty位
  8.  对物理地址赋值，`*physAddr = pageFrame * PageSize + offset`
  
  我们注意到，Translate函数返回的是ExceptionType类型，可以根据返回值进行错误处理。

##### **1.4**  code/userprog/exception.h(cc)

```cpp
void ExceptionHandler(ExceptionType which){
    int type = machine->ReadRegister(2);

    if ((which == SyscallException) && (type == SC_Halt)) {
        DEBUG('a', "Shutdown, initiated by user program.\n");
        interrupt->Halt();
        } else {
        printf("Unexpected user mode exception %d %d\n", which, type);
        ASSERT(FALSE);
    }
}
```

用户程序中的ExceptionHandler定义了异常处理程序。

按照MIPS架构的规定，异常号位于r2寄存器，如果异常处理程序需要参数，放置在r4-r7寄存器，返回结果放置在r2寄存器。

在异常处理程序执行完毕后，我们需要手动更新PC。（这是因为OneInstrction函数中调用RaiseException后直接return，并没有更新PC）

#### **Exercise 2 TLB MISS异常处理**

​	*修改code/userprog目录下exception.cc中的ExceptionHandler函数，使得Nachos系统可以对TLB异常进行处理（TLB异常时，Nachos系统会抛出PageFaultException，详见code/machine/machine.cc）。*

##### 实现

NachOS默认不使用TLB。我们需要在Makefile中打开`USE_TLB`宏：

```makefile
##########   [lab4] Use TLB   ##########   
DEFINES = -DUSER_PROGRAM -DFILESYS_NEEDED -DFILESYS_STUB -DUSE_TLB
```

这里需要注意，NachOS的设计者们认为pageTable和tlb不可能同时存在，然而这在当前阶段是有可能的。因此需要在/machine/machine.cc中注释一个ASSERT语句：

```cpp
//////////    [lab4] Disabled for now    //////////    
// we must have either a TLB or a page table, but not both!
// ASSERT(tlb == NULL || pageTable == NULL);	
ASSERT(tlb != NULL || pageTable != NULL);	
```

Translate函数使用`tlb == NULL`来判断当前machine使用的是线性页表还是TLB。由于NachOS将线性页表错误和TLB Miss同时看做PageFault，我们需要区分这两种情况：

```cpp
switch (which){
  case PageFaultException:{
    int badVAddr = machine->ReadRegister(BadVAddrReg);
    DEBUG('a', "[PF] PageFaultException at &%x\n", badVAddr);
    if (machine->tlb == NULL){
      // raised by page table -> invalid entry
      printf("[PF] Unexpected: invalid page table entry\n");
      ASSERT(FALSE);
    }else{
      // raised by tlb miss
      handleTLBMiss(badVAddr);
    }
  }
    break;
	...
```

在当前阶段线性页表中的页表项不会失效，因此如果线性页表项失效肯定是出现了错误。

当我们确定当前PageFaultException是由于TLB Miss导致后，将控制流切换到handleTLBMiss函数：

```cpp
//////////    [lab4] Just for now   //////////   
int TLBToReplace = 0;

// nextPC = PC in this case
void handleTLBMiss(unsigned int virtAddr){
    unsigned int vpn = (unsigned) virtAddr / PageSize;
    unsigned int offset = (unsigned) virtAddr % PageSize;

    //...

    machine->tlb[TLBToReplace] = machine->pageTable[vpn];
    //////////    [lab4] replace the other entry   //////////   
    TLBToReplace = !TLBToReplace;
}
```

当前的handleTLBMiss还是一个较为简陋的实现——我们将TLB的大小限制到2，然后每一次选择替换不同的entry。

决定了TLBToReplace后，`machine->tlb[TLBToReplace] = machine->pageTable[vpn]`将页表中的物理地址读到TLB中。

在NachOS中，如果异常处理程序不希望重新运行一遍当前指令，需要主动将PC+4。Page Fault显然不是这种情况，因此我们什么也不用做。

由于TLB为所有用户程序共用，因此上下文切换时需要让它失效：

```cpp
void AddrSpace::SaveState(){
  #ifdef USE_TLB
    for (int i = 0; i < TLBSize; i++) {
        machine->tlb[i].valid = FALSE;
    }
  #endif
}
```

除此之外，我们还需要修改Translate函数，保证页表项和TLB项的状态同步：

（一个晚上的时间让我充分明白了缓存同步的重要性）

```cpp
entry->use = TRUE;		// set the use, dirty bits
	pageTable[vpn].use = TRUE;
    if (writing){
		entry->dirty = TRUE;
		pageTable[vpn].dirty = TRUE;
}
```

##### 测试

在这里我们运行一个简单的测试程序/test/halt，以下是运行的结果：

```
Reading VA 0x0, size 4
	Translate 0x0, read: *** no valid TLB entry found for this virtual page!
Exception: page fault/no TLB entry
[PF] PageFaultException at &0
Reading VA 0x0, size 4
	Translate 0x0, read: phys addr = 0x0
	value read = 0c000034
At PC = 0x0: JAL 52
Reading VA 0x4, size 4
	Translate 0x4, read: phys addr = 0x4
	value read = 00000000
At PC = 0x4: SLL r0,r0,0
Reading VA 0xd0, size 4
	Translate 0xd0, read: *** no valid TLB entry found for this virtual page!
Exception: page fault/no TLB entry
[PF] PageFaultException at &d0
Reading VA 0xd0, size 4
	Translate 0xd0, read: phys addr = 0xd0
	value read = 27bdffe8
At PC = 0xd0: ADDIU r29,r29,-24
Reading VA 0xd4, size 4
	Translate 0xd4, read: phys addr = 0xd4
	value read = afbf0014
At PC = 0xd4: SW r31,20(r29)
Writing VA 0x4ec, size 4, value 0x8
	Translate 0x4ec, write: *** no valid TLB entry found for this virtual page!
Exception: page fault/no TLB entry
[PF] PageFaultException at &4ec
...
```

这样的结果显然与线性页表不同，说明我们的handleTLBMiss函数已经能够尝试处理TLB Miss了。

#### **Exercise 3 置换算法**

​	*为TLB机制实现至少两种置换算法，通过比较不同算法的置换次数可比较算法的优劣。*

##### 准备工作

首先我们得想个办法记录Translate的次数和TLB Miss的次数，这样才能准确地评判替换算法的性能。

我们在/machine/machine.h中增加两个变量，TLBUsed记录Translate的总次数，TLBMissed记录TLB Miss的次数。

```cpp
class Machine {
  ...
	// Metrics for TLB Replacement Performance
	int TLBUsed = 0;
	int TLBMissed = 0;
  ...
```

在Translate函数中也需要做一些修改：

```cpp
ExceptionType Machine::Translate(int virtAddr, int* physAddr, int size, bool writing)
{ ...
  if (tlb == NULL) {		// => page table => vpn is index into table
		...
    } else {    // => tlb => search vpn in the page table
			TLBUsed ++;
      ...
		if (entry == NULL) {				// not found
			TLBMissed ++;
      printf("[TLB] (%d/%d) miss @ vaddr=%x vpn=%x offset=%x\n", TLBMissed, TLBUsed, virtAddr, vpn, offset);
			...
```

这里我们增加了TLBToCheck变量用于记录时钟算法的指针。当无法找到空闲的表项时，handleTLBMiss使用替换算法在有效页表项中选出TLBToCheck作为牺牲者。

```cpp
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
```

##### **FIFO**

FIFO的思想是替换最早进入TLB的页面。然而Machine类中TLB的数据结构是数组而非链表，按照进入时间组织TLB项并不合理。但是如果观察我们寻找空TLB项的过程，就会发现在有效的TLB项中，越靠前的TLB项插入时间越早。（不考虑部分TLB项提前失效的情况）因此我们可以简单地移除第0项，把其它项向前移，把最后一个位置留给新页面。

```cpp
#ifdef USE_FIFO
// use last entry
for (int i = 0; i < TLBSize - 1; i++){
  machine->tlb[i] = machine->tlb[i+1];
}
TLBToReplace = TLBSize - 1;
#endif
```

##### **NRU时钟**

我们注意到TranslationEntry的数据结构中已经包含use位和dirty位，这与NRU算法的需求不谋而合。NRU的算法思想大致是，一个页面被再次访问的几率可以通过这个页面有无被访问过，有无被修改过来推测。页面被再次访问的概率大致符合无访问，无修改 < 无访问，有修改 < 有访问，无修改 < 有访问，有修改，因此我们就按照这个思想来找出最不可能被再次访问的页面替换。


```cpp
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
```

NRU时钟算法与随机选择的NRU不同，用于替换的TLB项不是随机选择的。NRU时钟算法从指针的当前位置遍历四遍TLB，分别尝试找到r=0、m=0，r=0、m=1，r=1、m=0，r=1、m=1的页用于替换。

我猜想这样的特点让时钟算法在存储器随机访问性能低于连续访问的场景下有一定的优势，例如虚拟存储。

##### **测试**

为了避免重复造轮子，我先尝试直接使用NachOS给出的/test/sort，然而这样会在运行时报错`Assertion failed: line 81, file "../userprog/addrspace.cc`。我又去看了看NachOS的源代码，啊，原来是佐田，NachOS默认只支持用户程序使用4KB的内存空间。

为了减少/test/sort的内存占用，我们需要对其做一些小小的修改：

```cpp
# define N 20
int A[N];	

int main(){
    int i, j, tmp;
    for (i = 0; i < N; i++)		
        A[i] = N - i;
		// ...
    Halt();
    // Exit(A[0]);		/* and then we're done -- should be 0! */
}
```

由于目前Exit系统调用也没有实现，因此我们将其修改为Halt()。

在Makefile中增加`-DUSE_FIFO`后，运行/test/sort：

```
[TLB] (560/10478) miss @ vaddr=300 vpn=6 offset=0
[TLB] (561/10489) miss @ vaddr=200 vpn=4 offset=0
[TLB] (562/10529) miss @ vaddr=280 vpn=5 offset=0
[TLB] (563/10545) miss @ vaddr=17c vpn=2 offset=7c
[TLB] (564/10547) miss @ vaddr=180 vpn=3 offset=0
[TLB] (565/10549) miss @ vaddr=758 vpn=e offset=58
[TLB] (566/10578) miss @ vaddr=304 vpn=6 offset=4
[TLB] (567/10589) miss @ vaddr=200 vpn=4 offset=0
[TLB] (568/10629) miss @ vaddr=280 vpn=5 offset=0
[TLB] (569/10645) miss @ vaddr=17c vpn=2 offset=7c
[TLB] (570/10647) miss @ vaddr=180 vpn=3 offset=0
[TLB] (571/10649) miss @ vaddr=758 vpn=e offset=58
[TLB] (572/10678) miss @ vaddr=308 vpn=6 offset=8
[TLB] (573/10689) miss @ vaddr=200 vpn=4 offset=0
[TLB] (574/10729) miss @ vaddr=280 vpn=5 offset=0
[TLB] (575/10745) miss @ vaddr=17c vpn=2 offset=7c
[TLB] (576/10747) miss @ vaddr=180 vpn=3 offset=0
[TLB] (577/10749) miss @ vaddr=758 vpn=e offset=58
[TLB] (578/10810) miss @ vaddr=304 vpn=6 offset=4
[TLB] (579/10821) miss @ vaddr=200 vpn=4 offset=0
[TLB] (580/10861) miss @ vaddr=280 vpn=5 offset=0
[TLB] (581/10877) miss @ vaddr=17c vpn=2 offset=7c
[TLB] (582/10879) miss @ vaddr=180 vpn=3 offset=0
[TLB] (583/10881) miss @ vaddr=758 vpn=e offset=58
[TLB] (584/11199) miss @ vaddr=10 vpn=0 offset=10
[TLB] Miss Rate=5.21% (584/11201)
```

在Makefile中增加`-DUSE_CLOCK`后，运行/test/sort：

```
[TLB] (380/10016) miss @ vaddr=200 vpn=4 offset=0
[TLB] (381/10056) miss @ vaddr=280 vpn=5 offset=0
[TLB] (382/10072) miss @ vaddr=17c vpn=2 offset=7c
[TLB] (383/10074) miss @ vaddr=180 vpn=3 offset=0
[TLB] (384/10094) miss @ vaddr=150 vpn=2 offset=50
[TLB] (385/10136) miss @ vaddr=300 vpn=6 offset=0
[TLB] (386/10147) miss @ vaddr=200 vpn=4 offset=0
[TLB] (387/10187) miss @ vaddr=280 vpn=5 offset=0
[TLB] (388/10203) miss @ vaddr=17c vpn=2 offset=7c
[TLB] (389/10205) miss @ vaddr=180 vpn=3 offset=0
[TLB] (390/10243) miss @ vaddr=200 vpn=4 offset=0
[TLB] (391/10283) miss @ vaddr=280 vpn=5 offset=0
[TLB] (392/10299) miss @ vaddr=17c vpn=2 offset=7c
[TLB] (393/10301) miss @ vaddr=180 vpn=3 offset=0
[TLB] (394/10339) miss @ vaddr=200 vpn=4 offset=0
[TLB] (395/10379) miss @ vaddr=280 vpn=5 offset=0
[TLB] (396/10395) miss @ vaddr=17c vpn=2 offset=7c
[TLB] (397/10397) miss @ vaddr=180 vpn=3 offset=0
[TLB] (398/10417) miss @ vaddr=150 vpn=2 offset=50
[TLB] (399/10459) miss @ vaddr=304 vpn=6 offset=4
[TLB] (400/10470) miss @ vaddr=200 vpn=4 offset=0
[TLB] (401/10510) miss @ vaddr=280 vpn=5 offset=0
[TLB] (402/10526) miss @ vaddr=17c vpn=2 offset=7c
[TLB] (403/10528) miss @ vaddr=180 vpn=3 offset=0
[TLB] (404/10548) miss @ vaddr=150 vpn=2 offset=50
[TLB] (405/10847) miss @ vaddr=10 vpn=0 offset=10
[TLB] Miss Rate=3.73% (405/10849)
```

我们可以发现NRU时钟算法的性能显著优于FIFO，也优于普通的时钟算法（其它同学实现得出的Miss Rate是3.90%，而受时间所限我并没有验证）。在NRU时钟的运行结果中可以看出，TLB反复在几个固定的内存地址处Miss；然而囿于时间有限，故无法进一步探究。

#### **Exercise 4 内存全局管理数据结构**

​	*设计并实现一个全局性的数据结构（如空闲链表、位图等）来进行内存的分配和回收，并记录当前内存的使用状态*

##### 准备工作

当分析/machine/machine.h时，我们可以发现NachOS默认的物理页数是32。

```cpp
#define NumPhysPages    32
#define MemorySize 	(NumPhysPages * PageSize)
```

随之而来的便是一个奇妙的想法——32个bit正好是一个Unsigned Int的大小，我们可以用Unsigned Int存储bitmap，然后通过位运算尽可能加快bitmap的操作，从而实现一个高效的数据结构对内存进行分配和回收。

首先，我们需要修改machine.h中的Machine类：

```cpp
class Machine {
  ...
// [lab4] bitmap with only 32bits -> unsigned int
// if you want to expand NPhyPages, it have to be altered
	void printTE(TranslationEntry *pt, int size);
	unsigned int bitmap;
	int allocBit();
	void freeBit(int n);
	void freeAllMem();
```

增加bitmap存储当前的内存分配情况。当bitmap的第$i$位为0，说明物理页的第$i$页是空闲的；若为1，说明物理页的第$i$页被占用。

```cpp
// print tlb or pageTable
void Machine::printTE(TranslationEntry *pt, int size){
    if (size == TLBSize) printf("\033[1;36m");
    else printf("\033[1;35m");
    printf("%5s%6s%6s%4s%4s%4s%4s\n", "IND","VPN","PPN","VAL","RDO","USE","DIR");
    for(int i = 0; i < size; i++){
        printf(" %3d|%6x%6x%4d%4d%4d%4d\n", i, pt[i].virtualPage, pt[i].physicalPage, 
            pt[i].valid, pt[i].readOnly, pt[i].use, pt[i].dirty);
    }
    printf("\033[0m");
}
```

为了方便调试，我们增加了printTE工具函数。这个函数可以打印出TLB/页表/虚拟内存文件内各项的值，甚至还能用不同颜色在终端上打印字符。您还可以在之后看到它。

##### 内存分配

allocBit函数从bitmap中找出为0的最低的一位，将这位置为1，并返回它的index。这代表着物理页的第index项已经被占用。

```cpp
// [lab4] alloc 1 bit in bitmap; return its its index
int Machine::allocBit(){
    // gcc builtin function __builtin_ffs: 
    // return the index of last significant bit (starts from 1)
    // Credit: https://blog.csdn.net/jasonchen_gbd/article/details/44948523
    int shift = __builtin_ffs(~bitmap) - 1;  
    if (shift < 0){
        DEBUG('M', "[allocBit] bitmap already full %d (%x)\n", shift, bitmap);
        return -1;
    }
    bitmap |= 0x1 << shift;
    DEBUG('M', "[allocBit] allocate bit %d -> %x\n", shift, bitmap);
    return shift;
}
```

为了尽可能加快这一过程，我们使用了gcc自带的__builtin_ffs函数，这个函数能找出一个无符号整数的最低有效位。如果最低有效位为0，则说明这个数原本就是0。若shift < 0，则说明所有物理页已经被占用；如果存在空闲的物理页，`bitmap |= 0x1 << shift`在bitmap中将这一位置为1。

我们还需要修改AddrSpace的构造函数：

```cpp
// first, set up the translation 
    pageTable = new TranslationEntry[numPages];
    for (i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;	// for now, virtual page # = phys page #
        pageTable[i].physicalPage = machine->allocBit();  // [lab4] point to a Physical Page
        pageTable[i].valid = TRUE;
        pageTable[i].use = FALSE;
        pageTable[i].dirty = FALSE;
        pageTable[i].readOnly = FALSE;  // if the code segment was entirely on 
					// a separate page, we could set its 
					// pages to be read-only
        
        // [lab4] Check for errors
        if (pageTable[i].physicalPage < 0 || pageTable[i].physicalPage >= NumPhysPages){
            DEBUG('M', "\033[1;31m[AddrSpace] init pageTable[%d] with invalid PhyPage %d\n\033[0m", i, pageTable[i].physicalPage);
            pageTable[i].valid = FALSE;
        }
    }

    // [lab4] DEBUG
    machine->printTE(pageTable, numPages);
```

在为函数分配虚拟内存地址空间时，我们就分配和虚拟内存地址空间一样大的物理内存。（通过allocBit实现）如果分配失败，页表项对应的页号为-1，之后的if语句会将这个页表项置为无效。

##### 内存释放

freeBit函数将bitmap的shift位置为0，对应着物理页的第shift项被释放。

```cpp
// [lab4] free 1 bit in bitmap
void Machine::freeBit(int shift){
    bitmap &= ~(0x1 << shift);
    DEBUG('M', "[allocBit] free bit %d -> %x\n", shift, bitmap);
}
```

freeAllMem函数对应着用户程序让出CPU时的情况，会将整个页表中所有的项置为无效，并清空bitmap。

```cpp
// [lab4] free all memory in page table
void Machine::freeAllMem(){
    DEBUG('M', "[freeAllMem] freeing all entries in pageTable for current Thread\n");
    for(int i = 0; i < pageTableSize; i++){
        if(pageTable[i].valid){
            int pageToFree = pageTable[i].physicalPage;
            freeBit(pageToFree);  // set bitmap
            pageTable[i].valid = FALSE;  // set valid
        }
    }
    printTE(pageTable, pageTableSize);
}
```

从NachOS的代码中可以看出，在`machine->Run()`进入用户程序后，用户程序并不会返回。因此，我们需要在用户程序退出时手动回收内存。

这里选择修改ExceptionHandler，在用户程序调用Exit()时进行内存回收。

```cpp
void ExceptionHandler(ExceptionType which){
  	...
    }else if (type==SC_Exit){
      #ifdef USER_PROGRAM
      printf("\033[1;33m[Exit] Exit from user program\n\033[0m");
      // free everything in current thread's address space
      if (currentThread->space != NULL) {
        machine->freeAllMem(); 
        delete currentThread->space;
        currentThread->space = NULL;
      }
      #endif
      currentThread->Finish(); // Finish current thread
    }
```

##### 测试

我们还是用老朋友sort来测试我们的代码。为了不让过多的Miss调试信息污染我们的终端，我们在这里将N改为5，并将最后的Halt改为Exit来验证内存回收是否正常。

```cpp
[allocBit] allocate bit 0 -> 1
[allocBit] allocate bit 1 -> 3
[allocBit] allocate bit 2 -> 7
...
[allocBit] allocate bit 12 -> 1fff
[allocBit] allocate bit 13 -> 3fff
[allocBit] allocate bit 14 -> 7fff
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     0   1   0   0   0
   1|     1     1   1   0   0   0
   2|     2     2   1   0   0   0
   3|     3     3   1   0   0   0
   4|     4     4   1   0   0   0
   5|     5     5   1   0   0   0
   6|     6     6   1   0   0   0
   7|     7     7   1   0   0   0
   8|     8     8   1   0   0   0
   9|     9     9   1   0   0   0
  10|     a     a   1   0   0   0
  11|     b     b   1   0   0   0
  12|     c     c   1   0   0   0
  13|     d     d   1   0   0   0
  14|     e     e   1   0   0   0

```

可以看到这里的物理页被正常地分配。（之所以虚拟页号和物理页号还相等，是因为___builtin_ffs最低有效位的机制使靠前的页被优先取出）

```
[TLB] (1/1) miss @ vaddr=0 vpn=0 offset=0
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     0   0   0   0   0
   1|     0     0   0   0   0   0
   2|     0     0   0   0   0   0
   3|     0     0   0   0   0   0
[TLB] (2/4) miss @ vaddr=d0 vpn=1 offset=50
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     0   1   0   1   0
   1|     0     0   0   0   0   0
   2|     0     0   0   0   0   0
   3|     0     0   0   0   0   0
[TLB] (3/7) miss @ vaddr=76c vpn=e offset=6c
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     0   1   0   1   0
   1|     1     1   1   0   1   0
   2|     0     0   0   0   0   0
   3|     0     0   0   0   0   0
[TLB] (4/24) miss @ vaddr=104 vpn=2 offset=4
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     0   1   0   1   0
   1|     1     1   1   0   1   0
   2|     e     e   1   0   1   1
   3|     0     0   0   0   0   0
[TLB] (5/39) miss @ vaddr=2f0 vpn=5 offset=70
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     0   1   0   1   0
   1|     1     1   1   0   1   0
   2|     e     e   1   0   1   1
   3|     2     2   1   0   1   0
...
```

当出现TLB Miss时，TLB会从页表中加载页。

```
[Exit] Exit from user program
[freeAllMem] freeing all entries in pageTable for current Thread
[allocBit] free bit 0 -> 7ffe
[allocBit] free bit 1 -> 7ffc
...
[allocBit] free bit 13 -> 4000
[allocBit] free bit 14 -> 0
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     0   0   0   0   0
   1|     1     1   0   0   0   0
   2|     2     2   0   0   0   0
   3|     3     3   0   0   0   0
   4|     4     4   0   0   0   0
   5|     5     5   0   0   0   0
   6|     6     6   0   0   0   0
   7|     7     7   0   0   0   0
   8|     8     8   0   0   0   0
   9|     9     9   0   0   0   0
  10|     a     a   0   0   0   0
  11|     b     b   0   0   0   0
  12|     c     c   0   0   0   0
  13|     d     d   0   0   0   0
  14|     e     e   0   0   0   0
```

当从用户程序退出时，所有的页表项都被正常的释放。

#### **Exercise 5 多线程支持**

​	*目前Nachos系统的内存中同时只能存在一个线程，我们希望打破这种限制，使得Nachos系统支持多个线程同时存在于内存中。*

##### 实现

AddrSpace调用构造函数时会清空所有用户程序的内存，这显然不合理。因此我们把bzero一行注释了。

```cpp
 AddrSpace::AddrSpace(){
   ...
    // zero out the entire address space, to zero the unitialized data segment 
    // and the stack segment
    // bzero(machine->mainMemory, size);
```

NachOS原先将用户程序拷贝到物理内存的机制十分简朴——直接把程序文件塞到物理内存中对应的地址，也就是只适用于线性页表的情况。这里我们还考虑到代码段和数据段的起始地址很可能不与段对齐，因此没有直接复用处理页失效的代码。

```cpp
 AddrSpace::AddrSpace(){
   ...
// then, copy in the code and data segments into memory
# ifdef MULTITHREAD_SUPPORT
    // [lab4] init Addrspace with real vaddr
    writeSegmentToMem(&(noffH.code), executable);
    writeSegmentToMem(&(noffH.initData), executable);
# else
    if (noffH.code.size > 0) {
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n", 
			noffH.code.virtualAddr, noffH.code.size);
        executable->ReadAt(&(machine->mainMemory[noffH.code.virtualAddr]),
			noffH.code.size, noffH.code.inFileAddr);
    }
    ...
#endif
```

考虑到MIPS指令集中代码段长度一定是4的倍数，因此我们可以每次调用Translate函数，以Word的单位将代码段和数据段正确地写入内存。writeWordToMem本质是一个简化版的Translate+writeMem。

```cpp
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
```

writeSegmentToMem函数将用户程序的代码段和数据段拆分，每次调用ReadAt读入buffer，并调用writeWordToMem写入物理内存。

这里我们需要注意NachOS模拟的MIPS是大端的，因此还需要对Word进行大小端转换。

```cpp
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
```

##### 测试

为了使NachOS一次性创建多个用户程序线程，我们需要对StartProcess函数做一些修改：

```cpp
# define numInstances 2
// [lab4] Modified to run multiple instances
void StartProcess(char *filename){
    OpenFile *executable = fileSystem->Open(filename);
    AddrSpace *space;

    if (executable == NULL) {
	    printf("Unable to open file %s\n", filename);
	    return;
    }
    Thread *t [10];
    for(int i = 1; i <= numInstances; i++){
        t[i-1] = new Thread("userProg",i);
        t[i-1]->space = new AddrSpace(executable);
        machine->printMem();
        t[i-1]->Fork(RunSingleProcess, (void*)i);
    }

    // Manually yield the thread
    currentThread->Yield();
}
```

这里需要注意，在没有开启抢占的情况下main不会主动下CPU，因此需要在StartProcess中调用Yield。

这里我们创建了两个用户进程sort：

```
[AddrSpace] Allocating 15 pages to user program
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     0   1   0   0   0
   1|     1     1   1   0   0   0
   2|     2     2   1   0   0   0
   3|     3     3   1   0   0   0
   4|     4     4   1   0   0   0
   5|     5     5   1   0   0   0
   6|     6     6   1   0   0   0
   7|     7     7   1   0   0   0
   8|     8     8   1   0   0   0
   9|     9     9   1   0   0   0
  10|     a     a   1   0   0   0
  11|     b     b   1   0   0   0
  12|     c     c   1   0   0   0
  13|     d     d   1   0   0   0
  14|     e     e   1   0   0   0
Page0 | 34 0 0 c 0 0 0 0 8 0 0 c2120 0 0 0 0 224 c 0 0 0 8 0e0 3 0 0 0 0 1 0 224 c 0 0 0 8 0e0 3 0 0 0 0 2 0 224 c 0 0 0 8 0e0 3 0 0 0 0
...
Page5 |  0 0 33cf0 26324211062 018 0c38f 0 0 0 0 0 043ac14 0c28f 0 0 0 0 1 043245f 0 0 814 0c3af10 0c28f 0 0 0 0 1 0432454 0 0 810 0c3af
...
Page31|  0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
[AddrSpace] Allocating 15 pages to user program
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     f   1   0   0   0
   1|     1    10   1   0   0   0
   2|     2    11   1   0   0   0
   3|     3    12   1   0   0   0
   4|     4    13   1   0   0   0
   5|     5    14   1   0   0   0
   6|     6    15   1   0   0   0
   7|     7    16   1   0   0   0
   8|     8    17   1   0   0   0
   9|     9    18   1   0   0   0
  10|     a    19   1   0   0   0
  11|     b    1a   1   0   0   0
  12|     c    1b   1   0   0   0
  13|     d    1c   1   0   0   0
  14|     e    1d   1   0   0   0
Page0 | 34 0 0 c 0 0 0 0 8 0 0 c2120 0 0 0 0 224 c 0 0 0 8 0e0 3 0 0 0 0 1 0 224 c 0 0 0 8 0e0 3 0 0 0 0 2 0 224 c 0 0 0 8 0e0 3 0 0 0 0
...
Page5 |  0 0 33cf0 26324211062 018 0c38f 0 0 0 0 0 043ac14 0c28f 0 0 0 0 1 043245f 0 0 814 0c3af10 0c28f 0 0 0 0 1 0432454 0 0 810 0c3af
...
Page15| 34 0 0 c 0 0 0 0 8 0 0 c2120 0 0 0 0 224 c 0 0 0 8 0e0 3 0 0 0 0 1 0 224 c 0 0 0 8 0e0 3 0 0 0 0 2 0 224 c 0 0 0 8 0e0 3 0 0 0 0
...
Page20|  0 0 43cf0 2848c 8 0 0 c 0 0 0 021e8c0 324 0bf8f20 0be8f 8 0e0 328 0bd27 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
...
Page31|  0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
[Exit] Exit from user program
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     0   0   0   0   0
   1|     1     1   0   0   0   0
   2|     2     2   0   0   0   0
   3|     3     3   0   0   0   0
   4|     4     4   0   0   0   0
   5|     5     5   0   0   0   0
   6|     6     6   0   0   0   0
   7|     7     7   0   0   0   0
   8|     8     8   0   0   0   0
   9|     9     9   0   0   0   0
  10|     a     a   0   0   0   0
  11|     b     b   0   0   0   0
  12|     c     c   0   0   0   0
  13|     d     d   0   0   0   0
  14|     e     e   0   0   0   0
[Exit] Exit from user program
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     f   0   0   0   0
   1|     1    10   0   0   0   0
   2|     2    11   0   0   0   0
   3|     3    12   0   0   0   0
   4|     4    13   0   0   0   0
   5|     5    14   0   0   0   0
   6|     6    15   0   0   0   0
   7|     7    16   0   0   0   0
   8|     8    17   0   0   0   0
   9|     9    18   0   0   0   0
  10|     a    19   0   0   0   0
  11|     b    1a   0   0   0   0
  12|     c    1b   0   0   0   0
  13|     d    1c   0   0   0   0
  14|     e    1d   0   0   0   0
```

可以看见两个用户进程的数据可以同时驻留在内存里。

#### **Exercise 6  缺页中断处理**

​	*基于TLB机制的异常处理和页面替换算法的实践，实现缺页中断处理（注意！TLB机制的异常处理是将内存中已有的页面调入TLB，而此处的缺页中断处理则是从磁盘中调入新的页面到内存）、页面替换算法等。*

##### 初始化地址空间

我们为每一个用户线程创建一个和其地址空间一样大的虚拟内存文件（AddrSpace类中的OpenFile* vm）。

由于虚拟内存机制需要保留对多线程的支持，不同线程的虚拟内存文件还不能重名。

```cpp
 AddrSpace::AddrSpace(){
   ...
   # ifdef USE_VM
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
    if(!(vm = fileSystem->Open(vmname))){
        printf("\033[1;31m[AddrSpace] VM: Failed to open %s: check permissions and your code\n\033[0m", vmname);
        ASSERT(FALSE);
    }

    DEBUG('V', "[AddrSpace] VM: Created vm file %s\n", vmname);
```

在初始化页表时，我们先不把任何东西加载到物理内存，并将所有的页表项设为无效，以增加PageFault出现的几率。

之后用户程序文件的代码段和数据段会被加载到buffer（buffer和地址空间一样大），整个buffer会被写入虚拟内存文件。

```cpp
AddrSpace::AddrSpace(){
 		...
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

    // dump temp mem -> vm
    vm->WriteAt(buffer, MemorySize, 0);   
    #endif
 }
```

##### 缺页异常处理

loadPageFromVM函数将当前线程虚拟内存文件的第vpn页加载到物理内存的ppn页。在算出物理地址和虚拟地址之后，loadPageFromVM调用ReadAt，将物理内存从physAddr开始的一页数据覆盖为虚拟内存文件从position开始的数据。

```cpp
ExceptionType AddrSpace::loadPageFromVM(int vpn){
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

    return NoException;
}
```

这里我加入了大量的错误处理代码。ReadAt函数的实现较为简单粗暴，并不会检查写入数据的位置是否出现数组越界。如果物理地址不正确，ReadAt会修改mainMemory之外的内存，从而导致一系列难以调试的错误。NachOS将00000000认为是nop，导致这个问题的症状更具有迷惑性。在键盘翻飞与一屏屏的gdb调试信息中，一晚上又轻易地过去了。

NachOS中TLB Miss与Page Fault共用了PageFaultException，因此我们需要在处理TLB Miss的时候检查是否真的发生了Page Fault。

```cpp
void handleTLBMiss(unsigned int virtAddr){
		...
    // Check if we got an actual page fault
    if (!machine->pageTable[vpn].valid) handlePageFault(vpn);
    // update TLB entry
    machine->tlb[TLBToReplace] = machine->pageTable[vpn];
}
```

如果当前表项无效，就说明发生了缺页。首先我们需要分配一个新物理页，如果分配失败则退出，然后等待下一个Execise解决这个问题。

分配成功之后，我们需要修改pageTable[vpn]，并调用loadPageFromVM(vpn)将数据载入内存。

如果需要创建新页怎么办？这并不是问题，虚拟内存文件事实上就是另一个虚拟地址空间，因此将数据载入内存可以实现创建新页的功能。

```cpp
void handlePageFault(unsigned int vpn){
    if (vpn >= machine->pageTableSize){
         printf("\033[1;31m[handlePageFault] Invalid vpn=%d/%d\n", vpn, machine->pageTableSize);
    }

    // PPN might not exist; require a new page
    int newPPN = machine->allocBit();

    if(newPPN >= 0) DEBUG('V', "[handlePageFault] Found available ppn=%d\n", newPPN);

    if (newPPN == -1) {  // all physical page used up,,,
      ASSERT(FALSE);  // not implemented
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
```

##### 测试

我们尝试运行两次sort：

```
[AddrSpace] Allocating 15 pages to user program
[AddrSpace] VM: Created vm file 1-vm.bin
Initializing code segment, at 0x0, size 752
[loadPageFromVM] Loading vpn=0 from vm
[loadPageFromVM] Loading vpn=1 from vm
[loadPageFromVM] Loading vpn=14 from vm
[loadPageFromVM] Loading vpn=2 from vm
[loadPageFromVM] Loading vpn=5 from vm
[loadPageFromVM] Loading vpn=6 from vm
[loadPageFromVM] Loading vpn=3 from vm
[loadPageFromVM] Loading vpn=4 from vm
[Exit] Exit from user program
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     0   0   0   0   0
   1|     1     1   0   0   0   0
   2|     2     3   0   0   0   0
   3|     3     6   0   0   0   0
   4|     4     7   0   0   0   0
   5|     5     4   0   0   0   0
   6|     6     5   0   0   0   0
   7|     7ffffffff   0   0   0   0
   8|     8ffffffff   0   0   0   0
   9|     9ffffffff   0   0   0   0
  10|     affffffff   0   0   0   0
  11|     bffffffff   0   0   0   0
  12|     cffffffff   0   0   0   0
  13|     dffffffff   0   0   0   0
  14|     e     2   0   0   0   0
[AddrSpace] Allocating 15 pages to user program
[AddrSpace] VM: Created vm file 2-vm.bin
Initializing code segment, at 0x0, size 752
[loadPageFromVM] Loading vpn=1 from vm
[loadPageFromVM] Loading vpn=2 from vm
[loadPageFromVM] Loading vpn=5 from vm
[loadPageFromVM] Loading vpn=14 from vm
[loadPageFromVM] Loading vpn=6 from vm
[loadPageFromVM] Loading vpn=3 from vm
[loadPageFromVM] Loading vpn=4 from vm
[loadPageFromVM] Loading vpn=0 from vm
[Exit] Exit from user program
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     7   0   0   0   0
   1|     1     0   0   0   0   0
   2|     2     1   0   0   0   0
   3|     3     5   0   0   0   0
   4|     4     6   0   0   0   0
   5|     5     2   0   0   0   0
   6|     6     4   0   0   0   0
   7|     7ffffffff   0   0   0   0
   8|     8ffffffff   0   0   0   0
   9|     9ffffffff   0   0   0   0
  10|     affffffff   0   0   0   0
  11|     bffffffff   0   0   0   0
  12|     cffffffff   0   0   0   0
  13|     dffffffff   0   0   0   0
  14|     e     3   0   0   0   0
```

可以看到Page Fault能被正确地处理。

#### **Exercise 7 Lazy Load**

​	*我们已经知道，Nachos系统为用户程序分配内存必须在用户程序载入内存时一次性完成，故此，系统能够运行的用户程序的大小被严格限制在4KB以下。请实现Lazy-loading的内存分配算法，使得当且仅当程序运行过程中缺页中断发生时，才会将所需的页面从磁盘调入内存。*

当前的NachOS实现中，每一个用户程序占用的物理内存大小与其地址空间大小相等，这显然不合理。如果我们同时运行多个用户程序，新的用户程序在物理内存空余的情况下也无法分配内存，会出现显著的内碎片，所以应该允许用户程序将用不到的数据换出物理内存。

##### 实现

dumpPageToVM和loadPageFromVM代码相似，实现的功能是将物理内存的PPN页换出到进程虚拟内存文件的VPN页中。

```cpp
ExceptionType AddrSpace::dumpPageToVM(int vpn){
    if (!pageTable[vpn].valid) DEBUG('V',"[dumpPageToVM] You are trying to dump invalid vpage %d\n", vpn);
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

    return NoException;
}
```

我们的handlePageFault需要在无法分配空闲物理页时，主动将页面换出：

```cpp
void handlePageFault(unsigned int vpn){
    ...
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
        // Don't load, wait for other threads to finish then try again
        currentThread->Yield();
        return;  
    }
		...
}
```

当找不到空闲物理页时，handlePageFault会尝试：

1. 尝试找dirty=0的物理页

   找到之后，handlePageFault会将页表**和TLB**中该页的有效位置为FALSE，不将其换出内存

2. 尝试找dirty=1的物理页

   找到之后，handlePageFault会将页表**和TLB**中该页的有效位置为FALSE，并将其换出内存

3. 处理其他情况

   这一种情况较为少见——当前线程上CPU时物理内存已经被分配完了。由于我们现在没有记录所有线程用户地址空间的数据结构，因此当前线程没法释放其它线程的地址空间。这时候当前线程会调用Yield先下CPU，等待别的线程回收内存。

##### 测试

我们选择运行多个占用内存在4KB以下的sort程序测试。这里需要对sort进行一些修改：

```cpp
# define N 400
int A[N];	/* size of physical memory; with code, we'll run out of space!*/
int main() {
    int i, j, tmp;
    /* first initialize the array, in reverse sorted order */
    for (i = 0; i < N; i++)		
        A[i] = N - i;
    Yield();
    Exit(A[0]);		/* and then we're done -- should be 0! */
}
```

将数据量N增大到400，可以让sort占用更多的内存，从而检验页是否被正确回收。

删去排序过程，避免看了一整屏的调试输出还不知道问题出在哪。

用户程序调用Exit时会回收内存，这很合理，然而这样运行多个sort也无法把物理内存占满。

我们选择实现Yield系统调用，让线程暂时下CPU，数据驻留在内存中。

```cpp
if (type == SC_Yield){
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
```

这里需要注意Yield返回到下一条指令，因此需要手动增加PC。

创建三个sort线程后，运行结果如下：

```
[AddrSpace] Allocating 24 pages to user program
[AddrSpace] VM: Created vm file 1-vm.bin
Initializing code segment, at 0x0, size 384
[AddrSpace] Allocating 24 pages to user program
[AddrSpace] VM: Created vm file 2-vm.bin
Initializing code segment, at 0x0, size 384
[AddrSpace] Allocating 24 pages to user program
[AddrSpace] VM: Created vm file 3-vm.bin
Initializing code segment, at 0x0, size 384
```

每一个sort占用的内存大小为24页，运行3个显然会超出NachOS的物理内存大小。

```
[handlePageFault] vpn=0
[handlePageFault] Found available ppn=0
[loadPageFromVM] Loading vpn=0 ppn=0 from vm
[loadPageFromVM] vmname=1-vm.bin position 0/4096 (128) 
...
[handlePageFault] Found available ppn=16
[loadPageFromVM] Loading vpn=15 ppn=16 from vm
[loadPageFromVM] vmname=1-vm.bin position 1920/4096 (128) 
[Yield] Yield from 1-userProg
[handlePageFault] vpn=0
[handlePageFault] Found available ppn=17
[loadPageFromVM] Loading vpn=0 ppn=17 from vm
[loadPageFromVM] vmname=2-vm.bin position 0/4096 (128) 
...
[handlePageFault] Found non-dirty vpn=2 ppn=20
[loadPageFromVM] Loading vpn=1 ppn=20 from vm
[loadPageFromVM] vmname=2-vm.bin position 128/4096 (128) 
[Yield] Yield from 2-userProg
  IND   VPN   PPN VAL RDO USE DIR
   0|     0    11   0   0   1   0
   1|     1    14   1   0   1   0
   2|     2    14   0   0   1   0
   3|     3    15   1   0   1   1
   4|     4    16   1   0   1   1
   5|     5    17   1   0   1   1
   6|     6    18   1   0   1   1
   7|     7    19   1   0   1   1
   8|     8    1a   1   0   1   1
   9|     9    1b   1   0   1   1
  10|     a    1c   1   0   1   1
  11|     b    1d   1   0   1   1
  12|     c    1e   1   0   1   1
  13|     d    1f   1   0   1   1
  14|     e    11   1   0   1   1
  15|     f    12   1   0   1   1
  16|    10ffffffff   0   0   0   0
  17|    11ffffffff   0   0   0   0
  18|    12ffffffff   0   0   0   0
  19|    13ffffffff   0   0   0   0
  20|    14ffffffff   0   0   0   0
  21|    15ffffffff   0   0   0   0
  22|    16ffffffff   0   0   0   0
  23|    17    13   1   0   1   1
[handlePageFault] vpn=0
[handlePageFault] No valid entry found, but somehow mem went full. Switching to other threads
[Exit] Exit from 1-userProg
  IND   VPN   PPN VAL RDO USE DIR
   0|     0     0   1   0   1   0
   1|     1     1   1   0   1   0
   2|     2     3   1   0   1   0
   3|     3     4   1   0   1   1
   4|     4     5   1   0   1   1
   5|     5     6   1   0   1   1
   6|     6     7   1   0   1   1
   7|     7     8   1   0   1   1
   8|     8     9   1   0   1   1
   9|     9     a   1   0   1   1
  10|     a     b   1   0   1   1
  11|     b     c   1   0   1   1
  12|     c     d   1   0   1   1
  13|     d     e   1   0   1   1
  14|     e     f   1   0   1   1
  15|     f    10   1   0   1   1
  16|    10ffffffff   0   0   0   0
  17|    11ffffffff   0   0   0   0
  18|    12ffffffff   0   0   0   0
  19|    13ffffffff   0   0   0   0
  20|    14ffffffff   0   0   0   0
  21|    15ffffffff   0   0   0   0
  22|    16ffffffff   0   0   0   0
  23|    17     2   1   0   1   1
[Exit] Exit from 2-userProg
  IND   VPN   PPN VAL RDO USE DIR
   0|     0    11   0   0   1   0
   1|     1    14   1   0   1   0
   2|     2    14   0   0   1   0
   3|     3    15   1   0   1   1
   4|     4    16   1   0   1   1
   5|     5    17   1   0   1   1
   6|     6    18   1   0   1   1
   7|     7    19   1   0   1   1
   8|     8    1a   1   0   1   1
   9|     9    1b   1   0   1   1
  10|     a    1c   1   0   1   1
  11|     b    1d   1   0   1   1
  12|     c    1e   1   0   1   1
  13|     d    1f   1   0   1   1
  14|     e    11   1   0   1   1
  15|     f    12   1   0   1   1
  16|    10ffffffff   0   0   0   0
  17|    11ffffffff   0   0   0   0
  18|    12ffffffff   0   0   0   0
  19|    13ffffffff   0   0   0   0
  20|    14ffffffff   0   0   0   0
  21|    15ffffffff   0   0   0   0
  22|    16ffffffff   0   0   0   0
  23|    17    13   1   0   1   1
[handlePageFault] vpn=1
[handlePageFault] Found available ppn=0
[loadPageFromVM] Loading vpn=1 ppn=0 from vm
[loadPageFromVM] vmname=3-vm.bin position 128/4096 (128) 
[handlePageFault] Found available ppn=15
[loadPageFromVM] Loading vpn=15 ppn=15 from vm
[loadPageFromVM] vmname=3-vm.bin position 1920/4096 (128) 
[Yield] Yield from 3-userProg
[Exit] Exit from 3-userProg
  IND   VPN   PPN VAL RDO USE DIR
   0|     0    10   1   0   1   0
   1|     1     0   1   0   1   0
   2|     2     2   1   0   1   0
   3|     3     3   1   0   1   1
   4|     4     4   1   0   1   1
   5|     5     5   1   0   1   1
   6|     6     6   1   0   1   1
   7|     7     7   1   0   1   1
   8|     8     8   1   0   1   1
   9|     9     9   1   0   1   1
  10|     a     a   1   0   1   1
  11|     b     b   1   0   1   1
  12|     c     c   1   0   1   1
  13|     d     d   1   0   1   1
  14|     e     e   1   0   1   1
  15|     f     f   1   0   1   1
  16|    10ffffffff   0   0   0   0
  17|    11ffffffff   0   0   0   0
  18|    12ffffffff   0   0   0   0
  19|    13ffffffff   0   0   0   0
  20|    14ffffffff   0   0   0   0
  21|    15ffffffff   0   0   0   0
  22|    16ffffffff   0   0   0   0
  23|    17     1   1   0   1   1
```

我们可以看出，运行过程大致如下：

1. sort-1 成功的分配了所有物理内存，并调用了Yield
2. sort-2 分配了部分物理内存，还有一部分只能置换自身的页
3. sort-3 上CPU时物理内存已经被分配完，因此调用Yield等待其它线程回收内存
4. sort-1，sort-2 退出，回收物理内存
5. sort-3 继续运行

这说明我们的实现没有问题。

#### **Challenge 2 倒排页表**

​	*多级页表的缺陷在于页表的大小与虚拟地址空间的大小成正比，为了节省物理内存在页表存储上的消耗，请在Nachos系统中实现倒排页表。*

##### 实现

在当前的NachOS中，每个线程的地址空间都需要一张页表，这显然需要比较多的存储空间。可不可以让整个机器共用一张物理页表，并在表项中标记物理页属于哪个线程？这就产生了倒排页表。由于我想尽量复用原先的代码（lan），倒排页表仅支持单线程，且不支持虚拟内存与Lazy Load。

首先我们要给页表项添加tid属性：

```cpp
class TranslationEntry {
  	...
    #ifdef REV_PAGETABLE
    int tid;  // the thread holding this entry
    #endif
};
```

页表的大小需要与物理内存的大小匹配：

```cpp
AddrSpace::AddrSpace(){
  	...
		# ifndef REV_PAGETABLE
        numPages = divRoundUp(size, PageSize);
    # else
        numPages = NumPhysPages;
    # endif
  	...
}
```

在初始化物理内存时，没有内存被线程占用，因此tid=-1：

```cpp
AddrSpace::AddrSpace(){
  	...
		for (i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;	// for now, virtual page # = phys page #
        ...
        # ifdef REV_PAGETABLE
            pageTable[i].tid = -1;
        # endif
}
```

现在线程回收内存时显然无法清空页表，只能清空属于自己的内存：

```cpp
void Machine::freeAllMem(){
    ...
    # ifdef REV_PAGETABLE
        for(int i = 0; i < pageTableSize; i++){
            // only free memory of current thread
            if(pageTable[i].valid && pageTable[i].tid == currentThread->getThreadId()){
                int pageToFree = pageTable[i].physicalPage;
                freeBit(pageToFree);  // set bitmap
                pageTable[i].valid = FALSE;  // set valid
            }
        }
    # endif
}
```

这里我选择了在Translate时检查tid。如果物理页没有被占用，那就占用它；如果被别的线程占用，那就退出。

```cpp
ExceptionType Machine::Translate(){
  ...
	if (tlb == NULL) {		// => page table => vpn is index into table
		...
		# ifdef REV_PAGETABLE
			if (pageTable[vpn].tid != currentThread->getThreadId()){
				if (pageTable[vpn].tid == -1){  // unused PPN
					pageTable[vpn].tid = currentThread->getThreadId();
				}else{
					DEBUG('R', "PPN %d owned by %d, however accessed by %d\n", vpn, pageTable[vpn].tid, currentThread->getThreadId());
					ASSERT(FALSE);  // might be altered soon
					return AddressErrorException;
				}
			}
		# endif
}
```

##### 测试

这里选用的测试程序与Execise 7相同，依然是我们的老朋友sort。

在开始sort时，我们初始化一个大小为NumPhysPages的地址空间：

```
[AddrSpace] Allocating 32 pages to user program
  IND   VPN   PPN VAL RDO USE DIR TID
   0|     0     0   1   0   0   0  -1
   1|     1     1   1   0   0   0  -1
   2|     2     2   1   0   0   0  -1
   3|     3     3   1   0   0   0  -1
   4|     4     4   1   0   0   0  -1
   5|     5     5   1   0   0   0  -1
   6|     6     6   1   0   0   0  -1
   7|     7     7   1   0   0   0  -1
   8|     8     8   1   0   0   0  -1
   9|     9     9   1   0   0   0  -1
  10|     a     a   1   0   0   0  -1
  11|     b     b   1   0   0   0  -1
  12|     c     c   1   0   0   0  -1
  13|     d     d   1   0   0   0  -1
  14|     e     e   1   0   0   0  -1
  15|     f     f   1   0   0   0  -1
  16|    10    10   1   0   0   0  -1
  17|    11    11   1   0   0   0  -1
  18|    12    12   1   0   0   0  -1
  19|    13    13   1   0   0   0  -1
  20|    14    14   1   0   0   0  -1
  21|    15    15   1   0   0   0  -1
  22|    16    16   1   0   0   0  -1
  23|    17    17   1   0   0   0  -1
  24|    18    18   1   0   0   0  -1
  25|    19    19   1   0   0   0  -1
  26|    1a    1a   1   0   0   0  -1
  27|    1b    1b   1   0   0   0  -1
  28|    1c    1c   1   0   0   0  -1
  29|    1d    1d   1   0   0   0  -1
  30|    1e    1e   1   0   0   0  -1
  31|    1f    1f   1   0   0   0  -1
```

当地址空间创建时，会调用allocBit函数在物理页与虚拟页之间创建一个映射。现在所有物理页都没有被占用，因此tid都是-1。

以下是用户程序调用Yield之后的页表：

```
[Yield] Yield from 1-userProg
  IND   VPN   PPN VAL RDO USE DIR TID
   0|     0     0   1   0   1   0   1
   1|     1     1   1   0   1   0   1
   2|     2     2   1   0   1   0   1
   3|     3     3   1   0   1   1   1
   4|     4     4   1   0   1   1   1
   5|     5     5   1   0   1   1   1
   6|     6     6   1   0   1   1   1
   7|     7     7   1   0   1   1   1
   8|     8     8   1   0   1   1   1
   9|     9     9   1   0   1   1   1
  10|     a     a   1   0   1   1   1
  11|     b     b   1   0   1   1   1
  12|     c     c   1   0   1   1   1
  13|     d     d   1   0   1   1   1
  14|     e     e   1   0   1   1   1
  15|     f     f   1   0   1   1   1
  16|    10    10   1   0   0   0  -1
  17|    11    11   1   0   0   0  -1
  18|    12    12   1   0   0   0  -1
  19|    13    13   1   0   0   0  -1
  20|    14    14   1   0   0   0  -1
  21|    15    15   1   0   0   0  -1
  22|    16    16   1   0   0   0  -1
  23|    17    17   1   0   0   0  -1
  24|    18    18   1   0   0   0  -1
  25|    19    19   1   0   0   0  -1
  26|    1a    1a   1   0   0   0  -1
  27|    1b    1b   1   0   0   0  -1
  28|    1c    1c   1   0   0   0  -1
  29|    1d    1d   1   0   0   0  -1
  30|    1e    1e   1   0   0   0  -1
  31|    1f    1f   1   0   1   1   1
```

我们可以观察到所有程序访问过的页表项都被设置了tid。

以下是用户程序退出后的页表：

```
[Exit] Exit from 1-userProg  
IND   VPN   PPN VAL RDO USE DIR TID
   0|     0     0   0   0   1   0   1
   1|     1     1   0   0   1   0   1
   2|     2     2   0   0   1   0   1
   3|     3     3   0   0   1   1   1
   4|     4     4   0   0   1   1   1
   5|     5     5   0   0   1   1   1
   6|     6     6   0   0   1   1   1
   7|     7     7   0   0   1   1   1
   8|     8     8   0   0   1   1   1
   9|     9     9   0   0   1   1   1
  10|     a     a   0   0   1   1   1
  11|     b     b   0   0   1   1   1
  12|     c     c   0   0   1   1   1
  13|     d     d   0   0   1   1   1
  14|     e     e   0   0   1   1   1
  15|     f     f   0   0   1   1   1
  16|    10    10   1   0   0   0  -1
  17|    11    11   1   0   0   0  -1
  18|    12    12   1   0   0   0  -1
  19|    13    13   1   0   0   0  -1
  20|    14    14   1   0   0   0  -1
  21|    15    15   1   0   0   0  -1
  22|    16    16   1   0   0   0  -1
  23|    17    17   1   0   0   0  -1
  24|    18    18   1   0   0   0  -1
  25|    19    19   1   0   0   0  -1
  26|    1a    1a   1   0   0   0  -1
  27|    1b    1b   1   0   0   0  -1
  28|    1c    1c   1   0   0   0  -1
  29|    1d    1d   1   0   0   0  -1
  30|    1e    1e   1   0   0   0  -1
  31|    1f    1f   0   0   1   1   1
```

用户程序退出后，所有tid=1的表项都被设为invalid，表明用户程序回收了自己的内存。

#### 遇到的困难以及收获

1. gdb的使用

   在难以使用其他调试工具的docker环境下，gdb真是帮了大忙。以下是几个常用命令：

   `p machine->printMem()` 显示结果（可以是当前上下文的变量或函数）

   `info stack` 显示函数调用栈

   `watch currentThread->space->vm` 当变量值变化时触发断点

2. ANSI `\033`

   可以设置printf打印字符串的颜色，方便在浩繁的调试输出信息中找到一些关键。例如`\033[31m`可以将字符串设置为红色。

   <img src="/Users/Apple/Desktop/屏幕快照 2020-12-09 上午12.13.30.png" alt="屏幕快照 2020-12-09 上午12.13.30" style="zoom:50%;" />

#### 对课程或Lab的意见和建议

1. 希望助教提供Lab的答疑和讨论（主要是NachOS代码相关的
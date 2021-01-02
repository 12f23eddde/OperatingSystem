#### 任务完成情况

| Exercise 1 | Exercise 2 | Exercise 3 | Exercise 4 | Exercise 5 |
| ---------- | ---------- | ---------- | ---------- | ---------- |
| Y          | Y          | Y          | Y          | Y          |

#### Exercise 1 源代码阅读

> 阅读与系统调用相关的源代码，理解系统调用的实现原理。

##### 1.1 code/userprog/syscall.h

syscall.h中定义了NachOS中10种系统调用类型：

```cpp
#define SC_Halt		0
#define SC_Exit		1
#define SC_Exec		2
#define SC_Join		3
#define SC_Create	4
#define SC_Open		5
#define SC_Read		6
#define SC_Write	7
#define SC_Close	8
#define SC_Fork		9
#define SC_Yield	10
```

syscall.h中同时声明了这10中系统调用的处理函数：

```cpp
#ifndef IN_ASM
  void Halt();		
  void Exit(int status);	
  typedef int SpaceId;	
  SpaceId Exec(char *name);
  int Join(SpaceId id); 	
  typedef int OpenFileId;	
  #define ConsoleInput	0  
  #define ConsoleOutput	1  
  void Create(char *name);
  OpenFileId Open(char *name);
  void Write(char *buffer, int size, OpenFileId id);
  int Read(char *buffer, int size, OpenFileId id);
  void Close(OpenFileId id);
  void Fork(void (*func)());
  void Yield();		
#endif
```

我们这里注意到这些函数仅当没有定义`IN_ASM`宏时才会被声明。我们可以在sysdep.h中找到这些函数的定义：（以下以Exit为例）

```cpp
void Exit(int exitCode){
    exit(exitCode);
}
```

可以观察到这里的Exit仅仅是Unix标准库中定义的exit的封装，其他函数亦是如此。（Halt除外，x86体系不允许用户态程序调用Halt指令）

start.c中会以汇编的形式定义系统调用，后文会对其进行详细叙述。

##### 1.2 code/userprog/exception.cc

我们首先回顾一下NachOS用户程序触发异常的流程。NachOS用户程序会被编译成MIPS汇编的形式，在mipssim.cc中定义的MIPS模拟器上执行。

###### 1.2.1 OneInstruction

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

###### 1.2.2 RaiseException

在执行指令过程中触发一个异常，并将发生内存异常的地址badVAddr存入寄存器BadVAddrReg。

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

###### 1.2.3 ExceptionHandler

在RaiseException触发异常后，NachOS就会执行exception.cc中定义的ExceptionHandler函数：

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

OneInstrction函数中调用RaiseException后直接return，PC还没有被更新。因此如果我们的异常处理程序需要返回到下一条指令，需要手动更新PC。

##### 1.3 code/test/start.s(c)

```cpp
#define IN_ASM
#include "syscall.h"
```

start.c中定义了`IN_ASM`宏之后再`include "syscall.h"`，也就是说syscall.h中的异常处理函数并不会被声明。start.c中以汇编的形式定义了这些函数（以下以Halt为例）：

```assembly
	.globl Halt
	.ent	Halt
Halt:
	addiu $2,$0,SC_Halt
	syscall
	j	$31
	.end Halt
```

按照MIPS架构的规定，异常号位于r2寄存器，如果异常处理程序需要参数，放置在r4-r7寄存器，返回结果放置在r2寄存器。

#### Exercise 2 系统调用实现

> 类比Halt的实现，完成与文件系统相关的系统调用：Create, Open，Close，Write，Read。Syscall.h文件中有这些系统调用基本说明。

##### 2.1 Create

```cpp
// [lab6] increase PC for syscall -> Next instr
void increasePC(){
    machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
    machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
    machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg) + 4);
}
```

系统调用需要返回到下一条指令，因此在异常处理程序中我们需要手动更新PC。increasePC函数会手动设置PrevPC，PC，NextPC。

```cpp
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
```

Create函数的参数是Char*型，按照MIPS体系结构的规定，字符串的地址存放在r4寄存器中。

getNameFromAddr函数从r4寄存器中读出字符串地址，并将字符串复制到name中。这一复制过程是必要的，因为用户空间的内存地址不是实际的内存地址，对用户程序内存空间的读写只能通过ReadMem、WriteMem函数进行。

```cpp
case SC_Create:{  // void Create(char *name);
  DEBUG('C', "\033[33m[Create] Create from %s\n\033[0m", currentThread->getName());
  char* name = getNameFromAddr();
  bool res = fileSystem->Create(name, 0);
  if(res) printf("[Create] Successfully created %s\n", name);
  else printf("[Create] Failed to create %s\n", name);
  delete name;
  increasePC();  // -> next instr
}
break;
```

Create系统调用：

1. 从用户内存中读出字符串
2. 调用fileSystem->Create()创建文件
3. 释放name字符串的空间，避免内存泄漏
4. 更新PC

##### 2.2 Open

```cpp
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
```

Open系统调用：

1. 从用户内存中读出字符串
2. 调用fileSystem->Open()打开文件
3. 释放name字符串的空间，避免内存泄漏
4. 根据MIPS体系结构的要求，将返回值放于r2中（为了简化实现，我们将OpenFile的内存地址当做OpenFileId，也能保证唯一性）
5. 更新PC

##### 2.3 Close

```cpp
case SC_Close:{  // void Close(OpenFileId id);
  DEBUG('C', "\033[33m[Close] Close from %s\n\033[0m", currentThread->getName());
  int addr = machine->ReadRegister(4);  // 1st arg
  OpenFile* file = (OpenFile*) addr;
  ASSERT(file);  // no one wants to encounter nullptr here
  delete file;
  increasePC();  // -> next instr
}
break;
```

Close系统调用：

1. 从寄存器中读出OpenFile的地址
2. delete file 关闭文件
3. 更新PC

##### 2.4 Read

```cpp
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
```

Read系统调用：

1. 从r4， r5，r6中读出参数into，size，addr
2. 检查size是否合理
3. 调用file->Read()，将addr指向的文件内容读入buffer（正如上文提到的，Read函数无法直接读写用户内存空间，因此需要buffer）
4. 将buffer的内容复制到用户内存空间
5. 设置r2的值为size，释放buffer，更新PC

##### 2.5 Write

```cpp
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
```

Write系统调用：

1. 从r4， r5，r6中读出参数into，size，addr
2. 检查size是否合理
3. 将用户内存空间中的内容复制到buffer
4. 调用file->Write()，将buffer中的内容写入磁盘
5. 释放buffer，更新PC

#### Exercise 3  编写用户程序

> 编写并运行用户程序，调用练习2中所写系统调用，测试其正确性。

我们编写了一个简单的程序`syscall_test.c`来测试系统调用：

```cpp
#include "syscall.h"
char filename[9] = "Shuwarin";
char text[9] = "Dreaming";

int main(){
    char buffer[9];
    int fileno;
    Create(filename);
    fileno = Open(filename);
    Write(text, 9, fileno);
    Close(fileno);
    fileno = Open(filename);
    Read(buffer, 9, fileno);
    Close(fileno);
}
```

1. 创建文件"Shuwarin"
2. 打开文件
3. 在文件中写入"Dreaming"
4. 关闭文件
5. 打开文件
6. 从文件中读取
7. 关闭文件

这里需要注意，字符串末尾还有一个'\0'，我们需要预留'\0'的空间。不加'\0'会导致getNameFromAddr()读出的文件名末尾出现乱码。

以下是测试结果：

```
[Create] Create from test1
Creating file Shuwarin, size 0
Reading 1320 bytes at 0, from file of length 1320.
Reading 128 bytes at 0, from file of length 128.
[Create] Creating file Shuwarin (File), size 0, time 2368520
Writing 1320 bytes at 0, from file of length 1320.
Reading 40 bytes at 1280, from file of length 1320.
Writing 128 bytes at 0, from file of length 128.
[Create] Successfully created Shuwarin
[Open] Open from test1
Opening file Shuwarin
Reading 1320 bytes at 0, from file of length 1320.
[Open] Successfully open Shuwarin @0x8ca09a8
[Write] Write from test1
[Write] Dreaming
Reading 128 bytes at 0, from file of length 128.
[ScaleUp] Extending (1/0)
[ScaleUp] Allocating sector 20, i=0
Writing 128 bytes at 0, from file of length 128.
Writing 9 bytes at 0, from file of length 0.
Reading 9 bytes at 0, from file of length 9.
[Write] Written 9 bytes
[Close] Close from test1
[Open] Open from test1
Opening file Shuwarin
Reading 1320 bytes at 0, from file of length 1320.
[Open] Successfully open Shuwarin @0x8ca09b8
[Read] Read from test1
Reading 9 bytes at 0, from file of length 9.
[Read] Successfully read 9 bytes
[Read] Dreaming
[Close] Close from test1
[Exit] Exit from test1
```

我们可以看到Read系统调用成功从文件中读出了"Dreaming"，结果符合我们的预期。

#### Exercise 4  系统调用实现

> 实现如下系统调用：Exec，Fork，Yield，Join，Exit。Syscall.h文件中有这些系统调用基本说明。

##### 4.1 Yield

```cpp
case SC_Yield:{  // void Yield();
  DEBUG('C', "\033[33m[Yield] Yield from %s\n\033[0m", currentThread->getName());
  increasePC();  // -> next instr
  currentThread->Yield(); // Yield current thread
}
break;
```

Yield系统调用已于Lab4时实现。当调用Yield时：

1. 更新PC
2. 执行currentThread->Yield()

##### 4.2 Exit

```cpp
case SC_Exit:{  // void Exit(int status);
  #ifdef USER_PROGRAM
    // free everything in current thread's address space
    if (currentThread->space != NULL) {
      machine->freeAllMem();
      delete currentThread->space;
      currentThread->space = NULL;
    }
    machine->printMem(machine->mainMemory);
  #endif
  int exitcode = machine->ReadRegister(4);
  printf("\033[33m[Exit] Exit %d from %s\n\033[0m", exitcode, currentThread->getName());
  currentThread->Finish(); // Finish current thread
}
```

Exit系统调用已于Lab4时实现。当调用Exit时：

1. 回收当前线程的内存空间
2. 调用currentThread->Finish()结束线程。

##### 4.3 Fork

```cpp
// [lab6] starting forked function in current thread
void RunFunc(int funcPC){
    DEBUG('C', "\033[1;33m[RunFunc] Starting %s @PC=%d\033[0m\n", currentThread->getName(), funcPC);
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
```

用户程序Fork出的线程和原程序共用一个地址空间，但是有不同的PC。

当Fork出的线程开始执行时，需要先设置PC，并保存当前的上下文，再调用Machine->Run()执行。Machine->Run()理应不会返回。

```cpp
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
```

Fork系统调用：

1. 从r4中读出需要Fork的函数的PC
2. 创建一个线程，这个线程与当前线程共享同一个地址空间
3. 调用Fork，在新线程中执行RunFunc
4. 更新PC

##### 4.4 Exec

```cpp
// [lab4] starting a userprog thread
void RunProcess(int ptr){
    DEBUG('C', "\033[1;33m[RunProcess] Starting %s\033[0m\n", currentThread->getName());
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
```

RunProcess函数的实现与progtest.cc中执行用户程序的RunSingleProcess函数完全一致。当开始执行用户程序时：

1. 调用InitRegisters()初始化寄存器
2. 调用RestoreState()用PCB中加载页表
3. 调用machine->Run()运行用户程序（不会返回）

```cpp
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
}
break;
```

Exec系统调用：

1. 从r4中读取文件名
2. 尝试打开文件；如果失败，则在更新PC后返回
3. 建立新线程，并用文件初始化新线程的内存空间
4. 调用Fork，在新线程中执行RunProcess
5. 释放executable（这里不能释放filename，会导致线程名无法显示）
6. 更新PC，并将r2置为新线程的tid（这里用tid代替SpaceId，tid显然也是唯一的）

##### 4.5 Join

Join系统调用将等待一个线程执行完成，并返回那个线程的exit code。这就带来了两个问题：

1. 如何等待一个线程执行完成？
2. 如何保存exit code？

在这里我们采用较为简单的方法处理这些问题：

1. lockList为每个线程分配一把锁，当调用Exec分配线程时加锁，当线程调用Exit退出时解锁
2. exitStatus保存每个线程的exit code，线程在调用Exit时设置

首先我们需要修改system.h和system.cc，增加全局变量lockList和exitStatus：

```cpp
#ifdef USER_PROGRAM
    machine = new Machine(debugUserProg);	// this must come first
    // [lab6] Yield
    exitStatus[MAX_THREADS] = {0};
    for(int i = 0; i < MAX_THREADS; i++){
        lockList[i] = new Lock("execLock");
    }
#endif
```

当Exec创建线程时，需要找到lockList中新线程对应的锁，并加锁：

```cpp
case SC_Exec:{
  ...
  // [join] Acquire lock on exec
  lockList[t->getThreadId()]->Acquire();
}
break;
```

当线程调用Exit退出时，需要解锁，并设置exit code：

```cpp
case SC_Exit:{
  ...
  // [join] keep exit status
  exitStatus[currentThread->getThreadId()] = exitcode;
  // [join] release lock on exit
  lockList[currentThread->getThreadId()]->Release();
}
break;
```

waitThread函数通过加锁和解锁的过程，等待线程退出。

```cpp
// [lab6] wait thread to finish
void waitThread(int tid){
    lockList[tid]->Acquire();
    lockList[tid]->Release();
}
```

我先前曾经考虑过通过对线程的状态进行轮询来判断当前线程是否退出，可实践证明这并不是一种好的实现。

以下是修改后Join系统调用的实现：

```cpp
case SC_Join:{  // int Join(SpaceId id);
  DEBUG('C', "\033[33m[Join] Join from %s\n\033[0m", currentThread->getName());
  int tid = machine->ReadRegister(4);
  // wait for thread to finish by polling
	// while(findThread(tid)){
	//     DEBUG('C', "\033[33m[Join] %s: %d still running @%d\n\033[0m",
	//             currentThread->getName(), tid, stats->totalTicks);
 	//     currentThread->Yield();
	// }
  waitThread(tid);
  increasePC();
  // submit exit status ro r2
  machine->WriteRegister(2, exitStatus[tid]);
  DEBUG('C', "\033[33m[Join] Thread %d exited: %d\n\033[0m", tid, exitStatus[tid]);
}
break;
```

1. 调用waitThread函数等待线程退出
2. 更新PC
3. 将r2置为exitStatus[tid]，也就是对应线程的返回值。

#### Exercise 5  编写用户程序

> 编写并运行用户程序，调用练习4中所写系统调用，测试其正确性。

exit.c只执行`Exit(1)`：

```cpp
// [12f23eddde] Minimal test for exit
#include "syscall.h"

int main(){
    Exit(1);
}
```

我们对syscall_test.c做了修改：

```cpp
// [12f23eddde] Testing syscall
#include "syscall.h"

int exitCode;
SpaceId sp;
char executable[5] = "exit";

void testExec(){
    sp = Exec(executable);
    exitCode = Join(sp);
    Exit(exitCode);
}

int main(){
    Fork(testExec);
    Yield();
    testExec();
}
```

1. Fork函数testExec
2. 调用Yield主动让出CPU，执行Fork出的testExec
3. testExec：调用Exec执行exit，并调用Join等待exit执行完成，之后再调用Exit退出线程
4. 在当前线程内执行testExec

以下是测试的结果：

```cpp
[Create] Creating file exit (File), size 296, time 320520
[Create] Creating file test (File), size 440, time 1824520
[AddrSpace] Allocating 12 pages to user program
[Fork] Fork from test1
[Yield] Yield from test1
[RunProcess] Starting test1-fork @PC=208
[Exec] Exec from test1-fork
[AddrSpace] Allocating 10 pages to user program
[Exec] forked thread name=exit tid=0
[Join] Join from test1-fork
[RunProcess] Starting exit
[Exit] Exit 1 from exit
[Join] Thread 0 exited: 1
[Exit] Exit 1 from test1-fork
[AddrSpace] Allocating 10 pages to user program
[Exec] forked thread name=exit tid=0
[Join] Join from test1
[RunProcess] Starting exit
[Exit] Exit 1 from exit
[Join] Thread 0 exited: 1
[Exit] Exit 1 from test1
No threads ready or runnable, and no pending interrupts.
Assuming the program completed.
Machine halting!
```

我们可以发现，Yield后test1-fork线程开始执行，而test1线程会等待test1-fork后再执行exit程序，结果符合我们的预期。

#### 遇到的困难以及收获

1. gdb的使用

   在难以使用其他调试工具的docker环境下，gdb真是帮了大忙。以下是几个常用命令：

   `p machine->printMem()` 显示结果（可以是当前上下文的变量或函数）

   `info stack` 显示函数调用栈

   `watch currentThread->space->vm` 当变量值变化时触发断点

2. 字符串的末尾有'\0'，在计算字符串长度的时候一定要考虑。

3. 在include头文件时一定要考虑循环引用可能造成的问题。

#### 对课程或Lab的意见和建议

1. 希望之后的操作系统课程更换Lab，可以更换到NachOS更新的版本（如NachOS 5.0j）或者采用其他OS。
2. 希望助教能够更加明确实验要求，希望我们得出什么样的结果，并给出一定的测试。课程给出的实验要求有不少模棱两可之处，很容易导致人与人之间的理解出现偏差。
3. 希望实验报告别卷了。这学期以来我在NachOS的实验与报告上花费了大量时间，然而真正有学习效果的时间估计只有50%，例如为NachOS增加错误处理、试图弄明白NachOS各种宏和内部routine的定义、以及撰写长达万字的实验报告的时间都不能给予我充实感。我真诚地建议助教和老师能够在这些方面给予我们帮助，例如给出实验报告示例、规定字数上限（这真的非常重要，否则助教批改作业将是千万字级别的工作量）。
4. 最后，感谢老师与助教一个学期的付出，现在的操作系统课程已然不错，希望之后能变得更好。祝老师和助教元旦快乐。
#### 任务完成情况

| Exercise 1 | Exercise 2 | Exercise 3 | Exercise 4 |
|---|---|---|---|
|Y|Y|Y|Y|

**Exercise 1  调研**

**1.1** Linux操作系统的PCB实现

注：Linux操作系统并不怎么区分进程与线程，在大量的实现中将它们统称为task。

在Linux的task_struct中大致包含以下信息：

1. task的状态

   在task_struct中有两个变量state， flags来描述task的状态。其中state变量描述变量的大致状态(Running, Interruptible, Uninterruptible, Zombie, Stopped)，而flags变量则描述一些更加细节的状态，用于task的管理——flag例如`PF_USED_MATH`描述task是否需要使用FPU（早期的80387浮点协处理器，现代处理器的SSE/AVX浮点单元等），flags则是这些flag通过位运算叠加的结果。

2. 内存管理（包括进程地址空间，内存缺页信息等）

3. task管理

   Linux采用了不同的数据结构管理PCB：

   双向链表：`next_task, prev_task`指向链表中前一个、后一个PCB。双向链表的头和尾都是0号进程。

   红黑树： `*p_opptr, *p_pptr,*p_cptr,*p_ysptr,*p_osptr`分别代表原始父进程，父进程，子进程，新老兄弟进程。

4. 调度

   我们注意到linux的task_struct中policy变量代表了几种不同的调度算法（FIFO，CFS， RR等）。事实上，在Linux下运行的程序可以手动设置运行时采用什么调度算法。：

5. 进程权限（包括创建者的uid,gid等）

6. 文件相关（包括文件系统信息，打开文件信息等）

7. 信号相关（包括当前信号阻塞状态，信号处理函数等）

8. 其它

**1.2** NachOS的线程机制

NachOS的所有线程信息均定义在`class Thread`中。在我们修改NachOS前，`class Thread`包含以下数据成员：

```cpp
int* stackTop; // 栈顶指针
void *machineState[MachineStateSize]; // 寄存器状态（除了栈指针寄存器%sp)
int* stack; // 栈底指针
ThreadStatus status; // 线程的状态
char* name;  // 线程名（创建线程时定义）
```

NachOS中线程共有四种状态：JUST_CREATED，RUNNING，READY，BLOCKED。

```cpp
enum ThreadStatus { JUST_CREATED, RUNNING, READY, BLOCKED };
```

**Exercise 2  源代码阅读**

**2.1** code/threads/main.cc

`main.cc`是NachOS启动的入口。当运行`main`函数时，NachOS先进入`Initialize`函数初始化kernel。在这之后定义了不同的宏（如`THREADS`，`USER_PROGRAM`），来完成不同lab模块的测试。以`THREADS`为例，当测试NachOS线程机制时，`main`函数会从argv中读取测试号（testnum），随后会调用`thread.cc`中定义的`ThreadTest`函数进行测试。在这个Lab中，我们修改了`ThreadTest`，来调用我们自己编写的测试函数。当`main`函数退出时，如果还有正在运行的线程，则会把控制流切换到正在运行的线程。（通过`currentThread->Finish()`手动退出main线程实现）

**2.2** code/threads/threadtest.cc

`threadtest.cc`包含对NachOS线程模块的测试程序。当`main`函数调用了`ThreadTest`后，便会根据全局变量`testnum`执行对应的测试程序。在我们修改之前，`threadtest.cc`中含有一个简单的fork测试函数`ThreadTest1`。我们在`ThreadTest`中添加了case 2、3、4，以调用我们新增的测试函数。（有关这方面的具体内容，请参看Exercise 3及Exercise 4的部分）

**2.3** code/threads/thread.h

`thread.h`中定义了与线程相关的常量（例如`MachineStateSize`，`StackSize`），及`class Thread`。`class Thread`中声明了线程名、栈指针等PCB中具备的数据成员（正如上一节提到的），同时声明了`Fork`，`Yield`，`Sleep`，`Finish`等管理线程生命周期必须的函数。

**2.4** code/threads/thread.cc

`thread.cc`中包含了对`class thread`中声明的函数的实现：

- `Thread::Thread`

  Thread类的构造函数，在线程生命周期开始时初始化status和内存空间。

- `Thread::~Thread`

  Thread类的析构函数，在线程生命周期结束时释放内存空间。
  
- `Thread::Fork`

  为一个函数分配并初始化内存空间，并将其放至Ready队列。
  
- `Thread::CheckOverflow`

  手动检查线程所用内存是否超出了分配的内存空间。

- `Thread::Finish`

  告知调度器这个线程将被销毁（`threadToBeDestroyed = currentThread`），然后进入Sleep。

- `Thread::Yield`

  主动让出CPU给下一个Ready状态的线程；若没有其它线程需要运行，则立即返回。
  
- `Thread::Sleep`

  在当前进程等待同步条件（信号量，锁）时主动让出CPU。如果没有其它线程需要运行，则将CPU置为Idle。
  
- `Thread::StackAllocate`

  在线程初始化时分配内存空间。

**Exercise 3  扩展线程的数据结构**

**3.1** 增加“用户ID、线程ID”两个数据成员，并在Nachos现有的线程管理机制中增加对这两个数据成员的维护机制。

首先， 在`system.h`中声明常量`MAX_THREADS`(4.1需要用到)，声明全局变量数组`tid_allocated`用于标记一个tid是否被占用

```cpp
/* from system.h */
// [lab1] set max threads / extern tid
#define MAX_THREADS 128
extern bool tid_allocated[MAX_THREADS];  // declared only; must be defined in system.cc
```

在`system.cc`中对`tid_allocated`进行定义及初始化（其实初始化为false等同于什么都不做）


```cpp
/* from system.cc */
bool tid_allocated[MAX_THREADS] = {false};  // define here
```

在`threads.h`中加入`uid`，`tid`变量（private）。考虑到安全性，public方法中仅允许设置`uid`，而不允许设置`tid`。

```cpp
/* from threads.h */
// [lab1] Add uid, pid
private:
	int uid, pid;
public:
	...
  int getUserId() { return uid; }
  int getThreadId() { return tid; }
  void setUserId(int userId){ uid = userId; }
```

在线程初始化时，从`tid_allocated`寻找第一个没有被占用的tid（`tid_allocated[i]==false`)，占用这个tid（`tid_allocated[i] = true`）。


```cpp
/* from threads.cc  */
Thread::Thread(){
  ...
  // [lab1] Allocate tid 
  this->tid = -1;
  for(int i = 0; i < MAX_THREADS; i++){
    if (tid_allocated[i] == false){
      tid_allocated[i] = true;
      this->tid = i;
      break;
    }
  } 
}
```

在线程生命周期结束时，取消`tid_allocated[this->tid]`的占用状态。

```cpp
/* from threads.cc  */
Thread::~Thread(){
  ...
  // [lab1] free allocated tid
  tid_allocated[this->tid] = false;
}
```

**测试**

`Lab1Thread`除了打印tid和uid外什么也不做。

```cpp
/* from threadtest.cc */
void Lab1Thread(int uid, int tid){ printf("Created thread %d@%d\n", tid, uid); }
```

我们共建立114个线程，为每个线程分配不同的uid；在这里`if(tc%4==0) t->~Thread()`用于测试tid是否能被正常释放。

```cpp
/* from threadtest.cc */
void Lab1ThreadTest1(){
    int test_uids[4] = {114, 514, 1919, 810};
    for (int tc = 0; tc < 114; tc++){
        DEBUG('t', "Entering Lab1Thread uid=%d", test_uids[tc%4]);
        Thread *t = new Thread("Lab1Thread");
        t->setUserId(test_uids[tc%4]);
        Lab1Thread(t->getUserId(), t->getThreadId());
        if(tc%4==0) t->~Thread();
    }
}
```

测试结果符合我们的预期。

```
Created Thread 1@114
Created Thread 1@514
Created Thread 2@1919
Created Thread 3@810
Created Thread 4@114
Created Thread 4@514
...
```

**Exercise 4  增加全局线程管理机制**

**4.1** 在Nachos中增加对线程数量的限制，使得Nachos中最多能够同时存在128个线程

我们在初始化线程时判断是否被成功地分配了tid；若没有分配成功，则打印错误，并通过`ASSERT`退出。

```cpp
/* from threads.cc */
Thread::Thread(){
	...
  bool thread_allocated_tid = (0 <= this->tid && this->tid < MAX_THREADS);
  if(!thread_allocated_tid){
      printf("[thread] thread \"%s\" @(%d) failed to allocate tid (MAX_THREADS=%d)\n", name, uid, MAX_THREADS);
  }
  ASSERT(thread_allocated_tid);
}
```

**测试**

我们尝试建立129个线程：

```cpp
void Lab1ThreadTest2(){
    int test_uids[4] = {114, 514, 1919, 810};
    for (int tc = 0; tc < 129; tc++){
        DEBUG('t', "Entering Lab1Thread uid=%d", test_uids[tc%4]);
        Thread *t = new Thread("Lab1Thread");
        t->setUserId(test_uids[tc%4]);
        Lab1Thread(t->getUserId(), t->getThreadId());
    }
}
```
在尝试建立第128个线程时，nachos打印错误并通过`ASSERT`退出，这符合我们的预期。

```
...
Created Thread 126@514
Created Thread 127@1919
[thread] thread "Lab1Thread" @(0) failed to allocate tid (MAX_THREADS=128)
Assertion failed: line 58, file "../threads/thread.cc"
```

**4.2** 仿照Linux中PS命令，增加一个功能TS(Threads Status)，能够显示当前系统中所有线程的信息和状态

在`system.h`声明全局变量数组`threadsList`记录所有线程的指针，声明`printThreadsList`函数（即TS）。

```cpp
/* from system.h */
extern Thread *threadsList[MAX_THREADS];
void printThreadsList();
```

在`system.cc`中定义`threadsList`，并将其初始化为`NULL`。

```cpp
/* from system.cc */
Thread *threadsList[MAX_THREADS] = {NULL};
```

在`threads.h`中增加`getStatus`函数（status是私有变量）。

```cpp
/* from threads.h */
ThreadStatus getStatus() { return status; }
```

在初始化线程时将当前线程的指针加入`threadsList`中。

```cpp
/* from threads.cc */
Thread::Thread(){
	...
	threadsList[this->tid] = this;
}
```

在线程生命周期结束时从`threadsList`中移除当前线程的指针。

```cpp
/* from threads.cc */
Thread::~Thread(){
	...
	threadsList[this->tid] = NULL;
}
```

`printThreadsList`函数遍历`threadsList`数组，打印当前线程信息。

```cpp
/* from system.cc */
// [Lab1] printThreadsList
void printThreadsList(){
    printf("%5s %5s %20s %20s\n", "<tid>", "<uid>", "<name>", "<status>");
    for(int i = 0; i < MAX_THREADS; i++){
        if(tid_allocated[i]&&threadsList[i]){
            printf("%5d %5d %20s ", threadsList[i]->getThreadId(), threadsList[i]->getUserId(), threadsList[i]->getName());
            switch(threadsList[i]->getStatus()){
                case JUST_CREATED:printf("%20s\n", "JUST_CREATED");break;
                case RUNNING:printf("%20s\n", "RUNNING");break;
                case READY:printf("%20s\n", "READY");break;
                case BLOCKED:printf("%20s\n", "BLOCKED");break;
                default:printf("%20s\n", "UNDEFINED");ASSERT(0);break;
            }
        }
    }
}
```

**测试**

我们创建了10个线程，并给这10个线程分配不同的uid，`if(tc%4==0) t->~Thread()`语句测试线程是否被正常销毁；随后调用`printThreadsList`函数。

```cpp
void Lab1ThreadTest3(){
    int test_uids[4] = {114, 514, 1919, 810};
    for (int tc = 0; tc < 10; tc++){
        DEBUG('t', "Entering Lab1Thread uid=%d", test_uids[tc%4]);
        Thread *t = new Thread("Lab1Thread");
        t->setUserId(test_uids[tc%4]);
        printf("Created Thread %d@%d\n", t->getThreadId(), t->getUserId());
        Lab1Thread(t->getUserId(), t->getThreadId());
        if(tc%4==0) t->~Thread();
    }
    printThreadsList();
}
```

可以看到，测试结果符合我们的预期。

```
<tid> <uid>               <name>             <status>
    0     0                 main              RUNNING
    1   514           Lab1Thread         JUST_CREATED
    2  1919           Lab1Thread         JUST_CREATED
    3   810           Lab1Thread         JUST_CREATED
    4   514           Lab1Thread         JUST_CREATED
    5  1919           Lab1Thread         JUST_CREATED
    6   810           Lab1Thread         JUST_CREATED
    7   514           Lab1Thread         JUST_CREATED
```

#### 遇到的困难以及收获

**1. extern**

有大半年的时间我没有写过c++，对c/c++如何extern函数与变量忘的差不多了。如果只在`system.h`中extern一个变量，链接时会报`undefined`错误；若在extern时初始化，如`extern bool tid_allocated[MAX_THREADS] = {false}`，又会由于多个文件都include了`system.h`导致`multiple defination`错误。在查阅了相关资料后，我明白了`system.h`中是对变量的声明，`system.cc`中才应该包含对变量定义与初始化。这一Lab让我回想起了一些ICS课上有关于编译及链接的知识，对c/cpp又更加熟悉了一些。

**2. docker**

之前我曾经在维护服务器时尝试过部署docker，然而那时候只是跟着官方教程照葫芦画瓢，实际上对docker依然所知甚少。这次nachOS需要在32位Linux下编译和运行，然而Ubuntu对于32位的支持只持续到16.04，在较新的平台（AMD3000系列）上运行会报错，驱动也有许多兼容性问题；这时候，教学网上的"用Docker打开NachOS"给我指了条路——用Docker！Docker的资源占用与性能损失都低于虚拟机，容器化部署的便利性较在虚拟机管理器内导入ovf模板的方式也更胜一筹。

在配置这次lab的环境时，我参看了GitHub上前辈留下的Dockerfile，又查阅了Docker的一些官方文档和Reference，对Dockerfile的编写与docker命令行（如-p，-v，-it等指令）有了更深的了解。docker-compose看上去也很有吸引力，一句`docker compose up`就可以快速部署并运行一个完整的后端服务器，并且docker API也提供了监控以及自动重启功能。之后我会尝试在别的课程中使用docker，实现容器化部署及持续集成。

#### 对课程或Lab的意见和建议

1. 希望实验说明更够更为详细，您们可以适当参考UC Berkeley的NachOS Lab Handout。篇幅不一定像UC的十多页那么长，只要能具体而非笼统地说明您们希望我们完成的任务即可。
2. 希望助教能提供一些功能检测的函数（参考ICS的形式），感觉学生既当裁判员又当运动员不大合理。

#### 参考文献

[Linux Kernel 2.6.38 Source: /include/linux/sched.h (1193)](https://elixir.bootlin.com/linux/v2.6.38/source/include/linux/sched.h#L1193)

[How to change scheduling algorithm and priority used by a process?](https://access.redhat.com/solutions/2955601)

[浅析Linux下的task_struct结构体](https://www.jianshu.com/p/691d02380312)

[C/C++中extern关键字详解](https://www.jianshu.com/p/111dcd1c0201)

[Docker Docs - Reference](https://docs.docker.com/reference/)
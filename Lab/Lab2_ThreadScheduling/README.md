#### 任务完成情况

| Exercise 1 | Exercise 2 | Exercise 3 | Challenge 1 |
| ---------- | ---------- | ---------- | ----------- |
| Y          | Y          | Y          | Y           |

**Exercise 1  调研Linux中采用的进程调度算法**

为了避免行文冗杂，本文仅简要分析Linux2.6及之前的Linux进程调度机制。

**1.1 Linux 2.4 - 传统调度器**

Linux 2.4调度算法基本上与传统Unix操作系统一脉相承。

- task的组织方式

  在Linux2.4内核中，定义了一个队列`runqueue`。`runqueue`由所有的CPU共享，采用spinlock来解决同步问题。（当然，在处理器数量较多时，这样会造成明显的overhead）

  `runqueue`中存储所有状态为RUNNABLE的进程；也就是说只要一个进程的状态变为RUNNABLE，就会进入链表；不为RUNNABLE，就会从链表中移除。（这一点的实现类似于xv6）

- 时间片的处理

  Linux在进行调度时，明显地区分了实时进程与普通进程。在调度策略上，普通进程采用SCHED_OTHER，实时进程则采用SCHED_RR，SCHED_FIFO。

  - 对于实时进程，在调度器遍历`runqueue`，根据RR或FIFO规则进行调度。

  - 对于普通进程，则根据其静态优先级分配一个时间片，进程在运行时时间片递减；若时间片用完则不能上CPU。如果`runqueue`中没有进程，调度器则会重新分配时间片。

- 哪个进程上CPU？

  当调度器寻找下一个需要运行的进程时，会遍历就绪队列中的所有进程，调用`goodness`函数计算权值，选择权值最大的一个上CPU，然后进行上下文切换。（显然这是一个$O(n)$的过程）

  在进程调度时，调度器计算的权值是进程的动态优先级。影响动态优先级的因素有不少：

  - counter 当前进程剩的时间片越多，`counter`越大。如果一个进程之前`counter>0`时进入睡眠，则在`counter`重新分配时这个进程的优先级相对更高。（也许这样的设计有助于提高交互式进程的响应速度）
  - nice nice值是手动设定的优先级，范围是$[-19,20]$。（这一点参考了Unix的设计）Linux的静态优先级`rt_priority=20-nice`。
  - 如果进程上一次在同一个CPU上运行，增加一个常量。（减少CPU间的数据交换）
  - 如果进程不需要内存空间切换，增加1。（减少内存交换）
  - 如果进程是实时进程，增加一个较大的固定偏移量。（这能保证实时进程的执行更为优先）

**1.2 Linux 2.6 - O(1) 调度器**

随着计算机性能的发展（尤其是多核心CPU的出现），Linux 2.4的调度算法逐渐捉襟见肘——多CPU访问一个`runqueue`性能低下，且$O(n)$的调度过程会显著降低高性能计算机系统的调度性能。

Ingo Molnar提出了一个“空间换时间”的思路——既然所有的CPU运行任意优先级的进程都要遍历队列，为什么不增加队列的数量呢？

- 数据结构

  在Linux Kernel 2.6中，共划分了140个优先级——其中实时进程的优先级永远高于普通优先级，保证实时进程永远被优先执行。Linux kernel 2.6里有 140 个优先级，一个非常自然的想法就是用140个队列的array来管理进程。每个优先级对应的队列采用FIFO策略——新的进程插到队尾，先进先出。在这种情况下，insert / deletion 都是 O(1)。

  140个runqueue数目不小，若是调度器在寻找下一个待调度的进程时需要遍历所有的队列，也会造成很大的时间开销。在设计O(1)调度器时，设计者将一个队列映射到长度为140的bitarray中的一位——如果这个优先级队列下面有待调度的进程，那么对应的bit置为1，否则置为0。寻找最高位的位置这一操作正好对应x86中的`fls`指令，因此bitarray的实现是十分高效的。

- 优先级

  在Linux Kernel 2.6中，实时进程没有动态优先级（优先级的设计已经保证了实时进程会被优先执行）。对于普通进程而言，动态优先级还与bonus值有关。bonus值对应着进程之前的平均睡眠时间，睡眠时间越长则bonus值越大。事实上：

  动态优先级 $ = max(100,min($静态优先级$-bonus + 5 ,139))$

  Linux kernel 2.6又对普通进程中的交互进程和批处理进程做了区分——设计者认为，批处理进程占用大量的CPU资源，对响应时间要求不高；而交互进程大多数时间处于SLEEP状态，对响应时间要求很高。若一个进程满足以下条件：

  动态优先级 $ \leq \frac{3}{4}*$静态优先级$+28$

  则认为它是交互式进程。

- 调度策略

  Linux内核在调度时维护两个队列——active保存待调度的进程，expired保存已经下CPU而需要再次运行的进程，两个队列的数据结构是相同的。调度器首先尝试在active中找到优先级最高的非空队列，取队列的队首，这一过程是$O(1)$的；若调度器发现active为空，则交换active与expired的指针后继续——这一过程显然也是$O(1)$的。

  当一个进程下CPU后，有两种可能的操作：
  
  - 插入expired进程队尾 - 下CPU的是普通进程或当前运行的是实时/交互进程但是有进程处在饥饿状态
  
  - 插入活跃队列队尾 - 下CPU的是实时/交互进程

**Exercise 2  源代码阅读**

**2.1** code/threads/scheduler.h & code/threads/scheduler.cc

```cpp
/* from code/threads/scheduler.h */
class Scheduler {
  public:
    Scheduler();            // Initialize list of ready threads
    ~Scheduler();            // De-allocate ready list

    void ReadyToRun(Thread* thread);    // Thread can be dispatched.
    Thread* FindNextToRun();        // Dequeue first thread on the ready
                    // list, if any, and return thread.
    void Run(Thread* nextThread);    // Cause nextThread to start running
    void Print();            // Print contents of ready list
  private:
  	List *readyList;   // queue of threads that are ready to run, but not running
```

在我们修改之前，NachOS实现了一个简单的先到先服务调度算法，并且没有实现优先级。

- Scheduler()

  ```cpp
  Scheduler::Scheduler(){ readyList = new List; } 
  ```

  在Scheduler创建时，初始化ReadyList，ReadyList存放所有待调度的线程（与xv6不同，调度器并不会遍历所有进程）。List是NachOS的队列实现，不过实现了`Mapcar`，`SortedInsert`，`SortedRemove`等功能。

- ~Scheduler()

  ```cpp
  Scheduler::~Scheduler(){ delete readyList; } 
  ```

  在删除Scheduler时，删除ReadyList。

- ReadyToRun(Thread* thread)

  ```cpp
  void Scheduler::ReadyToRun (Thread *thread){
      thread->setStatus(READY);
      readyList->Append((void *)thread);
  }
  ```

  将`thread`（作为参数传入）的状态改为READY并放到ReadyList的末尾，供稍后调用。

- FindNextToRun()

  ```cpp
  Thread *Scheduler::FindNextToRun (){ return (Thread *)readyList->Remove(); }
  ```

  `FindNextToRun`从ReadyList中找到下一个待调度的线程，并将其从ReadyList中移除。这等价于：

  ```cpp
  res = queue.front();
  queue.pop();
  return res;
  ```

- Run(Thread* nextThread)

  ```cpp
  void Scheduler::Run (Thread *nextThread){
      Thread *oldThread = currentThread;
  
      oldThread->CheckOverflow();		    // check if the old thread
  					    // had an undetected stack overflow
  
      currentThread = nextThread;		    // switch to the next thread
      currentThread->setStatus(RUNNING);      // nextThread is now running
      
      // This is a machine-dependent assembly language routine defined 
      // in switch.s.  You may have to think
      // a bit to figure out what happens after this, both from the point
      // of view of the thread and from the perspective of the "outside world".
  
      SWITCH(oldThread, nextThread);
      
      DEBUG('t', "Now in thread \"%s\"\n", currentThread->getName());
  
      // If the old thread gave up the processor because it was finishing,
      // we need to delete its carcass.  Note we cannot delete the thread
      // before now (for example, in Thread::Finish()), because up to this
      // point, we were still running on the old thread's stack!
      if (threadToBeDestroyed != NULL) {
          delete threadToBeDestroyed;
  				threadToBeDestroyed = NULL;
      }
  }
  ```

  `Run`函数对应调度器从当前线程调度到`nextThread`的过程。

  在操作之前，调度器首先检查有无出现栈溢出。

  若一切正常，则把`currentThread`指针设为`nextThread`，并将其状态设为RUNNING。

  随后调用SWITCH函数进行上下文切换（对应switch.s中的汇编代码）。

  之后，`Run`函数尝试对运行结束的线程进行回收。

- Print()

  ```cpp
  void Scheduler::Print(){
      printf("Ready list contents: ");
      readyList->Mapcar((VoidFunctionPtr) ThreadPrint);
      printf("\n");
  }
  ```

  打印当前readyList中的所有线程名称。（`MapCar`函数将List中的每一个成员作为参数传给`ThreadPrint`）

**2.2** code/threads/switch.s

`switch.s`中存放平台相关的执行上下文切换的汇编代码，对应`SWITCH`函数。（以下是代码中的x86部分）

```cpp
/* void SWITCH( thread *t1, thread *t2 )
**
** on entry, stack looks like this:
**      8(esp)  ->              thread *t2
**      4(esp)  ->              thread *t1
**       (esp)  ->              return address
**
** we push the current eax on the stack so that we can use it as
** a pointer to t1, this decrements esp by 4, so when we use it
** to reference stuff on the stack, we add 4 to the offset.
*/
        .comm   _eax_save,4

        .globl  SWITCH
SWITCH:
        movl    %eax,_eax_save          # save the value of eax
        movl    4(%esp),%eax            # move pointer to t1 into eax
        movl    %ebx,_EBX(%eax)         # save registers
        ...
        movl    %esp,_ESP(%eax)         # save stack pointer
        movl    _eax_save,%ebx          # get the saved value of eax
        movl    %ebx,_EAX(%eax)         # store it
        movl    0(%esp),%ebx            # get return address from stack into ebx
        movl    %ebx,_PC(%eax)          # save it into the pc storage

        movl    8(%esp),%eax            # move pointer to t2 into eax

        movl    _EAX(%eax),%ebx         # get new value for eax into ebx
        movl    %ebx,_eax_save          # save it
        movl    _EBX(%eax),%ebx         # retore old registers
        ...
        movl    _ESP(%eax),%esp         # restore stack pointer
        movl    _PC(%eax),%eax          # restore return address into eax
        movl    %eax,4(%esp)            # copy over the ret address on the stack
        movl    _eax_save,%eax

        ret
```

- SWITCH通过栈中的指针，将通用寄存器状态备份到oldThread中，并保存栈中的返回地址，以便之后跳转。

- 之后，从newThread中恢复通用寄存器状态，将4(%esp)设为新线程的地址。
- 接下来，从SWITCH中返回到Run函数，检查是否有线程需要销毁；若没有，则再从Run返回跳转到线程代码。

**2.3** code/threads/timer.h & code/threads/timer.cc

`timer.cc`中定义了一个模拟的硬件计时器。这个模拟计时器通过NachOS的Interrupt机制，每隔一段时间执行handler函数。

```cpp
// The following class defines a hardware timer. 
class Timer {
  public:
    Timer(VoidFunctionPtr timerHandler, int callArg, bool doRandom);
				// Initialize the timer, to call the interrupt
				// handler "timerHandler" every time slice.
    ~Timer() {}

// Internal routines to the timer emulation -- DO NOT call these

    void TimerExpired();	// called internally when the hardware
				// timer generates an interrupt

    int TimeOfNextInterrupt();  // figure out when the timer will generate
				// its next interrupt 

  private:
    bool randomize;		// set if we need to use a random timeout delay
    VoidFunctionPtr handler;	// timer interrupt handler 
    int arg;			// argument to pass to interrupt handler
};
```

- Timer

  初始化一个硬件计时器。

  其中callArg以int的形式把参数传给timerHandler，doRandom判断这一个计时器要不要使用随机时间。

  随后调用`interrupt->Schedule`，设置在一定时间间隔后执行时钟事件。

  ```cpp
  Timer::Timer(VoidFunctionPtr timerHandler, int callArg, bool doRandom){
      randomize = doRandom;
      handler = timerHandler;
      arg = callArg; 
    // schedule the first interrupt from the timer device
      interrupt->Schedule(TimerHandler, (int) this, TimeOfNextInterrupt(), 
  		TimerInt); 
  }
  ```

- TimeofNextInterrupt

  计算下一次中断的时间。如果使用随机时间，则中断间隔为$[1, TimerTicks * 2]$；否则中断间隔为$TimerTicks$。（TimerTicks定义在`/machine/stats.h `，默认值为100）

  ```cpp
  int Timer::TimeOfNextInterrupt() {
      if (randomize)
    return 1 + (Random() % (TimerTicks * 2));
      else
    return TimerTicks; 
  }
  ```

- TimeExpired

  当时钟中断事件到来时，`TimerExpired`函数会被调用。这个函数设置下一次时钟事件，之后执行handler中定义的函数。
  
  ```cpp
  void Timer::TimerExpired() {
      // schedule the next timer device interrupt
    interrupt->Schedule(TimerHandler, (int) this, TimeOfNextInterrupt(), 
      TimerInt);
  
      // invoke the Nachos interrupt handler for this device
      (*handler)(arg);
  }
  ```


**Exercise 3  实现基于优先级的抢占式调度算法**

**3.1** 手动设置调度算法

为了方便测试，我们定义了调度器的三种调度算法：NAIVE（原本NachOS的调度算法），STATICPRIORTY（静态优先级），ROUND_ROBIN（时间片轮转）。

```cpp
enum SchedulerPolicy { NAIVE, STATICPRIORTY, ROUND_ROBIN };
```

在Thread类中，我们增加了一个私有变量和一个成员函数，允许测试程序设置调度器的调度算法。

```cpp
class Thread {
  public:
  	...
    void setPolicy(SchedulerPolicy newpolicy);
  private:
    SchedulerPolicy policy;
}
```

**3.2** 实现优先级数据结构

针对每个线程，我们增加了priority私有变量，并增加了两个成员函数——setter和getter。

```cpp
class Thread {
  private:
  	...
    int priority;  // [lab2] set priority HIGH 0 - 127 LOW

  public:
    ...
    // [lab2] Add priority
    int getPriority() { return priority; }
    void setPriority(int newpr) {priority = newpr; }
```

**3.3** 高优先级抢占

- 首先我们要让待调度的线程在readyList上按照优先级排序：

  幸运的是，List中的`SortedInsert`函数正好实现了这个功能。只要把Append换成SortedInsert，问题就解决了。

- 然后我们需要让新来的高优先级进程主动把currentThread从控制流中抢下来：

  在条件判断成立之后，ReadyToRun函数会调用Thread的Run方法，强行把控制流切换到新来的进程。

```cpp
void Scheduler::ReadyToRun (Thread *thread){
    DEBUG('t', "Putting thread %s on ready list.\n", thread->getName());
    thread->setStatus(READY);

    // [lab2]  Thank GOD!!! You got SortedInsert(), which inserts elements in asending order.
    if (policy==STATICPRIORTY){
        readyList->SortedInsert((void *)thread, thread->getPriority());
    } else {
        readyList->Append((void *)thread);
    }

  	// 抢占
    if(this->policy==STATICPRIORTY && thread->getPriority() < currentThread->getPriority()){
        printf("[handler] context switch (pr) prev->pr=%d new-pr=%d\n", currentThread->getPriority(), thread->getPriority());
        currentThread->setStatus(READY);
        readyList->SortedInsert((void *)currentThread, currentThread->getPriority());
        this->Run(thread);
    }
}
```

**测试**

如果所有测试进程都在`Lab2Test2`函数中被创建，则当控制流离开`main`线程时，这三个进程会同时出现在readyList上。

```cpp
void Lab2Test2(){
    scheduler->setPolicy(STATICPRIORTY);

    Thread *t1 = new Thread("Thread0", 114);
    t1->Fork(Lab2Thread2, (void*)114);

    scheduler->Print();
}
```

为了解决这个问题，我想到了递归调用的方法。`Lab2Thread2`在运行中会创建优先级更高的进程`t`，当`t`的状态变为READY后，应该能将当前进程抢占下来。这里需要注意，NachOS中的时间不会主动流动；只有当调用`interrupt->OneTick()`时，时间才会增加1或10个tick。

```cpp
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
```

测试结果符合我们的预期，高优先级的进程的确能在状态变为READY时抢占低优先级的进程。然而，进程回收时会出现问题。

```
(30) [1] name=Thread0 Forking...
[handler] context switch (pr) prev->pr=114 new-pr=57
(50) [2] name=Thread Forking...
[handler] context switch (pr) prev->pr=57 new-pr=28
(70) [3] name=Thread Forking...
[handler] context switch (pr) prev->pr=28 new-pr=14
(90) [4] name=Thread Forking...
[handler] context switch (pr) prev->pr=14 new-pr=7
(110) [5] name=Thread Forking...
(130) [5] name=Thread Exiting...
...
```

**Challenge 1  时间片轮转算法**

**1.1 实现必要的数据结构**

由于我们这里不实现动态优先级，因此可以把记忆时间片的工作交给调度器。`lastCalledTick`会记录上一次发生上下文切换的时间，若距今超过`switchDuration`，则强行进行上下文切换。

```cpp
class Scheduler {
  public:
    ...
    // [lab2] rr
    void setSwitchDuration(int newduration);
    static void handleThreadTimeUp(int ptr_int);
  private:
  	...
    // [lab2] Round-Robin
  	int switchDuration;   // switchDuration may not be accurate
    int lastCalledTick;
    void inline resetCalledTick();
};
```

成员函数中增加了switchDuration的setter。

```cpp
// [lab2] Round-Robin
void Scheduler::setSwitchDuration(int newduration){ 
    ASSERT(switchDuration > TimerTicks);
    switchDuration = newduration; }
```

`Run`函数进行上下文切换。在切换前，我们需要记录当前的时间。

```cpp
void Scheduler::Run (Thread *nextThread){
    // [lab2] rr - resetCalledTick on thread switching
    resetCalledTick();
  	...
}

void inline Scheduler::resetCalledTick(){ scheduler->lastCalledTick = stats->totalTicks; }
```

然而这里出现了一个很大的问题——我不知道该怎么按时间触发上下文切换。NachOS中的timer实现限定了其功能——每隔固定时间执行函数，然而这显然不能满足分时间片轮转的需求。这时候我注意到，似乎`TimerInterruptHandler`是可以替换的！这里用`Scheduler::handleThreadTimeUp`这个函数替换了默认的`TimerInterruptHandler`。

```cpp
/* from code/threads/system.cc */
...
else if (!strcmp(*argv, "-rr")) {
  // Start Round-Robin Timer
  ASSERT(argc > 1);
  enableRoundRobin = TRUE;
  argCount = 2;
}
```

```cpp
if (randomYield){               // start the timer (if needed)
  timer = new Timer(TimerInterruptHandler, 0, randomYield);
} else if (enableRoundRobin){
  // [lab2] RR
  // create customed TimerInterruptHandler with random disabled
  // hack here: pass &scheduler as arg
  timer = new Timer(scheduler->handleThreadTimeUp, (int)scheduler, false);
}
```

在调度器的策略不为ROUND_ROBIN时，`handleThreadTimeup`的功能与`timerInterruptHandler`别无二致。而当策略为ROUND_ROBIN时，当距上一次上下文切换的时间超过`switchDuration`时，会触发上下文切换。

```cpp
void Scheduler::handleThreadTimeUp(int ptr_int){
    // [lab2] hack here: pass scheduler as int
    Scheduler* curr_sche = (Scheduler*) ptr_int;

    if (curr_sche->policy!=ROUND_ROBIN){
        if (interrupt->getStatus() != IdleMode) {
            // Not on RR, act like TimerInterruptHandler
            printf("[handler] context switch (non-rr) \n");
            interrupt->YieldOnReturn();
        }
        return;
    }

    int passedDuration = stats->totalTicks - curr_sche->lastCalledTick;
    if(passedDuration >= curr_sche->switchDuration){
        /* from interrupt.cc */
        if (interrupt->getStatus() != IdleMode) {
            printf("[handler] context switch (rr) duration=%d\n", passedDuration);
            interrupt->YieldOnReturn();
        }
    }
}
```

**测试**

`Lab2Thread3`的功能非常简单：运行一定的时间（调用OneTick使时间流动），打印线程状态。

```cpp
void Lab2Thread3(int ticks){
    int cnt_ticks = 0;
    while(ticks--){
        printf("(%d) [%d] name=%s Running for %d ticks\n", 
            stats->totalTicks, currentThread->getThreadId(), currentThread->getName(), 10*cnt_ticks++);
        interrupt->OneTick();  // extend life for 10 ticks
    }
}
```

`Lab2Test3`设置三个线程，分别运行300、80、200个ticks，从中可以观察时间片轮转的功能是否正常。

```cpp
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
```

可以看到，测试结果总体来说符合我们的预期。受NachOS的Timer实现的限制，`handleThreadTimeUp`每隔100个ticks才能运行一次，因此是否超出时间片的检查可能是延后的。some线程第一次运行了160个ticks才下CPU，而naive线程运行了110个ticks。

```
(50) [1] name=some Running for 0 ticks
...
(190) [1] name=some Running for 140 ticks
[handler] context switch (rr) duration=160
(210) [2] name=times Running for 0 ticks
...
(280) [2] name=times Running for 70 ticks
(300) [3] name=naive Running for 0 ticks
...
(390) [3] name=naive Running for 90 ticks
[handler] context switch (rr) duration=110
(410) [1] name=some Running for 150 ticks
...
(490) [1] name=some Running for 230 ticks
[handler] context switch (rr) duration=100
(510) [3] name=naive Running for 100 ticks
...
(590) [3] name=naive Running for 180 ticks
[handler] context switch (rr) duration=100
(610) [1] name=some Running for 240 ticks
...
(660) [1] name=some Running for 290 ticks
(680) [3] name=naive Running for 190 ticks
```

#### 遇到的困难以及收获

**1. 强制类型转换**

在`Thread.h`中，对Fork函数的定义如下：

```cpp
void Fork(VoidFunctionPtr func, void *arg);  
```

而`VoidFunctionPtr`的定义如下：

```cpp
typedef void (*VoidFunctionPtr)(int arg); 
```

NachOS的运行环境是32位，也就是说`void *`可以被强制转换为`int`。我们可以通过强制类型转换的方式，把一个指针传给被Fork的线程。以下给出一个示例程序：

```cpp
void thu(int ptr_int){
  class_* ptr_to_pass = (class_*) ptr_int
}
...
t->Fork(thu, (void*)ptr_to_pass)
```

**2. 线程的回收问题 **

在进行测试时，我意外地发现在不少情况下运行结束的线程并不会被回收，而是会进入BLOCKED状态。

关于这一点，某位热心的同学给出了答案：

> 若一个线程刚刚被创建（例如Fork())，其栈顶元素是ThreadRoot的入口；这将导致SWITCH无法返回Run函数，导致已经完成的线程无法回收。

#### 对课程或Lab的意见和建议

1. 希望实验说明更够更为详细，您们可以适当参考UC Berkeley的NachOS Lab Handout。篇幅不一定像UC的十多页那么长，只要能具体而非笼统地说明您们希望我们完成的任务即可。

2. 希望助教能够提供一些实现的思路和实现中可能遇到的问题，帮助学生思考。在Lab中可能会需要用到一些实用的小Trick，例如强制类型转换，自己想可能会想不到。

#### 参考文献

[Linux Kernel 2.6.38 Source](https://elixir.bootlin.com/linux/v2.6.38/source/include/linux/sched.h)

[Linux Kernel 2.4.30 Source](https://elixir.bootlin.com/linux/2.4.30/source/include/linux/sched.h)

[谈谈调度 - Linux O(1)](https://zhuanlan.zhihu.com/p/33461281)
在分析xv6的进程机制时，我并不打算按照一个个文件、一个个函数的方式静态地来分析。本文会先介绍xv6实现进程机制必需的数据结构，然后借由分析xv6进程的整个生命周期，按照操作的时间顺序来阐释xv6操作系统对进程的管理机制。

#### 1. 数据结构

**1.1**  常量
```cpp
/* from param.h */
#define NPROC        64  // maximum number of processes
#define KSTACKSIZE 4096  // size of per-process kernel stack
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
...
```

`param.h`中的这些语句定义了xv6操作系统重要的常量。以上的代码说明了xv6最多支持64个进程（NPROC=64），每个进程最多打开16个文件（NOFILE=16），整个操作系统最多打开100个文件（NFILE=100）。

**1.2**  内存地址空间

xv6为每个进程实现了不同的页表，如下图所示。0x80000000以下的部分是用户栈，包括text，data，堆和栈；而内核的指令和数据也会被映射到每个进程的地址空间中0x80100000以上的部分。当处理系统调用时，程序实际在当前进程的内核地址空间内执行，这样的设计使内核态程序能访问当前进程用户态的数据；而进程在用户态运行时无法访问内核栈，即使用户栈被破坏也不会影响内核的运行。



<img src="/Users/Apple/Library/Application Support/typora-user-images/image-20201029201625347.png" alt="image-20201028091408834" style="zoom:33%;" />

<center>[图1] XV6中进程的地址空间</center>


```cpp
/* from memlayout.h */
#define EXTMEM  0x100000            // Start of extended memory
#define PHYSTOP 0xE000000           // Top physical memory
#define DEVSPACE 0xFE000000         // Other devices are at high addresses
```

**1.3**  xv6的PCB实现

在xv6中，`proc`结构体用于维护进程的状态。

```cpp
/* from proc.h */
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};
```

我们注意到以下这些变量：

- `pgdir` 指向页表的指针
- `kstack` 进程的内核栈底指针
- `procstate` 进程状态

```cpp
/* from proc.h */
enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
```
- `trapframe`（用户态/内核态切换时由x86硬件和中断处理程序推入栈中，上一次代码阅读报告已有所阐释）
```cpp
struct trapframe {
  // registers as pushed by pusha
  uint edi;
  uint esi;
	...
  ushort gs;
  ushort fs;
  ushort es;
  ushort ds;
  uint trapno;
	...
};
```
- `context`保存切换上下文时必须要保存的寄存器

  不需要保存段寄存器与$\%eax, \%ecx, \%edx$的值，段寄存器的值不会改变，而另外三个寄存器的值应该由caller保存。
```cpp
/* from proc.h */
struct context {
  uint edi, esx, ebx, ebp, eip;
};
```

全局变量ptable中包括了一个spinlock（用来保证修改操作的原子性）和proc数组（所有PCB构成的数组）。由于spinlock的机制不是本文讲解的重点，因此下文的`acquire(lock)`与`release(lock)`语句都会略过。


```cpp
/* from proc.h */
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;
```

#### 2. xv6中进程的状态

<img src="/Users/Apple/Library/Application Support/typora-user-images/image-20201030181234990.png" alt="image-20201030181234990" style="zoom: 23%;" />

<center>[图2]  XV6中进程的状态转换</center>

在xv6中，进程有6种状态：

- UNUSED 进程刚刚被创建，尚未分配（类似于NachOS中的JUST_CREATED）
- EMBRYO 进程已分配
- RUNNABLE 进程等待运行（类似于NachOS中的READY），调度器只会处理状态为RUNNABLE的进程
- RUNNING 进程正在运行
- SLEEPING 进程正在被阻塞（例如等待IO设备相应）
- ZOMBIE 进程已退出，但没有被回收

#### 3. 创建一个进程

**3.1** userinit()

事实上`userinit`函数在xv6的启动过程中发挥了举足轻重的作用。当xv6启动后，bootloader被加载到内存中。随后计算机开始运行`entry.S`，初始化页表，并调用`main.c`中定义的`main`函数。`main`函数在完成对设备、文件系统的初始化后，便调用`userinit`函数创建第一个用户进程。

```cpp
void userinit(void) {
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  p = allocproc();  // allocproc() 分配proc和内存空间
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)  // 初始化内核页表
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);  // 初始化用户页表
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));  // 设置trapframe
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
	...
  acquire(&ptable.lock);
  p->state = RUNNABLE;  // 修改进程状态
  release(&ptable.lock);
}
```

我们可以看到，`userinit`函数首先在ptable中为这个进程找到一个空闲的proc，分配后修改进程状态为EMBRYO；随后初始化内存地址空间和页表，设置trapframe；最后修改进程状态为RUNNABLE，交给`scheduler`处理。（相关的内容将会在调度一节详细讲解）

**3.1.1** allocproc()

```cpp
static struct proc* allocproc(void) {
  struct proc *p;
  char *sp;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;
  release(&ptable.lock);
  return 0;
found:
  p->state = EMBRYO;  // 修改状态为EMBRYO, 已分配proc, 未初始化内存
  p->pid = nextpid++;
  release(&ptable.lock);
```

`allocproc`函数首先在ptable中寻找一个没有被占用的proc，将其状态改为EMBRYO，尝试进行初始化；若没有状态为UNUSED的proc，则返回0。xv6用了一个较为简单的实现顺序遍历了整个ptable数组；然而在xv6中最多同时存在64个进程，因此顺序遍历应该不会造成很大的性能瓶颈。


``` cpp
  // 分配内核栈
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;
  // trapret
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;
  sp -= 4;
  *(uint*)sp = (uint)trapret;
	// forkret
  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  return p;
}
```

xv6采取了一种巧妙的方式使无论是`fork()`创建的进程还是`userinit()`创建的进程都能正常退出。如果我们观察`allocproc`函数建立的内核栈，会发现这样的设计能够使`fork()`创建的进程先执行`forkret`恢复内核寄存器，再执行 `trapret`恢复用户寄存器。

<img src="https://th0ar.gitbooks.io/xv6-chinese/content/pic/f1-3.png" alt="figure1-3" style="zoom: 50%;" />

<center>allocproc建立的内核栈</center>

**3.2** fork()

```cpp
/* from proc.c */
int fork(void){
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  if((np = allocproc()) == 0){
    return -1;
  }
```

`fork`函数首先运行`allocproc`，分配proc，初始化内核栈。

```cpp
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  np->tf->eax = 0;
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  pid = np->pid;
  acquire(&ptable.lock);
  np->state = RUNNABLE;
  release(&ptable.lock);
  return pid;
}
```

以上的代码除了复制父进程p的内存空间和PCB之外，还做了以下几件事：

- 清空新进程的内核栈（`kfree(np->kstack);np->kstack = 0`）
- 将$\%eax$设为0，这样fork()在子进程中的返回值就是0（`np->tf->eax = 0`）
- 将当前进程的状态修改为RUNNABLE，调度器将会对这个进程进行调度。

#### 4. 进程上/下CPU

**4.1** 进程调度

注：由于xv6页表机制、上下文切换与调度算法并不是本文讨论的重点，因此暂且将进程调度的讨论范围局限于`scheduler`函数。

```cpp
/* from proc.c */
void scheduler(void){
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;  // cpu上没有进程在运行
```

xv6中使用cpu结构体表示当前cpu的执行状态（这里需要注意xv6有多核心支持）。在cpu结构体中定义了proc指针用于表示当前运行的进程，若定义为0（即NULL）则表示当前没有进程运行。

```cpp
  for(;;){
    sti();  // 在当前cpu上启用中断
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
```

当调度器运行时，会在ptable数组中顺序遍历有无状态为RUNNABLE的进程。（这一实现简单而低效，但对进程数上限为64的xv6来说问题不大）我们注意到，调度器只会处理状态为RUNNABLE的进程，因此一个进程需要调度器处理时，必须将其状态设置为RUNNABLE。

```cpp
      c->proc = p;  // cpu切换到进程p
      switchuvm(p);  // 将页表切换为进程p的页表
      p->state = RUNNING;  // p的状态修改为RUNNING
      swtch(&(c->scheduler), p->context);  // swtch将控制流切换到进程p
      switchkvm();  // 执行完成，将页表切换为内核页表
      c->proc = 0;  // cpu上没有进程在运行
    }
    release(&ptable.lock);
  }
}
```

这里需要注意到，在`swtch`函数将控制流切换到进程p后，进程p会释放spinlock，并在返回到`scheduler`函数之前获取spinlock。进程运行完成后修改状态也是由其自身而非调度器完成。

**4.2** yield()

```cpp
void yield(void){
  acquire(&ptable.lock); 
  myproc()->state = RUNNABLE;
  sched();  // 进行检查和保存中断状态后，调用当前cpu的scheduler
  release(&ptable.lock);
}
```

进程主动让出CPU时，会调用yield()。yield()将当前进程的状态修改为RUNNABLE，进行检查和保存中断状态后，调用当前cpu的调度器。

*问题*：参考上文scheduler的实现，调度器按顺序遍历ptable数组；但是如果ptable中编号靠前的进程与编号靠后的进程同时处于RUNNABLE状态，那无论是否编号靠前的进程是否主动调用yield()让出CPU，都是它先运行吗？

**4.3** sleep()

注：这一部分省略了互斥机制的实现。

```cpp
p->chan = chan;  // 设置等待队列
p->state = SLEEPING;  // 将状态设置为SLEEPING
sched();  // 调用调度器
p->chan = 0;  // 等待队列为0
```

`sleep`函数会在将进程状态设为SLEEPING之后主动调用调度器。其中等待队列的实现对于提高进程生产者/消费者队列的效率大有裨益。以IDE驱动程序为例，当进程等待从硬盘中读取数据时，采用轮询会大量消耗CPU资源。中断队列的实现可以一次性唤醒所有因等待硬盘数据而睡眠的进程，从而可以让其他进程在这段时间运行。

**4.4** wakeup()

```cpp
static void wakeup1(void *chan){
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}
```

wakeup()是wakeup1()的wrapper函数，增加了获取spinlock和释放spinlock的部分。`wakeup1`会将ptable中所有正在睡眠，等待队列为chan的进程状态设为RUNNABLE。需要注意，wakeup()函数并不会主动调用调度器，因此唤醒操作可能不是实时的。


#### 5. 退出一个进程

**5.1** exit()

```cpp
void exit(void){
  ...
  wakeup1(curproc->parent);  // 唤醒正在睡眠的父进程
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;  // 救不了你了，找init去
      if(p->state == ZOMBIE)
        wakeup1(initproc);  // 唤醒init进程，回收ZOMBIE
    }
  }
  curproc->state = ZOMBIE;
  sched();  // 调用调度器
  panic("zombie exit");  // exit什么也不返回，若返回则说明调度出错
}
```

进程在退出时会唤醒父进程；若有待回收的子进程，则会把它们交给init。（猜想唤醒操作是为了保证回收的实时性）之后exit函数会把当前进程的状态置为ZOMBIE，等待父进程回收。

**5.2** wait()

wait()函数会在父进程中等待一个子进程退出。

```cpp
int wait(void){
  ...
  for(;;){
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
```

wait()函数首先会顺序遍历ptable，寻找有无ZOMBIE状态的子进程。

```cpp
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }
```

当找到子进程时，则会重置子进程的PCB，释放子进程的内存空间，完成资源回收。

```cpp
	 if(!havekids || curproc->killed){  // 找不到子进程或者子进程被kill了，回收不了
      release(&ptable.lock);
      return -1;
    }
    sleep(curproc, &ptable.lock);  // 子进程exit()时会唤醒父进程
  }
}
```

这里的sleep对应子进程还没有进入ZOMBIE状态的情况：父进程会进入睡眠，直到被子进程的exit()函数唤醒。

**5.3** kill()

```cpp
int kill(int pid){
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){  // 遍历ptable数组
    if(p->pid == pid){  // pid 符合
      p->killed = 1;  // 设置killed
      if(p->state == SLEEPING)
        p->state = RUNNABLE;  // 唤醒正在睡眠的进程
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
```

`kill`函数顺序遍历整个ptable，找到pid符合的进程，将其killed置为true；若该进程正在睡眠，则将其唤醒。（我猜想是为了保证kill操作的实时性，否则该进程可能长时间维持睡眠状态）

*问题*：这里仅将`p->killed`设置为`true`，怎么保证进程一定会退出？

事实上xv6采用了一系列的机制保证killed设置为true时进程会尽快退出。以下引自参考资料：

> xv6 谨慎地在调用 sleep 时使用了 while 循环，检查 p->killed 是否被设置了，若是，则返回到调用者。调用者也必须再次检查 p->killed 是否被设置，若是，返回到再上一级调用者，依此下去。最后进程的栈展开（unwind）到了 trap，trap 若检查到 p->killed 被设置了，则调用 exit 终结自己。

#### References

[第一个进程 - xv6中文手册](https://th0ar.gitbooks.io/xv6-chinese/content/content/chapter1.html)

[调度 - xv6中文手册](https://th0ar.gitbooks.io/xv6-chinese/content/content/chapter5.html)

[xv6 source](https://github.com/mit-pdos/xv6-public)

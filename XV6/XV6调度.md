**1. 进入调度过程**

xv6操作系统的调度算法并不是抢占式的——先被调度上CPU的进程默认会一直运行，直到线程调用`yield()`，`sleep()`或`exit()`主动让出CPU，才会进入进程调度。

我们可以观察到，`yield`函数在更改进程的状态之后，调用了`sched`进行调度。

```cpp
void yield(void){
  ...
  myproc()->state = RUNNABLE;
  sched();
}
```

`exit`函数在更改进程的状态之后，调用了`sched`进行调度。（`exit`函数不会返回）

```cpp
void exit(void){
  ...
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}
```

`sleep`函数在更改进程的状态之后，调用了`sched`进行调度。当控制流回到这个进程时，则会解除睡眠状态并继续执行。


```cpp
void sleep(void *chan, struct spinlock *lk) {
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  sched();
  // Tidy up.
  p->chan = 0;
	...
}
```

**2. sched**

`sched`是调用调度器的入口。

我们在调用`scheduler`函数进行上下文切换之前，首先要检查：

- 是否设置ptable自旋锁（ptable.lock）

- 进程状态是否处在RUNNING（即没有通过`sleep`，`yield`，`exit`进入`sched`)

- 当前CPU是否在`puchcli`过程中（即正在执行读取当前CPU进程信息的`myproc`函数）

- 当前进程是否可被中断

这里需要注意，`scheduler`函数会在当前CPU上启用中断，因此我们需要保存当前CPU的中断状态`intena`，并在之后恢复。

```cpp
void sched(void) {
  int intena;  // Interrupt Enabled
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}
```

**3. scheduler**

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

当调度器运行时，会在ptable数组中顺序遍历有无状态为RUNNABLE的进程，之后调用`swtch`函数将控制流切换到这个进程。（这一实现简单而低效，但对进程数上限为64的xv6来说问题不大）

我们可以观察到xv6的调度算法并不是FIFO。事实上，当这一进程执行完毕，控制流回到`scheduler`后，将会从上一次调度的进程开始查找下一个——例如16号进程执行完成后，`scheduler`就会从17号进程开始查找。这一思想类似于轮转，能有效解决一个进程号较小的进程调用`yield`下CPU后又很快被调度上CPU，从而造成其他进程饥饿的问题。

我们再考虑多CPU的情况。在xv6中，`ptable`是全局变量。每一个CPU均拥有一个`scheduler`，而`p`是每一个`scheduler`函数的局部变量，CPU之间并不共享。这一实现较为简单，不过也不会造成什么问题——就算进程号较小的进程调用`yield`之后被另一个CPU调度，当前CPU的`scheduler`也会继续寻找下一个状态为RUNNABLE的进程，也不会造成饥饿。

```cpp
struct cpu {
  ...
  struct context *scheduler;
  ...
}
```

我们注意到，调度器只会处理状态为RUNNABLE的进程，因此一个进程需要调度器处理时，必须将其状态设置为RUNNABLE。

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

**4. swtch**

`swtch`函数定义在`defs.h`中，用于进程调度过程中的上下文切换。

```cpp
void  swtch(struct context**, struct context*);
```

`swtch`函数接受两个参数：旧进程上下文结构体的指针，和新进程的上下文结构体。上下文结构体的定义如下：

```cpp
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};
```

旧进程上下文结构体的指针保存在$4(\%esp)$中，新进程的上下文结构体保存在$8(\%esp)$中。

```assembly
.globl swtch
swtch:
  movl 4(%esp), %eax  # struct context**
  movl 8(%esp), %edx  # struct context*
```

将旧进程需要保存的寄存器push到栈中，和原先栈中的返回地址构成了上下文结构体。

```assembly
  # Save old callee-saved registers
  pushl %ebp
  pushl %ebx
  pushl %esi
  pushl %edi
```

更新旧进程的上下文结构体（这里直接传递了指针），并将栈指针$\%esp$切换到新进程。（新进程原先的栈顶也是上下文结构体）

```assembly
  # Switch stacks
  movl %esp, (%eax)
  movl %edx, %esp
```

将新进程需要保存的寄存器从栈中弹出，当前的栈顶就是新进程的$\%eip$。`ret`指令执行后，控制流被切换到新进程。

```assembly
  # Load new callee-saved registers
  popl %edi
  popl %esi
  popl %ebx
  popl %ebp
  ret
```


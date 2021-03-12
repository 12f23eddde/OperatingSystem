1. spinlock.h

   ```cpp
   struct spinlock {
     uint locked;       // Is the lock held?
     // For debugging:
     char *name;        // Name of lock.
     struct cpu *cpu;   // The cpu holding the lock.
     uint pcs[10];      // The call stack (an array of program counters)
                        // that locked the lock.
   };
   ```
   
   在spinlock结构体中，实质上有意义的就是`locked`变量，`locked = 1`表示加锁，`locked = 0`表示解锁。除此之外，spinlock中还含有一些用于调试的信息——锁的名
   
   称，调用锁的CPU，锁的程序调用栈（这里以PC的形式表示）。
   
2. spinlock.c

   （1）initlock

   ```cpp
   void initlock(struct spinlock *lk, char *name){
     lk->name = name;
     lk->locked = 0;
     lk->cpu = 0;
   }
   ```

   initlock函数负责初始化锁，`lk->locked = 0`将锁的状态置为解锁。initlock还设置了锁的名称及占有锁的CPU。

   （2） holding

   ```cpp
   int holding(struct spinlock *lock){
     int r;
     pushcli();
     r = lock->locked && lock->cpu == mycpu();
     popcli();
     return r;
   }
   ```
   
   `holding`函数判断当前锁是否被锁住，并且占有锁的CPU是否是当前进程运行的CPU。
   
   我们这里注意到，`r = lock->locked && lock->cpu == mycpu()`这个语句是不能被中断的。
   
     因此在spinlock的读/写过程有较大几率会发生竞争，需要保护spinlock。这里xv6用了一个简单粗暴的方法——关中断。在读锁的状态前，`pushcli`关中断，读取完成后`popcli`开中断，从而保证这一过程不会被打断。

   ```cpp
   void pushcli(void){
     int eflags;
   
     eflags = readeflags();
     cli();
     if(mycpu()->ncli == 0)
       mycpu()->intena = eflags & FL_IF;
     mycpu()->ncli += 1;
   }
   ```

   `pushcli`函数会读取执行它之前当前CPU中断的开关状态，稍后保存到`mycpu()->intena`，随后调用x86的`cli`汇编指令关闭当前CPU上的中断。我们这里注意到，`pushcli`这一过程是可以嵌套的，每调用一次`pushcli`就会将`mycpu()->ncli`加1。

    ```cpp
    void popcli(void){
    if(readeflags()&FL_IF)
    panic("popcli - interruptible");
    if(--mycpu()->ncli < 0)
    panic("popcli");
    if(mycpu()->ncli == 0 && mycpu()->intena)
    sti();
    }
    ```

   `popcli`函数需要确保当前CPU已经关闭了中断，并且之前执行过`puchcli`（否则关中断没有任何意义）。随后`popcli`会将`mycpu()->ncli`减1，当`mycpu()->ncli`减少到0（也就是`popcli`和`pushcli`能够匹配），并且在第一次调用`pushcli`前当前CPU中断状态为开时，`popcli`会调用x86的`sti`汇编指令开启当前CPU上的中断。

   （3）acquire
   
   `acquire`函数使用关中断的方式，保证进程在加锁的过程中以及进入临界区的过程中不会被中断。
   
   ```cpp
   void acquire(struct spinlock *lk){
     pushcli(); // disable interrupts to avoid deadlock.
     if(holding(lk))
     panic("acquire");
   
     // The xchg is atomic.
     while(xchg(&lk->locked, 1) != 0);
   
     // Tell the C compiler and the processor to not move loads or stores
     // past this point, to ensure that the critical section's memory
     // references happen after the lock is acquired.
     __sync_synchronize();
   
     // Record info about lock acquisition for debugging.
     lk->cpu = mycpu();
     getcallerpcs(&lk, lk->pcs);
   }
   ```
   
   然而仅仅关闭中断并不能保证不会出现死锁。xv6内核支持多CPU，进程之间存在并行关系。如果运行在CPU0，1上的两个进程同时执行`acquire`，这两个进程可能都认为`locked`的值为0，从而同时占用了锁，造成死锁。
   
   ```cpp
   static inline uint xchg(volatile uint *addr, uint newval){
     uint result;
   
     // The + in "+m" denotes a read-modify-write operand.
     asm volatile("lock; xchgl %0, %1" :
                  "+m" (*addr), "=a" (result) :
                  "1" (newval) :
                  "cc");
     return result;
   }
   ```
   
   为此，xv6采用了x86的`xchg`指令，这一指令在硬件设计时就保证了在多CPU环境中的原子性。`xchg`指令原子地交换一个寄存器和内存字的值，如果返回的内存字值为1，代表有CPU或者进程占用锁；如果xchg返回为0，代表目前没有人占用锁，将锁置1。
   
   `acquire`函数循环地调用`xhcg`，直到占用锁成功为止。（这一过程可能会导致busy waiting）
   
   现在x86处理器支持乱序执行，在实际运行程序时，汇编指令的顺序会被打断，以获得更高的性能。然而这可能导致在获取了锁之后，在汇编层面上依然有进程正在处理临界区的数据。`__sync_synchronize()`函数告诉编译器和处理器，等待所有对临界区的内存访问结束后再运行接下来的指令，以免在汇编层面上造成两个进程同时访问临界区的情况。
   
   在获得锁之后，`getcallerpcs`函数会记录当前获取锁的CPU号和调用栈（这不是本文讨论的重点，因此在此略去），用于调试。

   （4）release
   
   `release`函数用于进程出临界区之后释放锁。
   
   首先，`release`会判断锁是否被当前CPU占用。若没有被占用，则表示有进程错误地释放了锁，触发`kernel panic`。
   
   之后，`release`会清除调试信息，并调用`__sync_synchronize()`，等待对临界区的内存访问结束。

   ```cpp
   // Release the lock.
   void release(struct spinlock *lk){
     if(!holding(lk))
       panic("release");
   
     lk->pcs[0] = 0;
     lk->cpu = 0;
   
     // Tell the C compiler and the processor to not move loads or stores
     // past this point, to ensure that all the stores in the critical
     // section are visible to other cores before the lock is released.
     // Both the C compiler and the hardware may re-order loads and
     // stores; __sync_synchronize() tells them both not to.
     __sync_synchronize();
   
     // Release the lock, equivalent to lk->locked = 0.
     // This code can't use a C assignment, since it might
     // not be atomic. A real OS would use C atomics here.
     asm volatile("movl $0, %0" : "+m" (lk->locked) : );
   
     popcli();
   }
   ```
   
   为了保证解锁过程的原子性，这里采用了汇编指令`movl`代替`lk->locked = 0`。
   
   随后，`release`调用`popcli`选择性地开中断。（详见上文对`popcli`的说明）

3. sleeplock.h

   sleeplock的数据结构中，除了`locked`变量外，还增加了自旋锁`lk`，用于保护`locked`变量。

   ```cpp
   struct sleeplock {
     uint locked;       // Is the lock held?
     struct spinlock lk; // spinlock protecting this sleep lock
     
     // For debugging:
     char *name;        // Name of lock.
     int pid;           // Process holding lock
   };
   ```

4. sleeplock.c

   （1）init

   ```cpp
   void initsleeplock(struct sleeplock *lk, char *name){
     initlock(&lk->lk, "sleep lock");
     lk->name = name;
     lk->locked = 0;
     lk->pid = 0;
   }
   ```

   在初始化sleeplock时，`initsleeplock`会初始化自旋锁`lk`，并初始化调试信息。

   （2）holding
   
   ```cpp
   int holdingsleep(struct sleeplock *lk){
     int r;
     
     acquire(&lk->lk);
     r = lk->locked && (lk->pid == myproc()->pid);
     release(&lk->lk);
     return r;
     }
   ```

   sleeplock的holding函数与spinlock较为相似，只是用`acquire`和`release`替代了`pushcli`和`popcli`，保证获取锁状态这一过程的原子性。
   
   （3）acquire
   
   ```cpp
   void acquiresleep(struct sleeplock *lk){
     acquire(&lk->lk);
     while (lk->locked) {
       sleep(lk, &lk->lk);
     }
     lk->locked = 1;
    lk->pid = myproc()->pid;
     release(&lk->lk);
   }
   ```
在获取sleeplock之前，`acquiresleep`会锁住自旋锁`lk`。
   
   如果有进程占用了sleeplock，`acquiresleep`会循环让它们进入睡眠，直到没有进程占用锁为止。
   
我们再回顾一下`sleep`函数的定义：
   
```cpp
   void sleep(void *chan, struct spinlock *lk)
   ```
   
  可以看到，`chan`是`sleeplock`的指针，一个进程睡眠的channel事实上就对应着一把sleeplock。
   
   随后`acquiresleep`修改sleeplock的状态，释放自旋锁`lk`，并记录调用它的进程的pid。
   
   （4）release
   
```cpp
   void releasesleep(struct sleeplock *lk){
     acquire(&lk->lk);
     lk->locked = 0;
     lk->pid = 0;
     wakeup(lk);
     release(&lk->lk);
   }
   ```
   
   在释放sleeplock前，`releasesleep`会锁住自旋锁`lk`。
   
   随后，`releasesleep`会清除现在锁的状态，并唤醒所有在sleeplock上睡眠的进程。这里不需要等待所有的进程都被唤醒。
   
   最后，`releasesleep`会释放自旋锁`lk`。


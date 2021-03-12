

# xv6代码阅读报告：中断与系统调用

注：在本文中涉及对异常/中断/系统调用的处理，然而三者的定义边界较为模糊，实现上也大多有相似之处；xv6开发者习惯（其实是Unix的命名习惯）在大多数时候将其统称为trap，本文则在大多数时候将其统称为中断；当尝试对异常/中断/系统调用进行区分时，均会用*斜体*表示。

x86 允许 256 个不同的中断。中断 0-31 被定义为软件异常，比如除 0 错误和访问非法的内存页。xv6 将中断号 32-63 映射给硬件中断，并且用 64 作为系统调用的中断号。traps.h中定义了xv6操作系统所有的trap类型：

```c
/* from traps.h */
// 异常
#define T_DIVIDE         0      // 除0错误
#define T_DEBUG          1      // DEBUG
#define T_NMI            2      // 不可屏蔽的中断
...
// 系统调用
#define T_SYSCALL       64      // xv6选择了64号中断作为系统调用的入口
...
// 中断
#define IRQ_TIMER        0			// 时钟中断
#define IRQ_KBD          1      // 键盘中断
#define IRQ_COM1         4      // COM中断
#define IRQ_IDE         14      // IDE中断
...
```



### 一、初始化中断向量表

**SETGATE宏中描述了xv6中断描述符（Interrupt Descriptor）的结构：**

```c
/* from mmu.h */
#define SETGATE(gate, istrap, sel, off, d)                \
{                                                         \
  (gate).off_15_0 = (uint)(off) & 0xffff;                \   /* 段偏移 */
  (gate).cs = (sel);                                      \  /* 段选择 */
  (gate).args = 0;                                        \
  (gate).rsv1 = 0;                                        \
  (gate).type = (istrap) ? STS_TG32 : STS_IG32;           \  /* 区分中断与陷入 */
  (gate).s = 0;                                           \
  (gate).dpl = (d);                                       \  /* 调用中断处理程序所需的特权级 */
  (gate).p = 1;                                           \
  (gate).off_31_16 = (uint)(off) >> 16;                  \   /* 段偏移 */
}
#endif
```

**cs, off:**  包括中断处理程序的内存地址（段寻址）

**type: ** 区分*中断*与*陷入*的type位。由于*中断*不能被其它*中断*打断，而*系统调用*可以被其它*系统调用*打断，因此处理type=0的中断时，处理器会修改 $\%eflags$寄存器中的中断标识符(IF)，屏蔽中断；而type=1时则不会修改。

**dpl:**  调用中断处理程序所需的特权级。与Linux类似，xv6仅适用两个x86的特权模式——0为内核模式，3为用户模式。

**main.c的main()函数调用了tvinit()函数，初始化中断向量表(idt)：**

```c
/* from trap.c */
void tvinit(void){
  int i;
  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  	// 其中idt[i]对应中断向量表的第i项
  	// 而vectors[i]对应第i号中断的中断处理程序入口(定义在vectors.S中，由vectors.pl生成)
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  initlock(&tickslock, "time");  // xv6采用自旋锁(spinlock)保护中断上下文
}
```

tvinit()函数初始化IDT中所有的中断处理程序入口，并将IDT中*中断*的特权级设置为内核级，将*系统调用*（64号中断）的特权级设置为用户级，最后初始化自旋锁（鉴于篇幅限制，且spinlock的原理并不是本文讨论的重点，因此略过对spinlock.c的分析）。



### 二、中断处理过程

**1.  堆栈切换（硬件操作）**当特权级从用户模式向内核模式转换时，内核不能使用用户的栈，因为用户进程可能是恶意的或者包含了一些错误，使得用户的 %esp 指向一个不是用户内存的地方，此时就需要进行栈切换。硬件会从任务段描述符中加载 $\%esp$ 和 $\%ss$，把先前$\%ss, \%esp$的值压入新的栈中。

**2.  保存上下文（硬件操作）**将$\%eflags, \%cs, \%eip$压栈，并清除$\%eflags$的一些位，然后设置$\%cs$和$\%eip$为中断描述符中的值。对于某些中断（例如10号中断），此时$errno$会被压入栈。

**3.  控制流进入中断处理程序（于vectors.S中定义）**对于$errno$已经被压入栈的中断，此时中断处理程序不会压入$errno$。以下给出vectors.S中中断处理程序汇编的示例：

```assembly
# from vectors.S
# handlers
.globl alltraps
.globl vector0
vector0:
  pushl $0  /* errno */
  pushl $0  /* trapno */
  jmp alltraps
 ...
.globl vector10
vector10:  
  pushl $10  /* trapno */
  jmp alltraps
```

**4.  所有中断均进入alltraps进行处理（定义于trapasm.S）**

```assembly
# from trapasm.S
.globl alltraps
alltraps:
  # Build trap frame.
  pushl %ds
  pushl %es
  pushl %fs
  pushl %gs
  pushal
```

alltraps首先将$\%ds$，$\%es$，$\%fs$，$\%gs$和其它通用寄存器压入栈，现在trapframe结构体已经建立（在x86.h中定义，与trapasm.S中的压栈顺序对应）。

```c
/* from x86.h */
struct trapframe {
  // registers as pushed by pusha
  uint edi;
	...
  uint eax;
  // rest of trap frame
  ushort gs;
  ...
  uint trapno;
	...
  // below here defined by x86 hardware
  uint err;
  uint eip;
  ushort cs;
	...
  // below here only when crossing rings, such as from user to kernel
  uint esp;
  ushort ss;
  ...
};
```

设置段寄存器，并调用函数trap，参数为%esp（为当前的栈顶内存地址，在trap.c中体现为struct trapframe *tf结构体指针）

```assembly
# from trapasm.S
  # Set up data segments.
  movw $(SEG_KDATA<<3), %ax
  movw %ax, %ds
  movw %ax, %es

  # Call trap(tf), where tf=%esp
  pushl %esp
  call trap
  addl $4, %esp
```

**5. 控制流进入trap函数（tf->trapno为中断号）**

**5.1. 系统调用的处理过程 **

  ``` c
/* from trap.c */
void trap(struct trapframe *tf){
  /* 处理系统调用 */
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }
  ```

trap函数先检查中断号是否是64（系统调用）。若是系统调用且没有进程没有被kill，则调用syscall函数处理系统调用。

xv6的系统调用号定义在 syscall.h：

```c
/* from syscall.h */
#define SYS_fork    1
#define SYS_exit    2
#define SYS_wait    3
#define SYS_pipe    4
```

所有系统调用函数的入口被统一保存在syscalls[]函数指针数组中：


```c
/* from syscall.c */
static int (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
...
```

syscall函数从$\%eax$中获取系统调用号， 若系统调用号有效，则执行对应的syscall函数, 将结果保存到$\%eax$；否则则返回-1。

```c
/* from syscall.c */
void syscall(void){
  int num;
  struct proc *curproc = myproc();

  num = curproc->tf->eax; /* 从%eax中获取系统调用号 */
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {  /* 系统调用号有效 */
    curproc->tf->eax = syscalls[num]();  /* 执行对应的syscall函数, 将结果保存到%eax */
  } else {  /* 系统调用号无效 */
    cprintf("%d %s: unknown sys call %d\n",
            curproc->pid, curproc->name, num);
    curproc->tf->eax = -1;  /* %eax = -1 */
  }
}
```

syscall.c中同样提供了一些工具函数argint()、argptr()、argstr()，帮助系统调用函数安全而遍历的获取参数。工具函数首先计算参数所在的内存地址—— $(\%esp)$为函数返回地址，$(\%esp)+4$开始为参数，因此第$n$个参数的地址为$(\%esp)+4$。除此之外，工具函数还会检查参数对应的内存地址是否合法，避免引发错误。

**5.2.  硬件中断的处理过程 **

```c
/* 处理硬件中断 */
switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){  /* timer需要获取、释放自旋锁 */
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();  /* 等待中断完成 */
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();  /* 等待中断完成 */
    break;
	...
```

trap函数接下来逐项检查中断号是不是符合traps.h中定义的硬件中断，并分情况进行处理。由于这一方面的细节过于冗杂，因此在此不再赘述。

多核处理器出现后，每一颗 CPU 都需要一个中断控制器来处理发送给它的中断，而且也得有一个方法来分发中断。这一方式包括两个部分：第一个部分是在 I/O 系统中的（IO APIC，ioapic.c），另一部分是关联在每一个处理器上的（局部 APIC，lapic.c）。xv6在设计时考虑到了中断的并行，因此在处理一些硬件中断时获取自旋锁以保证安全性。

**5.3.  其它情况的处理过程 **

其它情况包括*异常*（如除0错误）及未定义的中断号。若中断在内核地址空间内发生，则认为是内核运行出现了错误，抛出kernel panic；若中断在用户地址空间内发生，则认为是进程运行出现了错误，杀死进程。

```c
/* 处理其它情况 */
default:
  if(myproc() == 0 || (tf->cs&3) == 0){
    // 在内核地址空间内发生 -> kernel panic
    cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
            tf->trapno, cpuid(), tf->eip, rcr2());
    panic("trap");
  }
  // 在用户地址空间内发生 -> kill process
  cprintf("pid %d %s: trap %d err %d on cpu %d "
          "eip 0x%x addr 0x%x--kill proc\n",
          myproc()->pid, myproc()->name, tf->trapno,
          tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
}
```

**6. 恢复进程现场**

在中断处理程序运行结束之后，trapret（定义于trapasm.S）完成恢复$\%ds$，$\%es$，$\%fs$，$\%gs$和其它通用寄存器的工作。注意此时`  addl $0x8, %esp `将栈顶+8，等同于将中断号与错误号弹出栈，之后恢复到之前的控制流。

```assembly
# from trapasm.S
# Return falls through to trapret...
.globl trapret
trapret:
  popal
  popl %gs
  popl %fs
  popl %es
  popl %ds
  addl $0x8, %esp  # trapno and errcode
  iret
```





**References**

[xv6 source](https://github.com/mit-pdos/xv6-public)

[xv6中文手册 - 陷入，中断和驱动程序](https://th0ar.gitbooks.io/xv6-chinese/content/content/chapter3.html)


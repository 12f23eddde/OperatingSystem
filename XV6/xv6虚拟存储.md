##### 1. x86的虚拟内存机制

32位的x86处理器采用二级页表来进行地址翻译。对于一个进程而言，大多数的虚拟页并没有映射的物理页，因此采用二级页表可以不存储那些没有映射的页表项，显著减少内存占用。x86处理器中默认的页大小是4KB，对应虚拟地址和物理地址中12位的Offset；物理地址的前20位对应物理内存中的页号，虚拟地址的前20位被拆成两个10位，分别对应第一级页表、第二级页表中的索引。

<img src="https://hehao98.github.io/assets/xv6-pic/pagetable.png" alt="figure2-1" style="zoom:50%;" />

接下来我们来考虑地址翻译的过程。x86处理器中的虚拟内存管理硬件会从CR3寄存器中读出页目录的基地址，按照虚拟地址的前10位从页目录中的$2^{10}$项中选出一项，读出页表的地址。如果这一项有效（P=1)，硬件会继续按照虚拟地址的中间10位从页表中选出页表项。否则，硬件则会抛出异常。

我们注意到一个页表项的大小是32bit，而其中PPN只占了20bit，剩下的12bit可以另作他用。页目录、页表中都存储了一些权限位（例如页是否有效、是否只读、是否允许用户访问等），页目录中还增加了PS位，表示这个页是一个4MB大页，不需要继续进行地址翻译（xv6的初始化过程包含对大页的使用）。如果当前的访存行为与权限位不匹配，硬件会抛出异常。

##### 2. 系统引导（bootasm.S bootmain.c entry.S）

###### bootasm.S

xv6的启动过程从调用bootasm.S中的汇编代码开始。

```assembly
  # Switch from real to protected mode.  Use a bootstrap GDT that makes
  # virtual addresses map directly to physical addresses so that the
  # effective memory map doesn't change during the transition.
  lgdt    gdtdesc
  ...
  # Bootstrap GDT
.p2align 2                                # force 4 byte alignment
gdt:
  SEG_NULLASM                             # null seg
  SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)   # code seg
  SEG_ASM(STA_W, 0x0, 0xffffffff)         # data seg

gdtdesc:
  .word   (gdtdesc - gdt - 1)             # sizeof(gdt) - 1
  .long   gdt                             # address gdt
```

x86处理器启动时工作在Real Mode，Bootloader需要负责将其切换到Protected Mode。这里xv6先创建了一张临时的GDT（bootstrap GDT），然后通过lgdt指令加载页GDT。lgdt指令会执行以下过程，加载GDT基地址和GDT的大小：

```
GDTR(Limit) SRC[0:15];
GDTR(Base)  SRC[16:47];
```

在这里Limit对应gdtdesc - gdt - 1，Base对应gdt。执行到这里，GDT中包含的元素如下：

| 索引 | 条目       | 内容                                    |
| ---- | ---------- | --------------------------------------- |
| 0    | 空         | `SEG_NULLASM`                           |
| 1    | 内核代码段 | `SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)` |
| 2    | 内核数据段 | `SEG_ASM(STA_W, 0x0, 0xffffffff)`       |
| 3    | 用户代码段 | 空                                      |
| 4    | 用户数据段 | 空                                      |
| 5    | 任务数据段 | 空                                      |

我们注意到内核数据段和内核代码段的基地址都是0x0，最大Offset都是0xffffffff，也就是虚拟地址和物理地址之间是直接映射关系。

```assembly
  ljmp    $(SEG_KCODE<<3), $start32
.code32  # Tell assembler to generate 32-bit code now.
start32:
  # Set up the protected-mode data segment registers
  movw    $(SEG_KDATA<<3), %ax    # Our data segment selector
  movw    %ax, %ds                # -> DS: Data Segment
  movw    %ax, %es                # -> ES: Extra Segment
  movw    %ax, %ss                # -> SS: Stack Segment
  movw    $0, %ax                 # Zero segments not ready for use
  movw    %ax, %fs                # -> FS
  movw    %ax, %gs                # -> GS

  # Set up the stack pointer and call into C.
  movl    $start, %esp
  call    bootmain
```

在此时cs段寄存器中的内容对应内核代码段，ds、es、ss段寄存器的内容对应内核数据段，使用段寻址与直接寻址除了权限之外没有什么区别。最后，bootasm.S会调用bootmain.c中的bootmain()函数，并且不返回。

###### bootmain.c

```cpp
void bootmain(void){
  struct elfhdr *elf;
  struct proghdr *ph, *eph;
  void (*entry)(void);
  uchar* pa;

  elf = (struct elfhdr*)0x10000;  // scratch space

  // Read 1st page off disk
  readseg((uchar*)elf, 4096, 0);

  // Is this an ELF executable?
  if(elf->magic != ELF_MAGIC)
    return;  // let bootasm.S handle error

  // Load each program segment (ignores ph flags).
  ph = (struct proghdr*)((uchar*)elf + elf->phoff);
  eph = ph + elf->phnum;
  for(; ph < eph; ph++){
    pa = (uchar*)ph->paddr;
    readseg(pa, ph->filesz, ph->off);
    if(ph->memsz > ph->filesz)
      stosb(pa + ph->filesz, 0, ph->memsz - ph->filesz);
  }

  // Call the entry point from the ELF header.
  // Does not return!
  entry = (void(*)(void))(elf->entry);
  entry();
}
```

bootmain.c将磁盘的第一页（xv6 kernel）加载到内存从0x10000起始的位置，加载elf header中定义的程序段，随后控制流进入xv6 kernel的elf header中定义的entry()，并且不返回。

###### entry.S

```assembly
# Entering xv6 on boot processor, with paging off.
.globl entry
entry:
  # Turn on page size extension for 4Mbyte pages
  movl    %cr4, %eax
  orl     $(CR4_PSE), %eax
  movl    %eax, %cr4
  # Set page directory
  movl    $(V2P_WO(entrypgdir)), %eax
  movl    %eax, %cr3
  # Turn on paging.
  movl    %cr0, %eax
  orl     $(CR0_PG|CR0_WP), %eax
  movl    %eax, %cr0

  # Set up the stack pointer.
  movl $(stack + KSTACKSIZE), %esp

  # Jump to main(), and switch to executing at
  # high addresses. The indirect call is needed because
  # the assembler produces a PC-relative instruction
  # for a direct jump.
  mov $main, %eax
  jmp *%eax
```

entry.S做了几件微小的工作：

1. 开启大页支持

2. 设置cr3为entrypgdir所在的内存地址

   entrypgdir是内核初始化过程中的初始页表，定义在main.c中，内容如下：

   ```cpp
   __attribute__((__aligned__(PGSIZE)))
   pde_t entrypgdir[NPDENTRIES] = {
     // Map VA's [0, 4MB) to PA's [0, 4MB)
     [0] = (0) | PTE_P | PTE_W | PTE_PS,
     // Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
     [KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
   };
   ```

   entrypgdir中包含了一个大页的映射：虚拟地址的[0, 4MB)，[KERNBASE, KERNBASE+4MB)均映射到物理地址的[0, 4MB)。在内核初始化阶段，能使用的物理内存大小仅为3MB。（页表仅包含一个大页的映射，且0x100000以下的内存空间被设备占用）

3. 将内存寻址模式由段寻址修改为页寻址

4. 将栈寄存器%esp修改为内核栈地址
5. 将控制流转移到main.c中定义的main函数，并且不返回

##### 3. 内核虚拟地址空间（kalloc.c vm.c）

我们再将目光回到main函数：

```cpp
int main(void){
  kinit1(end, P2V(4*1024*1024)); // phys page allocator
  kvmalloc();      // kernel page table
  ...
  seginit();       // segment descriptors
  ...
  startothers();   // start other processors
  kinit2(P2V(4*1024*1024), P2V(PHYSTOP)); // must come after startothers()
  userinit();      // first user process
  mpmain();        // finish this processor's setup
}
```

显然初始页表上无法运行一个操作系统，因此我们需要使用kinit1()，kvmalloc()，kinit2()为内核建立一个完整的虚拟地址空间。

###### kinit1()

```cpp
struct run {
  struct run *next;
};
```

在kalloc中，我们使用单向空闲链表来组织所有空闲的内存页。这里空闲页头部的4字节被用来保存指向下一个空闲页的指针。

```cpp
struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;
```

kmem数据结构用来管理内核的所有空闲页，`struct run *freelist`保存指向第一个空闲页的指针。由于xv6提供了多处理器支持，可能会有多个进程并行访问freelist，因此这里加入了自旋锁，保证并发安全性。

```cpp
void kfree(char *v){
  struct run *r;
  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");
  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}
```

kfree函数完成了以下工作：

1. 将指针`char *v`指向的页内容清空 `memset(v, 1, PGSIZE)`
2. 将指针`char *v`指向的页插入单向空闲链表的头部 ` r->next = kmem.freelist; kmem.freelist = r`

如果`use_lock`被置为1，就意味着kfree函数需要考虑并发安全性，在修改freelist前需要获取锁，修改完成后需要释放锁。

```cpp
char* kalloc(void){
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}
```

kalloc函数完成了以下工作：

1. 取出单向空闲链表的头部元素`kmem.freelist`

2. 如果没有空闲页，`kmem.freelist = NULL`，返回NULL；

   如果有空闲页，将`kmem.freelist`赋值为下一个空闲页`r->next`，返回`kmem.freelist`。

如果`use_lock`被置为1，就意味着kalloc函数需要考虑并发安全性，在修改freelist前需要获取锁，修改完成后需要释放锁。

```cpp
void freerange(void *vstart, void *vend){
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
```

freerange会将（首地址和尾地址都）在vstart和vend之间的所有页加入单向空闲链表。

```cpp
void kinit1(void *vstart, void *vend){
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}
```

当执行kinit1时，xv6中只有一个核心在运行，所以可以不加锁。main函数中的end指针标记了xv6 kernel ELF文件的结尾位置，于是kinit1可以将[PGROUNDUP(end), 0x400000]的物理内存用于分配。

###### kvmalloc()

```cpp
// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void kvmalloc(void){
  kpgdir = setupkvm();
  switchkvm();
}
```

kvmalloc函数调用setupkvm()初始化内核页表（主要在运行调度器时使用），并调用switchkvm()将内核页表载入硬件。

```cpp
pde_t* setupkvm(void){
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}
```

setupkvm完成了几件重要的工作：

1. 建立初始内核页表kmap *k

   kmap数据类型定义了内核地址空间，内核地址空间会被映射到每一个进程地址空间中KERNBASE以上的部分：

   ```cpp
   // This table defines the kernel's mappings, which are present in
   // every process's page table.
   static struct kmap {
     void *virt;
     uint phys_start;
     uint phys_end;
     int perm;
   } kmap[] = {
    { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
    { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
    { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
    { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
   };
   ```

   这是每一个进程地址空间中和内核有关的部分：

   | 虚拟地址                      | 物理地址         | 内容               |
   | ----------------------------- | ---------------- | ------------------ |
   | [0x80000000, 0x80100000]      | [0, 0x100000]    | I/O设备预留        |
   | [0x80100000, 0x80000000+data] | [0x100000, data] | 代码，只读数据     |
   | [0x80000000+data, 0x80E00000] | [data, 0xE00000] | 其它数据，可用空间 |
   | [0xFE000000, 0]               | [0xFE000000, 0]  | 内存映射I/O        |

2. 调用kalloc，为页表分配一个页

3. 调用mappages，建立页表项

   ```cpp
   static int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm){
     char *a, *last;
     pte_t *pte;
   
     a = (char*)PGROUNDDOWN((uint)va);
     last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
     for(;;){
       if((pte = walkpgdir(pgdir, a, 1)) == 0)
         return -1;
       if(*pte & PTE_P)
         panic("remap");
       *pte = pa | perm | PTE_P;
       if(a == last)
         break;
       a += PGSIZE;
       pa += PGSIZE;
     }
     return 0;
   }
   ```

   mappages函数将虚拟地址空间中从va开始，大小为size的空间映射到物理地址空间中从pa开始，大小为size的空间。

   对于虚拟地址空间中的每一页，mappages通过walkpgdir函数设置一级页表，获取二级页表的地址，并设置二级页表项。

   ```cpp
   // Return the address of the PTE in page table pgdir
   // that corresponds to virtual address va.  If alloc!=0,
   // create any required page table pages.
   static pte_t * walkpgdir(pde_t *pgdir, const void *va, int alloc){
     pde_t *pde;
     pte_t *pgtab;
   
     pde = &pgdir[PDX(va)];
     if(*pde & PTE_P){
       pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
     } else {
       if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
         return 0;
       // Make sure all those PTE_P bits are zero.
       memset(pgtab, 0, PGSIZE);
       // The permissions here are overly generous, but they can
       // be further restricted by the permissions in the page table
       // entries, if necessary.
       *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
     }
     return &pgtab[PTX(va)];
   }
   ```

   walkpgdir函数尝试从页目录中找到当前虚拟地址对应的二级页表，设置对应的一级页表项，并返回二级页表的地址。

   如果当前不存在二级页表，则会调用kalloc，分配一个新页用作二级页表。

4. 调用freevm，释放pgdir地址空间中所有的内存

   ```cpp
   void freevm(pde_t *pgdir){
     uint i;
     if(pgdir == 0)
       panic("freevm: no pgdir");
     deallocuvm(pgdir, KERNBASE, 0);
     for(i = 0; i < NPDENTRIES; i++){
       if(pgdir[i] & PTE_P){
         char * v = P2V(PTE_ADDR(pgdir[i]));
         kfree(v);
       }
     }
     kfree((char*)pgdir);
   }
   ```

   freevm按照以下顺序释放内存空间：

   1. 调用deallocuvm，释放pgdir中用户地址空间的所有内存
   2. 遍历整个页目录，释放页目录项对应的页表的内存
   3. 释放页目录的内存

```cpp
void switchkvm(void){
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}
```

switchkvm通过汇编指令将cr3寄存器置为kpgdir的地址，从而替换页表。

###### kinit2()

```cpp
void kinit2(void *vstart, void *vend){
  freerange(vstart, vend);
  kmem.use_lock = 1;
}
```

对于xv6，64MB的内存仍略显不够。在运行用户程序前，main函数调用kinit2将物理地址在[0x400000, 0xE00000]之间的内存加入内核页表。

需要注意的是，由于之前main函数调用了startothers()对其它CPU进行初始化，这里可能会存在多个进程并发访问kpgdir，因此需要加锁。

##### 4. 用户虚拟地址空间（ualloc.c vm.c）

```cpp
void userinit(void){
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  ...
}
```

main函数在最后调用userinit()函数初始化第一个用户进程。在这里我们保留与虚拟内存相关的代码，省略其它部分。

在调用allocproc()创建进程后，首先userinit()会调用setupkvm()创建一个与初始化内核时创建的相同的页表。随后会使用inituvm函数初始化用户内存地址空间，并把用户进程的数据移动到这个空间中。

###### inituvm()

```cpp
// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void inituvm(pde_t *pgdir, char *init, uint sz){
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}
```

inituvm函数调用kalloc函数分配一个页的内存空间，将这个内存空间清空。

随后，inituvm调用mappages创建对应的一级、二级页表，将这个地址空间映射到从0x0开始的第一个页上。

这里的init指针指向用户进程初始化代码（具体详见initcode.S），因此用户进程初始化代码的数据不能超过一个页。

这里我们只考虑与内存管理相关的部分，userinit函数中与创建新进程有关的部分已经在先前的阅读报告中提到。

###### exec()

当用户进程上CPU时，控制流会进入initcode.S，随后触发exec系统调用（在exec.c中定义）。exec函数从磁盘中加载用户程序，并真正开始运行用户程序。

```cpp
int exec(char *path, char **argv){
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

 ...
  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    ...
  }

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;
  ...

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
```

exec函数主要完成以下工作：

1. 从磁盘里加载一个ELF文件。ELF文件中包含了代码段（code segment）与数据段（data segment），并且描述了这些段应该被加载到的虚拟地址。
2. 分配两个虚拟内存页。其中第一个不可访问，用来防止栈访问越界；第二个用作用户栈。因此，xv6中用户程序所能利用的用户栈大小最大为4KB。

xv6中的用户地址空间采用双向空闲链表管理，而双向空闲链表的实现并不是本文的重点，因此在此略过。



##### 5. 最终的内存布局

<img src="https://th0ar.gitbooks.io/xv6-chinese/content/pic/f2-3.png" alt="figure2-3" style="zoom: 67%;" />

对于xv6的任意一个用户进程，当它完成初始化时，其虚拟地址空间应该如上图所示。

| 虚拟地址                      | 物理地址         | 内容                     |
| ----------------------------- | ---------------- | ------------------------ |
| [0x0, 0x1000]                 | 在kalloc()时分配 | 用户进程的代码和数据     |
| [0x1000, 0x2000]              | 在exec()时分配   | guard page，用于检测溢出 |
| [0x2000, 0x3000]              | 在exec()时分配   | 用户进程的栈             |
| [0x80000000, 0x80100000]      | [0, 0x100000]    | I/O设备预留              |
| [0x80100000, 0x80000000+data] | [0x100000, data] | 内核代码，只读数据       |
| [0x80000000+data, 0x80E00000] | [data, 0xE00000] | 内核数据+可用物理内存    |
| [0xFE000000, 0]               | [0xFE000000, 0]  | 内存映射的I/O设备        |

##### 参考资料

[LGDT/LIDT - 加载全局/中断描述符表格寄存器](https://blog.csdn.net/judyge/article/details/52343632)

[任务状态段(Task State Segment)](https://blog.csdn.net/pirloofmilan/article/details/8857382)
#### 0. xv6文件系统综述

##### 0.1 xv6文件系统分层

![figure6-1](/Users/Apple/Desktop/f6-1.png)

xv6的**块缓冲**层包含了IDE硬盘I/O操作的实现。操作系统中同时存在大量进程，难免存在多个进程同时访问同一个块的情况，因此xv6文件系统需要保证同时只有一个内核进程可以修改磁盘块。其次，传统的机械硬盘响应时间远远长于内存（尤其在随机访问的情况下），因此在内存中建立缓冲区，加快部分磁盘块的读写就显得尤为重要。

文件系统需要支持**崩溃恢复**——如果计算机在进行文件操作时意外断电，可能会有不少的待执行文件操作被意外打断。当计算机重启后，我们有可能会发现文件系统的数据结构中出现错误——例如不同的操作同时将一个块标志为使用中、空闲，文件系统需要从这种错误状态中恢复。为此，xv6文件系统引入了**日志**机制，将对磁盘的更新以“会话”的形式进行打包，从而避免更新过程被打断（这一点似乎与数据库有相似之处）。

xv6采用了与Unix相似的方式——inode来进行文件、目录的组织，并采用BitMap记录块分配的情况，这构成了**块分配**层。

xv6中将目录看做一种特殊的inode，目录项包含文件名、对应的 i 节点，这构成了**目录**层。

NachOS本身不提供复杂路径的解析功能（在Execise中，我被迫使用一些较为复杂的方法，实现对"/tmp/shuwarin.txt"这样复杂路径的解析）。而xv6将路径的递归解析功能封装为文件系统的一层（**查找**层），为程序员提供方便。

xv6遵循Unix中"一切皆文件"的思想，在**文件描述符**层中将管道、设备与普通文件一同抽象为文件，为程序员提供方便。

##### 0.2 xv6文件系统结构

![figure6-2](https://th0ar.gitbooks.io/xv6-chinese/content/pic/f6-2.png)

与NachOS相比，xv6在磁盘中增加了:

**Boot**	磁盘的第0块用于存放xv6的Bootloader（xv6并不是与NachOS一样的OS模拟器，启动自然需要Bootloader），这个块不会被文件系统使用

**Super**	磁盘的第1块用于存放文件系统的MetaData（如文件系统总块数、数据块数、inode块数、日志块数等）

```cpp
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};
```

**Log**	磁盘的最后是日志块

在磁盘中减少了：

**Root**	根目录块（xv6中根目录的inode号固定为1，其实也类似）

```cpp
#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size
```



#### 1. 块缓冲

##### 1.1 数据结构

xv6采用`buf`数据结构管理正在访问的磁盘块：

```cpp
/* from buf.h */
struct buf {
  int flags;
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  struct buf *qnext; // disk queue
  uchar data[BSIZE];
};
#define B_VALID 0x2  // buffer has been read from disk
#define B_DIRTY 0x4  // buffer needs to be written to disk
```

**flags**	记录缓冲区中块的状态。一个缓冲区有三种状态：`B_VALID` 意味着这个缓冲区拥有磁盘块的有效内容，`B_DIRTY` 意味着缓冲区的内容已经被改变并且需要写回磁盘。

**dev, blockno**	记录块号和设备号

**refcnt**	引用计数

**lock**	锁，保证对块的访问是互斥的

**prev, next**	xv6采用MRU链表的形式组织缓冲区中的块

采用链表之后，我们还可能遇到一个问题——xv6中不同进程的I/O操作可能同时修改链表，因此我们还需要保证访问链表的原子性。

```cpp
/* from bio.c */
struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;
```

`bcache`结构体中增加了`lock`用于避免多进程并发访问链表时出错，链表的表头是`head`。

```cpp
void binit(void){
  struct buf *b;
  initlock(&bcache.lock, "bcache");
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}
```

`binit`函数负责初始化链表与锁。

我们在这里还可以注意到一个细节——为了实现的简便，xv6采用了双向循环链表来组织buf。

##### 1.2 I/O操作

在xv6中，内核进程调用`bread`读一个块，`bwrite`写一个块。在I/O操作完成后，要求调用`brelse`释放这个块的锁（否则会出现死锁）。我们接下来关注这几个函数：

```cpp
// Return a locked buf with the contents of the indicated block.
struct buf* bread(uint dev, uint blockno){
  struct buf *b;
  b = bget(dev, blockno);
  if((b->flags & B_VALID) == 0) {
    iderw(b);
  }
  return b;
}
```

当进程调用`bread`后，需要先调用`bget`获取一个缓冲区中的块。

如果块的valid位没有被设置（也就是说需要从磁盘中读取），`bread`会调用`iderw`，等待xv6将磁盘中的内容读到buf后再继续执行。VALID位在`iderw`中被设置。

`bget`函数获取一个缓冲区中的块：

```cpp
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf* bget(uint dev, uint blockno){
  struct buf *b;
  acquire(&bcache.lock);
  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Not cached; recycle an unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->flags = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}
```

1. 获取链表锁`bcache.lock`
2. （块已经在缓冲区内）增加块的引用计数，释放链表锁，对块加锁。（如果这个块正在被使用，进程会被阻塞）
3. （缓冲区内找不到块）找一个没有被使用的块（refcnt=0&&!DIRTY）作为新块，释放链表锁，对块加锁

我们注意到如果bget在缓冲区内找不到空闲的块，则会触发一个panic。事实上，我们可以让bget在这里阻塞，等待缓冲区出现空闲。

我们还可以注意到一个细节——块缓冲链表中最近访问的块在前，因此在查找块是否被缓存时，xv6顺序遍历链表（MRU）；查找空闲块时，xv6逆序遍历链表（LRU）。这里利用了时间局部性原理，在实际应用场景中能够减少查找时间。

```cpp
// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b){
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  b->flags |= B_DIRTY;
  iderw(b);
}
```

进程调用`bwrite`对缓冲区中的块进行写操作，设置块的DIRTY位，并调用`iderw`，等待缓冲区内的内容被写回磁盘再继续执行。

需要注意的是，`iderw`函数在I/O操作完成后，会清除块的DIRTY状态，并设置VALID。从这里我们可以看出，xv6的块缓冲采用了直写（Write Through）的策略，DIRTY状态仅在写操作正在修改磁盘时存在。

```cpp
void brelse(struct buf *b){
  if(!holdingsleep(&b->lock))
    panic("brelse");
  releasesleep(&b->lock);
  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}
```

I/O操作完成后，内核进程需要调用`brelse`函数释放块。我们可以观察到，`brelse`函数：

1. 释放块的锁（唤醒等待这个锁的其它进程）
2. 获取链表锁，修改引用计数
3. 如果引用计数为0，将块放到链表的头部（`bcache.head.next`）
4. 释放链表锁



#### 2. 日志

##### 2.1 数据结构

```cpp
/* from log.c */
// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
  int n;
  int block[LOGSIZE];
};

struct log {
  struct spinlock lock;
  int start;
  int size;
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
struct log log;
```

之前我们提到，xv6中的日志块位于磁盘的末尾。事实上日志块中包含起始块和数据块，起始块的数据结构对应`logheader`：

**n**	还没有被写入磁盘的更改数量

**block**	日志中数据块对应的扇区号

xv6启动后，会在内存中维护log结构体。log结构体中含有：

**start, size, dev**	磁盘中日志区的位置、大小、设备号（`initlog`调用`readsb`函数，从磁盘的超级数据块中读出）

**outstanding**	当前正在执行的文件相关系统调用数

**committing**	正在写磁盘的进程数（xv6不允许多个commit同时执行，因此可能的值只有0和1）

**lock**	保护outstanding, committing的锁

##### 2.2 实现

xv6采取了一个巧妙的方法实现文件系统的崩溃恢复。当我们通过系统调用写磁盘时，xv6并不直接使用`bread`、`bwrite`、`brelse`，而是采取如下的形式：

```cpp
begin_op();
...
bp = bread(...);
bp->data[...] = ...;
log_write(bp);
...
end_op();
```

这样实现有以下的好处：

1. 一个系统调用对磁盘数据区的修改要么全部完成，要么全部撤回，不会出现修改了一半的情况
2. 系统意外停机时，可以从磁盘中读取log，继续没有完成的写操作

###### 2.2.1 begin_op

```cpp
// called at the start of each FS system call.
void begin_op(void){
  acquire(&log.lock);
  while(1){
    if(log.committing){
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // this op might exhaust log space; wait for commit.
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}
```

begin_op()完成的工作较为简单：

1. 当1. 有正在执行的commit 2. 日志区空间不够 时，进程会阻塞，等待日志状态更新后再执行系统调用。

2. 如果能够执行系统调用， outstanding+=1

###### 2.2.2 log_write

可能与我们的直觉不同的是，log_write()事实上没有复制磁盘上的任何数据，而仅仅是标记了缓冲区中哪些块被修改。

```cpp
void log_write(struct buf *b){
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorbtion
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) log.lh.n++;
  b->flags |= B_DIRTY; // prevent eviction
  release(&log.lock);
}
```

log_write()进行了以下工作：

1. 如果lh.block中已经存在了当前块的日志，那就什么也不做；否则就将lh.block[i]的值置为当前块号，并修改n。

  ```cpp
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) log.lh.n++;
  ```

2. 将缓冲区中的块置为DIRTY，避免这个块被从缓冲区中替换。

###### 2.2.3 end_op

```cpp
static void commit(){
  if (log.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log
    write_head();    // Write header to disk -- the real commit
    install_trans(); // Now install writes to home locations
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}
```

commit函数完成将数据从内存缓冲区$\rightarrow$磁盘日志区$\rightarrow$磁盘数据区的过程：

1. `write_log`将更改过的块写入磁盘日志区
2. `write_head`将log header写入磁盘
3. `install_trans`将磁盘日志区的数据写入磁盘数据区
4. 现在所有更改都被写入磁盘，因此`log.lh.n`被置为0，再调用`write_head`将log header写入磁盘

```cpp
// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void end_op(void){
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}
```

当系统调用操作完成后，我们需要在恰当的时机把更改写入磁盘。

1. 如果还有其它系统调用操作正在等待，我们需要先进行其它操作再写入磁盘，所以需要唤醒这些进程。
2. 如果没有系统调用在等待，那就将更改写入磁盘，log.committing = 1。
3. 在调用commit前，我们需要先释放锁，避免在没有释放锁的情况下阻塞造成死锁。
4. 重新获取锁，log.committing = 0， 唤醒所有正在等待的进程（可能因为正在进行commit操作而阻塞），然后再释放锁。

###### 2.2.4 恢复日志

```cpp
static void recover_from_log(void){
  read_head();
  install_trans(); // if committed, copy from log to disk
  log.lh.n = 0;
  write_head(); // clear the log
}
```

每一次执行`initlog`时，xv6都会调用`read_head`，从磁盘中读出之前的log header。`install_trans`会将日志区的数据复制到磁盘数据区（如果之前有“会话”没有执行完），随后`log.lh.n`被置为0，再调用`write_head`将log header写入磁盘。



#### 3. 块分配与Inode

##### 3.1 块分配

###### 3.1.1 balloc

balloc在BitMap中分配一个块：

```cpp
// Allocate a zeroed disk block.
static uint balloc(uint dev){
  int b, bi, m;
  struct buf *bp;
  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}
```

1. 在执行balloc之前，`readsb`应该已经将磁盘超级块的内容读入sb了。这里`BBLOCK(b, sb)`算出了存储块b空闲状态的BitMap的块号，那bp就是对应的BitMap。
2. 如果BitMap中有一位为空，那就将这一位置为占用，然后使用`log_write`更新内存缓冲区中的BitMap。（xv6的所有文件相关操作总是尽可能使用内存，以加快I/O速度）
3. 使用`brelse`释放块，通过`bzero`将块的内容清空。
4. 如果没有空闲块，则触发kernel panic。

bfree在BitMap中释放一个块：

```cpp
// Free a disk block.
static void bfree(int dev, uint b){
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}
```

需要注意的是，在bread和brelse的操作中我们已经实现了互斥，因此一定不会发生两个进程同时申请空间，将一个块同时分配给两个进程的情况。

##### 3.2 Inode 数据结构

<img src="https://th0ar.gitbooks.io/xv6-chinese/content/pic/f6-4.png" alt="figure6-4" style="zoom:75%;" />

```cpp
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};
```

**type**	文件类型（普通文件/目录/特殊文件）如果type是0，则表示inode处于空闲状态。

```cpp
#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Device
```

**nlink**	指向了这一个 i 节点的目录项

**size**	文件大小（字节）

**addrs**	文件索引	addrs的前12项是直接索引，第13项指向128项间接索引，也就是xv6中文件最大大小为12+128个块。

##### 3.3 Inode 操作

由于以下内容不是说明xv6文件系统原理的重点（更多的内容在实现上），且需要说明的内容与上文有诸多重复之处，因此这里仅仅简要说明每一个函数的功能。

###### 3.3.1 **ialloc**	

申请一个inode（与balloc类似，遍历所有inode，寻找空闲项）。

###### **3.3.2 iget**	

查找一个inode，并增加引用计数。（查找过程与bget类似）

###### **3.3.3 ilock， iunlock**	

ilock/iunlock：对一个inode加锁/解锁。

尽管iget与bget的实现大体类似，但这里我们将加锁的过程独立出来，以便进程长时间打开一个文件。

###### **3.3.4 iput**

将引用计数减1。如果引用计数为0，则释放inode。

###### **3.3.5 itrunc**

释放inode对应的数据。仅当inode没有引用（没有进程在使用），且没有链接（不属于任何一个目录）时调用。



#### 4. 目录

##### 4.1 数据结构

```cpp
// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14
struct dirent {
  ushort inum;
  char name[DIRSIZ];
};
```

direct结构体是xv6中的文件目录项——目录项中包括inode和文件名（最长14个字符）。

与之前的NachOS类似，在xv6中我们也将目录看做目录项的数组。令人疑惑的是，xv6中并没有将direct组织为数组，而是在文件夹的数据块内塞进多个direct，读取时加上offset偏移量。

##### 4.2 查找

dirlookup在目录dp内查找文件。

```cpp
struct inode* dirlookup(struct inode *dp, char *name, uint *poff){
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}
```

如果找到文件，dirlookup返回一个指向相关 i 节点的指针，并设置目录内条目的字节偏移`*poff` 。

这里我们需要注意，由于iget()不会对inode上锁，因此dirlookup返回的inode是未上锁的。如果我们在这里对Inode上锁，调用dirlookup查找"."则很可能会导致死锁。

##### 4.3 新增条目

dirlink在目录dp内新增项(inum, name)：

```cpp
// Write a new directory entry (name, inum) into the directory dp.
int dirlink(struct inode *dp, char *name, uint inum){
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}
```

1. 如果目录内有重名文件，则新增条目失败。
2. 在目录内查找空闲目录项；若找不到，则新增条目失败。
3. 为新项赋值，并调用`writei`写回更改后的目录文件。



#### 5. 查找

namex返回对应路径的inode：

```cpp
// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode* namex(char *path, int nameiparent, char *name){
  struct inode *ip, *next;
  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);
  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}
```

1. 如果路径以'/'开始，则从根目录开始解析路径；否则从当前目录开始解析路径。
2. 使用`skipelem`，从路径中解析出文件名。
3. 使用`dirlookup`，逐层查找文件。如果nameiparent为True，那就提前结束循环。
4. 当前的ip就是指向对应文件Inode的指针。

这里我们注意到，逐层查找文件时有对每个`ip`加锁、解锁的过程。这并不是为了解决并发问题，而是因为`ilock`函数会在当前inode失效时从磁盘中读取inode，保证`ip->type`一定存在。



#### 6. 文件描述符

##### 6.1 数据结构

和我们在NachOS的Lab中实现的类似，xv6有一张全局的打开文件表ftable：

```cpp
/* from file.c */
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;
```

##### 6.2 filealloc， filedup， fileclose

```cpp
struct file* filealloc(void){
  struct file *f;
  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}
```

filealloc函数在全局打开文件表中寻找一个引用计数为0的文件，将引用计数修改为1，并返回这个文件的指针。（查找过程有`ftable->lock`保证并发安全）

```cpp
// Increment ref count for file f.
struct file* filedup(struct file *f){
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}
```

filedup函数在保证并发安全的情况下，增加引用计数。

```cpp
// Close file f.  (Decrement ref count, close when reaches 0.)
void fileclose(struct file *f){
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}
```

fileclose函数首先减少引用计数。当引用计数为0时：

1. 如果这个文件是管道，那就关闭管道。
2. 如果这个文件是Inode，那就将inode的引用计数-1。

##### 6.3 filestat， fileread， filewrite

```cpp
int filestat(struct file *f, struct stat *st){
  if(f->type == FD_INODE){
    ilock(f->ip);
    stati(f->ip, st);
    iunlock(f->ip);
    return 0;
  }
  return -1;
}
```

filestat函数包装了inode的`stati`，将st的值置为inode的属性。

```cpp
int fileread(struct file *f, char *addr, int n){
  int r;
  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
    return r;
  }
  panic("fileread");
}
```

fileread函数执行读文件操作。在xv6的实现中，读写操作是互斥的，因此在调用`readi`前加锁，在调用`readi`后解锁。

```cpp
int filewrite(struct file *f, char *addr, int n){
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("filewrite");
}
```

fileread函数执行写文件操作。写文件时：

1. 调用`begin_op`，开始一个新会话。
2. 调用`ilock`，实现读写操作的互斥。
3. 调用`writei`，向缓冲区内写入数据。
4. 调用`iunlock`解锁inode。
5. 调用`end_op`结束会话，并在合适的时机将改动写回磁盘。

先前我们提到过，xv6的日志大小是有限的，因此当我们对较大的文件进行写操作时，不能一次性写完。一次写操作最大的数据大小是`((MAXOPBLOCKS-1-1-2) / 2) * 512`，因此在这里需要分批写入。


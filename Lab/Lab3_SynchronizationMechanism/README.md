#### 任务完成情况

| Exercise 1 | Exercise 2 | Exercise 3 | Exercise 4 | Challenge 1 |
| ---------- | ---------- | ---------- | ---------- | ----------- |
| Y          | Y          | Y          | Y          | Y           |

**Exercise 1  调研Linux中实现的同步机制**

**1.1** 原子操作

顾名思义，原子操作是原子的，也就是不能被分割，不能被打断的操作。操作的原子性是由硬件保证的，例如x86处理器提供的`xhcg`指令就是原子操作。原子操作需要硬件支持，为此，Linux的原子操作是平台相关的，大多采用汇编实现。原子操作一个常见的应用场景时资源引用计数：

```cpp
typedef struct { volatile int counter; } atomic_t; 
```

这里的volatile关键字提示编译器不要尝试优化这个变量，在每次对其值进行引用的时候都会从原始地址取值，以免对这个变量的原子性失效。

在`/include/asm-generic/atomic.h`中，Linux定义了以下原子操作的API：

```cpp
#define atomic_read(v)	READ_ONCE((v)->counter)
#define atomic_set(v, i) WRITE_ONCE(((v)->counter), (i))
#define atomic_xchg(ptr, v)		(xchg(&(ptr)->counter, (v)))
#define atomic_cmpxchg(v, old, new)	(cmpxchg(&((v)->counter), (old), (new))) 
...
```

**1.2** 互斥锁

Linux中定义了简单的互斥锁`struct mutex`，互斥锁在创建时初值为1。需要注意的是，这里`mutex`的实现更像课件上定义的信号量，是允许负值的。

`mutex.lock()`等价与对信号量进行`P`操作，当获取锁时，将值-1，若值<0则等待。

将而`mutex.unlock()`等价与对信号量进行`V`操作，当释放时，将值+1；如果有进程在等待，则唤醒等待队列的队首进程。

**1.3** 自旋锁

当进程尝试进入临界区前，进程需要先获得锁，离开临界区后需要释放锁。当进程获取锁时发现锁已经被其它进程占用，则会循环等待，即“自旋”。

需要注意的是，自旋锁在多处理器系统中不会出现什么问题，因为当一个进程在自旋时其它进程可以并行地完成锁的释放。在实现了时间片的单处理系统中，进程自旋的过程会占用一整个时间片，造成一定的资源浪费。而在没有实现时间片与抢占的单处理器系统中，忙等待造成的问题是灾难性的——进程不会主动放弃处理器，其它进程也无法操作锁，从而造成死锁。

进程在使用自旋锁时，调用`spin_lock_irqsave`进入临界区，完成操作后调用`spin_unlock_irqrestore`离开临界区。这一过程也存在关闭中断，恢复中断等级的操作，以免进程在临界区内被调度下CPU，造成死锁的风险。

**1.4** 读写锁

读写锁是一种特殊的自旋锁。在读者-写者问题中，允许多个读者同时读，但不允许多个写者同时写。Linux定义了`RW_LOCK`作为读写锁，读者进程调用`read_lock_irqsave`进入临界区，而写者进程调用`write_lock_irqsave`进入临界区。

**1.5** etc...

由于当前的进程同步互斥问题复杂性不断增加，除了以上列举的几种之外，Linux还实现了禁止中断、RCU、Futex、完成变量等机制以解决进程的同步互斥问题。

除此之外，Linux内核还实现了管道、 信号、套接字等机制用于进程间的通信。

**Exercise 2  源代码阅读**

**2.1 **code/threads/synch.h, code/threads/synch.cc

`synch.h`和`synch.cc`中定义了NachOS中用于实现同步的三种数据结构——信号量，锁和条件变量。其中信号量的实现已经给出，而锁和条件变量的实现需要我们完成。

**2.1.1** 信号量

```cpp
class Semaphore {
  public:
    Semaphore(char* debugName, int initialValue);	// set initial value
    ~Semaphore();   					// de-allocate semaphore
    char* getName() { return name;}			// debugging assist
    
    void P();	 // these are the only operations on a semaphore
    void V();	 // they are both *atomic*
    
  private:
    char* name;        // useful for debugging
    int value;         // semaphore value, always >= 0
    List *queue;       // threads waiting in P() for the value to be > 0
};
```

信号量类中的私有变量有信号量名`name`，信号量值`value`，等待信号量的进程队列`queue`。

这里我们注意到，NachOS并不允许用户程序直接访问信号量的值。NachOS的设计者们在注释中提到了这一点——尝试通过信号量的值进行判断是没有意义的。既然用户进程没有在访问信号量进行比较时用mutex保证信号量的值不会被上下文切换后其他进程的P或者V操作修改，那就意味着进程进行判断前后信号量的值可能不同。

除此之外，NachOS的信号量定义于操作系统课件有所区别——这里信号量的值只能是0和1。然而在实际使用时两者的差别并不大。

`Semaphore`类的方法定义于`synch.cc`中：

- `Semaphore(char* debugName, int initialValue)`

  ```cpp
  Semaphore::Semaphore(char* debugName, int initialValue){
      name = debugName;
      value = initialValue;
      queue = new List;
  }
  ```
  
  构造函数对一个信号量进行初始化，通过参数传递`name`及`value`，并创建一个等待队列。
  
- `~Semaphore()`

  ```cpp
  Semaphore::~Semaphore(){ delete queue; }
  ```
  
  析构函数删除该信号量的等待队列（NachOS的List定义于`list.c`，为了避免内存泄漏在删除信号量时最好调用`~List()`）。

- `P()`

  ```cpp
  void Semaphore::P(){
      IntStatus oldLevel = interrupt->SetLevel(IntOff);	// disable interrupts
      while (value == 0) { 			// semaphore not available
  			queue->Append((void *)currentThread);	// so go to sleep
  			currentThread->Sleep();
      } 
      value--; 					// semaphore available, consume its value.
      (void) interrupt->SetLevel(oldLevel);	// re-enable interrupts
  }
  ```

  在更改信号量的值前，`P`函数先保存了之前的中断等级`oldLevel`，屏蔽了中断，在`P`函数最后又恢复了中断等级。NachOS并没有多处理器支持，因此屏蔽中断足以保证原语的原子性。

  当前信号量已被占用（值为0）时，`P`会将当前线程的指针`currentThread`加入信号量等待队列`queue`中，然后将当前线程置为阻塞态。

  当前信号量未被占用时，`P`则将信号量的值-1，占用这个信号量。

- `V()`

  ```cpp
  void Semaphore::V(){
      Thread *thread;
      IntStatus oldLevel = interrupt->SetLevel(IntOff);
      thread = (Thread *)queue->Remove();
      if (thread != NULL)	   // make thread ready, consuming the V immediately
  			scheduler->ReadyToRun(thread);
      value++;
      (void) interrupt->SetLevel(oldLevel);
  }
  ```
  
  `V`函数也有先屏蔽中断，之后恢复中断等级以保证操作原子性的机制。
  
  `V`函数会先从信号量的等待队列中取出队首，如果队首存在将其状态修改为就绪，稍后继续执行。
  
  然后`V`会将信号量的值+1，释放这个信号量。

**2.1.2** 锁

```cpp
class Lock {
  public:
    Lock(char* debugName);  		// initialize lock to be FREE
    ~Lock();				// deallocate lock
    char* getName() { return name; }	// debugging assist

    void Acquire(); // these are the only operations on a lock
    void Release(); // they are both *atomic*
    bool isHeldByCurrentThread();	// true if the current thread
					// holds this lock.  Useful for
					// checking in Release, and in
					// Condition variable ops below.
  private:
    char* name;				// for debugging
    // plus some other stuff you'll need to define
};
```

`Lock`类中包含了互斥锁的实现（很可惜，这要我们自己补全）。一个锁的状态可以为Busy或Free，线程可以通过`Acquire()`占用锁，通过`Release()`释放锁。

如果锁被获取它的之外的线程释放了，则可能会导致死锁。因此，`isHeldByCurrentThread()`函数会判断当前进程是否持有锁（显然这需要补全一些私有变量）。

**2.1.3** 条件变量

```cpp
class Condition {
  public:
    Condition(char* debugName);		// initialize condition to 
					// "no one waiting"
    ~Condition();			// deallocate the condition
    char* getName() { return (name); }
    
    void Wait(Lock *conditionLock); 	// these are the 3 operations on 
					// condition variables; releasing the 
					// lock and going to sleep are 
					// *atomic* in Wait()
    void Signal(Lock *conditionLock);   // conditionLock must be held by
    void Broadcast(Lock *conditionLock);// the currentThread for all of 
					// these operations

  private:
    char* name;
    // plus some other stuff you'll need to define
};
```

`Condition`类中包含条件变量的实现，线程对于条件变量可以进行`Wait`，`Signal`和`Broadcast`操作。

`Wait`	线程将自己放到条件变量的等待队列中，然后进入阻塞态。

`Signal`	唤醒在条件变量等待队列的队首线程。

`Broadcast`	唤醒在条件变量等待队列的所有线程。

我们注意到，NachOS的条件变量支持Broadcast操作，也就是说类似于Mesa管程，线程被调度上CPU之后需要用`while`循环检查运行条件是否依然成立。

**2.2** code/threads/synchlist.h, code/threads/synchlist.cc

```cpp
class SynchList {
  public:
    SynchList();		// initialize a synchronized list
    ~SynchList();		// de-allocate a synchronized list

    void Append(void *item);	// append item to the end of the list,
				// and wake up any thread waiting in remove
    void *Remove();		// remove the first item from the front of
				// the list, waiting if the list is empty
				// apply function to every item in the list
    void Mapcar(VoidFunctionPtr func);

  private:
    List *list;			// the unsynchronized list
    Lock *lock;			// enforce mutual exclusive access to the list
    Condition *listEmpty;	// wait in Remove if the list is empty
};
```

`SynchList`类实现了一个同步列表。在注释中，NachOS的设计者描述了同步列表的功能：

1. 如果一个线程尝试移除列表上的项，却发现List是空的，那它会睡眠，直到List上不是空的为止。
2. 同时访问列表的线程只能有一个。

同步列表中的私有变量包含一个`List`，一把保护`List`的锁，和用于实现等待队列非空的条件变量`listEmpty`。我们可以发现，`SynchList`的实现思想与管程有类似之处。

- SynchList()

  ```cpp
  SynchList::SynchList()
  {
      list = new List();
      lock = new Lock("list lock"); 
      listEmpty = new Condition("list empty cond");
  }
  ```

  构造函数初始化了`List`，锁和条件变量。

-  ~SynchList()

  ```cpp
  SynchList::~SynchList()
  { 
      delete list; 
      delete lock;
      delete listEmpty;
  }
  ```

  析构函数删除了`List`，锁和条件变量。

-  Append(void *item)

  ```cpp
  void SynchList::Append(void *item){
      lock->Acquire();		// enforce mutual exclusive access to the list 
      list->Append(item);
      listEmpty->Signal(lock);	// wake up a waiter, if any
      lock->Release();
  }
  ```
  
  在向列表中增加项前，`Append`会获取互斥锁。

  在增加项后则会使用`Signal`唤醒等待列表非空的线程，然后释放互斥锁。

- Remove()

  ```cpp
  void * SynchList::Remove(){
      void *item;
      lock->Acquire();			// enforce mutual exclusion
      while (list->IsEmpty())
  			listEmpty->Wait(lock);		// wait until list isn't empty
      item = list->Remove();
      ASSERT(item != NULL);
      lock->Release();
      return item;
  }
  ```

  在移除列表中的项前，`Remove`会获取互斥锁。

  如果列表为空，则当前线程进入条件变量的等待队列内，直到列表内有项为止。

  若列表内有项，`Remove`则从列表中取走项，并释放互斥锁。

- Mapcar(VoidFunctionPtr func)

  ```cpp
  void SynchList::Mapcar(VoidFunctionPtr func){ 
      lock->Acquire(); 
      list->Mapcar(func);
      lock->Release(); 
  }
  ```
  
  `Mapcar`函数包装了`List`的`Mapcar`方法，增加了获取与释放互斥锁的部分。

**Exercise 3  实现锁和条件变量**

*可以使用sleep和wakeup两个原语操作（注意屏蔽系统中断），也可以使用Semaphore作为唯一同步原语（不必自己编写开关中断的代码）。*

**锁**

```cpp
class Lock {
  ...
  private:
    char* name;				// for debugging
    Semaphore* mutex;  // [lab3] ptr to Semaphore
    Thread* owner;  // [lab3] used in isHeldByCurrentThread()
};
```

锁可以被看做一个加上占有者的二值信号量，因此为了实现的简洁性（主要是懒），我们把Lock作为信号量的wrapper实现。

在Lock中我们添加私有变量`mutex`信号量，以及线程指针`owner`，指向占用锁的线程。

- Lock(char* debugName)

  ```cpp
  Lock::Lock(char* debugName) {
      name = debugName;
      mutex = new Semaphore(debugName, 1);  // init with val 1
      owner = NULL;
  }
  ```

  在锁的构造函数内，我们将信号量`mutex`的值设为1（即锁为Free状态），并将`owner`设为空。

- ~Lock()

  ```cpp
  Lock::~Lock() {
      delete mutex;
  }
  ```

  析构函数手动释放`mutex`的内存。

- Acquire()

  ```cpp
  void Lock::Acquire() {
      IntStatus oldLevel = interrupt->SetLevel(IntOff);
      // begin critical zone
      mutex->P();
      owner = currentThread;
      // end critical zone
      (void) interrupt->SetLevel(oldLevel);
  }
  ```

  锁的`Acquire`过程是原语，不可中断，因此我们采用了关中断的方法放置获取锁的过程被打断。（这里的实现类似于信号量的`P`方法）

  在临界区内，我们对`mutex`进行P操作。锁的状态为Free，线程就获取锁，将`mutex`减1；锁的状态为Busy，线程就在此等待。

  获取锁成功之后，` owner`指针就会指向当前的进程。

- Release()

  ```cpp
  void Lock::Release() {
      IntStatus oldLevel = interrupt->SetLevel(IntOff);
      // begin critical zone
    	ASSERT(mutex->isHeldByCurrentThread());
      mutex->V();
      owner = NULL;
      // end critical zone
      (void) interrupt->SetLevel(oldLevel);
  }
  ```

  
  锁的`Release`过程同样不可中断，因此这里依然有关中断与恢复中断等级的操作。
  
  在临界区内，我们对`mutex`进行V操作，并将`owner`置为空，将锁释放。
  
  ASSERT语句会捕获进程意外释放了其它进程占用的锁的情况，以免出错。

- isHeldByCurrentThread()

  ```cpp
  bool Lock::isHeldByCurrentThread() {
      return currentThread == owner;
  }
  ```


  `isHeldByCurrentThread`函数判断当前线程是否是占有锁的线程，以免锁被另外的进程访问而导致死锁。

**条件变量**

```cpp
class Condition {
	...
  private:
    char* name;
    List* queue;  // [lab3] ptr to waitQueue
};
```

等待条件变量的进程需要在队列内等待，因此我们参考了信号量类的设计，加入等待队列`queue`。

- Condition(char* debugName)

  ```cpp
  Condition::Condition(char* debugName) { 
      name = debugName;
      queue = new List();
  }
  ```

  构造函数会对`queue`进行初始化。

- ~Condition()

  ```cpp
  Condition::~Condition() { 
      delete queue;
  }
  ```

  析构函数会释放`queue`的内存空间。

- Wait(Lock* conditionLock)

  ```cpp
  void Condition::Wait(Lock* conditionLock) { 
      IntStatus oldLevel = interrupt->SetLevel(IntOff);
      // begin critical zone
      conditionLock->Release();  // release the lock
      // from Semaphore::P()  // relinquish the CPU until signaled
      queue->Append((void *)currentThread);
  		currentThread->Sleep();
      conditionLock->Acquire(); // re-acquire the lock
      // end critical zone
      (void) interrupt->SetLevel(oldLevel);
  }
  ```

  为了使对`queue`的操作不被打断，我们在这里也引入了关中断与恢复中断等级的操作。

  如果线程需要在信号量处等待，则需要进行与信号量`P`函数类似的操作——将当前线程的指针`currentThread`加入信号量等待队列`queue`中，然后将当前线程置为阻塞态。

  然而这里我们注意到，为了保证互斥性，在`Wait`操作前，线程需要先获取一个锁。如果线程在进入睡眠前不释放这个锁，则需要获取这个锁的其它线程不会运行；若进行`Signal`操作的线程也需要这个锁，则很可能会导致死锁。因此，在线程进入睡眠前，`Wait`需要先释放这个锁，当进程恢复运行时在重新获取这个锁。因此，这里会有对`conditionLock`的`Release`和`Acquire`操作。

- Signal(Lock* conditionLock)

  ```cpp
  void Condition::Signal(Lock* conditionLock) { 
      Thread* thread;
      IntStatus oldLevel = interrupt->SetLevel(IntOff);
      // begin critical zone
      
      ASSERT(conditionLock->isHeldByCurrentThread());
      // from Semaphore::V()
      thread = (Thread *)queue->Remove();
      if (thread != NULL)	 { 
          scheduler->ReadyToRun(thread);
      }  
      // end critical zone
      (void) interrupt->SetLevel(oldLevel);
  }
  ```
  
  为了使对`queue`的操作不被打断，我们在这里也引入了关中断与恢复中断等级的操作。
  
  NachOS的条件变量实现与Mesa管程类似，因此`Signal`函数不会阻塞进程。在这里我们并不需要释放锁与重新获取锁的操作，但是为了程序的鲁棒性考虑，加入了对锁所有者的检查。
  
  当需要从等待队列中唤醒线程时，我们需要进行与信号量`V`函数类似的操作——先从等待队列中取出队首，如果队首存在将其状态修改为就绪，稍后继续执行。
  
- Broadcast(Lock* conditionLock)

  ```cpp
  void Condition::Broadcast(Lock* conditionLock) { 
      Thread* thread;
      IntStatus oldLevel = interrupt->SetLevel(IntOff);
      // begin critical zone
      ASSERT(conditionLock->isHeldByCurrentThread());
      // from Semaphore::P()
      thread = (Thread*) queue->Remove();
      while (thread != NULL)	 {
          scheduler->ReadyToRun(thread);
          thread = (Thread *)queue->Remove();
      }  
      // end critical zone
      (void) interrupt->SetLevel(oldLevel);
  }
  ```

  `Broadcast`方法的实现与`Signal`非常类似。在这里我们把`if`修改为`while`，使`Broadcast`方法能够将等待队列内的所有线程状态修改为就绪。

**Exercise 4 实现同步互斥实例**

*基于Nachos中的信号量、锁和条件变量，采用两种方式实现同步和互斥机制应用（其中使用条件变量实现同步互斥机制为必选题目）。具体可选择“生产者-消费者问题”、“读者-写者问题”、“哲学家就餐问题”、“睡眠理发师问题”等。（也可选择其他经典的同步互斥问题）*

**生产者-消费者问题**

有一个有限大小的缓冲区，有两种线程——生产者与消费者。生产者会向缓冲区队列内放入元素，若队列已满则等待；消费者会从缓冲区队列中取出元素，若队列已空则等待。

**使用信号量和锁**

为了使解法看上去更加高级一点，我们将对缓冲区的操作封装为`Buffer`类中的方法。

```cpp
class Buffer{
    public:
        Buffer(int bufferSize);
        ~Buffer();
        void insertItem(int newelement);
        int popItem();
        unsigned int bufferUsed(){return buffer->NumInList();}
        unsigned int bufferTotal(){return bufferSize;}
        void printBuffer();
    private:
        List *buffer;  // Using linked list List* to avoid stack overflow
        // semaphores
        Semaphore *empty;
        Semaphore *full;
        // locks
        Lock* mutex;
        // vars for debug
        unsigned int bufferSize;
};
```

在`Buffer`类中含有一个`List`型的缓冲区队列`buffer`。为了实现简洁，`queue`内元素都为int型。

信号量`empty`的值为当前缓冲区内空闲的个数，`full`的值为当前缓冲区内元素的个数。锁`mutex`用于保护对缓冲区`queue`的访问。

- Buffer(int bufferSize)

  ```cpp
  Buffer::Buffer(int bufferSize):bufferSize(bufferSize){
      buffer = new List;
      empty = new Semaphore("empty", bufferSize);
      full = new Semaphore("full", 0);
      mutex = new Lock("bufferLock");
  }
  ```

  构造函数会初始化`buffer`，`empty`，`full`和`mutex`，并通过参数设置`bufferSize`。

- ~Buffer()

  ```cpp
  Buffer::~Buffer(){
      delete empty, full, buffer;
  }
  ```

  析构函数会从内存中释放`empty`，`full`和`buffer`。

- printBuffer()

  ```cpp
  void bufferPrint(int ptr){
      printf("%d,",ptr);
  }
  ```

  ```cpp
  void Buffer::printBuffer(){
      printf("used=%d/%d elements=[",bufferUsed(),bufferSize);
      buffer->Mapcar(bufferPrint); // apply bufferPrint to all elements in buffer
      printf("]\n");
  }
  ```

  `printBuffer`函数会打印出当前buffer队列内的所有元素。这里我们利用了`List`类的`Mapcar`方法，对其中每一个元素运行一次`bufferPrint`。

- insertItem(int newelement)

  ```cpp
  void Buffer::insertItem(int newelement){
      empty->P();
      mutex->Acquire();
  
      if ((void*)newelement == NULL){
          printf("[Buffer::insertItem] You may not insert NULL to a List\n");
          ASSERT(FALSE);
      }
  
      buffer->Append((void*)newelement);
  
      unsigned int _bufferUsed = bufferUsed();
      if(_bufferUsed > bufferSize){
          printf("[Buffer::insertItem] buffer overflow: %d/%d\n",_bufferUsed, bufferSize);
          ASSERT(FALSE);
      }
  
      mutex->Release();
      full->V();
  }
  ```

  当向缓冲区内插入元素时，首先要检查缓冲区内有无空位，如果没有则需要等待。我们用`empty->P()`实现。

  当`insertItem`发现缓冲区内有空位时，必须要先获取`mutex`，否则多个线程同时访问`queue`会造成问题。

  插入元素完成后，`insertItem`会释放`mutex`，并通知正在等待的消费者现在队列内多了一个元素。我们用`full->V()`实现。

  如果我们的实现出现了问题，`insertItem`会检查`buffer`有无溢出，并且通过`ASSERT`终止运行。考虑到`popItem`的实现，我们不能插入转换成`void*`后值为`NULL`的元素。

- popItem()

  ```cpp
  int Buffer::popItem(){
      full->P();
      mutex->Acquire();
      
      void* temp = buffer->Remove();
      
      if(temp == NULL){
          printf("[Buffer::insertItem] No Element in Buffer\n");
          ASSERT(FALSE);
      }
      int topelement = (int) temp;
  
      mutex->Release();
      empty->V();
  
      return topelement;
  }
  ```

  在从缓冲区内拿元素前，首先要检查缓冲区内有无元素，如果没有则需要等待。我们用`full->P()`实现。

  `popItem`在从`queue`内取出队首前需要先获取`mutex`，在取完之后释放`mutex`，从而避免多个线程同时访问`queue`造成问题。

  当元素被取出后，`popItem`需要通知正在等待的生产者队列中多了一个空位。我们用`empty->V()`实现。

  如果我们的实现出现了问题，`popItem`会检查取出的元素是否为`NULL`，并通过`ASSERT`终止运行。

**测试**

```cpp
void producerThread(int ptr){
    int times = 5;
    while(times--){
        Buffer *currBuffer = (Buffer*) ptr;
        int item = Random()%114514+1;  // generate item
        printf("(%s) attemting to insert %d to list\n", currentThread->getName(), item);
        currBuffer->insertItem(item);
        printf("(%s) inserted item %d to list, Buffer ",currentThread->getName(), item);
        currBuffer->printBuffer();

        interrupt->OneTick(); // make time going
    }
}
```

每一个生产者线程会尝试向队列内插入5个元素。在每次插入之前，生产者线程会使用`Random()%114514+1`生成一个随机的int作为插入的元素。插入成功后，会调用`printBuffer()`打印当前缓冲区队列内的所有元素，并调用`interrupt->OneTick()`使时间流动。（这里不得不说NachOS的TimerTicks机制非常谔谔，，，）

```cpp
void consumerThread(int ptr){
    int times = 5;
    while(times--){
        Buffer *currBuffer = (Buffer*) ptr;
        printf("(%s) trying to pop item\n",currentThread->getName());
        int item = currBuffer->popItem();
        printf("(%s) got item %d from list, Buffer ",currentThread->getName(), item);
        currBuffer->printBuffer();

        interrupt->OneTick(); // make time going
    }
}
```

每一个消费者线程会尝试从队列内取出5个元素。在取出元素后，消费者线程会打印出取出的元素，调用`printBuffer()`打印当前缓冲区队列内的所有元素，调用`interrupt->OneTick()`使时间流动。

```cpp
void Lab3Test1(){
    printf("Entering Test8\n");
    Buffer *currBuffer = new Buffer(3);

    Thread *p1 = new Thread("Producer 1");
    Thread *p2 = new Thread("Producer 2");
    Thread *c1 = new Thread("Consumer 1");
    Thread *c2 = new Thread("Consumer 2");

    p1->Fork(producerThread, (void*) currBuffer);
    p2->Fork(producerThread, (void*) currBuffer);
    c2->Fork(consumerThread, (void*) currBuffer);
    c1->Fork(consumerThread, (void*) currBuffer);
}
```

`Lab3Test1`中，我们创建了大小为3的缓冲区`currBuffer`，两个生产者`p1`、`p2`，两个消费者`c1`，`c2`共同使用这个缓冲区。以下是测试的结果：

```
(Producer 1) attemting to insert 6799 to list
(Producer 1) inserted item 6799 to list, Buffer used=1/3 elements=[6799,]
(Producer 1) attemting to insert 99856 to list
(Producer 1) inserted item 99856 to list, Buffer used=2/3 elements=[6799,99856,]
(Producer 1) attemting to insert 54687 to list
(Producer 1) inserted item 54687 to list, Buffer used=3/3 elements=[6799,99856,54687,]
(Producer 1) attemting to insert 18793 to list
(Producer 2) attemting to insert 16449 to list
(Consumer 2) trying to pop item
(Consumer 2) got item 6799 from list, Buffer used=2/3 elements=[99856,54687,]
(Consumer 2) trying to pop item
(Consumer 2) got item 99856 from list, Buffer used=1/3 elements=[54687,]
(Consumer 2) trying to pop item
(Consumer 2) got item 54687 from list, Buffer used=0/3 elements=[]
(Consumer 2) trying to pop item
(Consumer 1) trying to pop item
(Producer 1) inserted item 18793 to list, Buffer used=1/3 elements=[18793,]
(Producer 1) attemting to insert 78479 to list
(Producer 1) inserted item 78479 to list, Buffer used=2/3 elements=[18793,78479,]
(Producer 2) inserted item 16449 to list, Buffer used=3/3 elements=[18793,78479,16449,]
(Producer 2) attemting to insert 50382 to list
(Consumer 2) got item 18793 from list, Buffer used=2/3 elements=[78479,16449,]
(Consumer 2) trying to pop item
(Consumer 2) got item 78479 from list, Buffer used=1/3 elements=[16449,]
(Consumer 1) got item 16449 from list, Buffer used=0/3 elements=[]
(Consumer 1) trying to pop item
(Producer 2) inserted item 50382 to list, Buffer used=1/3 elements=[50382,]
(Producer 2) attemting to insert 71808 to list
(Producer 2) inserted item 71808 to list, Buffer used=2/3 elements=[50382,71808,]
(Producer 2) attemting to insert 13223 to list
(Producer 2) inserted item 13223 to list, Buffer used=3/3 elements=[50382,71808,13223,]
(Producer 2) attemting to insert 69989 to list
(Consumer 1) got item 50382 from list, Buffer used=2/3 elements=[71808,13223,]
(Consumer 1) trying to pop item
(Consumer 1) got item 71808 from list, Buffer used=1/3 elements=[13223,]
(Consumer 1) trying to pop item
(Consumer 1) got item 13223 from list, Buffer used=0/3 elements=[]
(Consumer 1) trying to pop item
(Producer 2) inserted item 69989 to list, Buffer used=1/3 elements=[69989,]
(Consumer 1) got item 69989 from list, Buffer used=0/3 elements=[]
```

在测试过程中，`insertItem`和`popItem`都没有终止运行，说明我们的实现没有出现问题。

在输出中，我们可以看到`Producer 1`和`Producer 2`在缓冲区内没有空位时都会等待消费者取走元素再运行，而`Consumer 1`发现队列中没有元素时也会等待生产者插入元素再运行。

**使用条件变量和锁**

```cpp
class Buffer{
   ...
    private:
        List *buffer;  // Using linked list List* to avoid stack overflow
        // semaphores
        Condition *waitForSpace;
        Condition *waitForElement;
        // locks
        Lock* mutex;
        unsigned int bufferUsed;  // you need to protect this var
        unsigned int bufferSize;
};
```

如果我们能够用条件变量实现类似于信号量的功能，那这一问题就迎刃而解。然而条件变量并没有值，因此我们加入了`bufferUsed`来存储当前缓冲区队列内的元素个数。为了保证互斥性，对`bufferUsed`的访问和修改必须在获取`mutex`后才能进行。

```cpp
void Buffer::insertItem(int newelement){
    mutex->Acquire();

    if ((void*)newelement == NULL){
        printf("[Buffer::insertItem] You may not insert NULL to a List\n");
        ASSERT(FALSE);
    }

    // With Mesa-like condition, thread has to check again condition itself
    while (bufferUsed == bufferSize){
        waitForSpace->Wait(mutex);
    }

    buffer->Append((void*)newelement);

    bufferUsed++;

    waitForElement->Broadcast(mutex);

    if(bufferUsed > bufferSize){
        printf("[Buffer::insertItem] buffer overflow: %d/%d\n",bufferUsed, bufferSize);
        ASSERT(FALSE);
    }
    mutex->Release();
}
```

在`insertItem`的过程中，我们必须通过`mutex`保证对`queue`和`bufferUsed`的操作是不会被打断的，因此加入了获取`mutex`和释放`mutex`的操作。

生产者会在临界区内获取`bufferUsed`的值，如果缓冲区满了，当前线程就会到`waitForSpace`条件变量处等待。（`Wait`操作中包含释放锁与重新获取锁的过程）需要注意到，NachOS的条件变量实现类似Mesa管程，被唤醒的线程很有可能不会立即上CPU。因此，在线程被唤醒之后，`while`循环会再检查一次缓冲区有没有满，如果缓冲区满了则会再次进入`Wait`。

如果缓冲区没有满，生产者则会向缓冲区内插入元素，修改`bufferUsed`。插入元素后，线程会通知所有在`waitForElement`等待的消费者，现在的缓冲区不是空的，将它们的状态修改为就绪。

```cpp
int Buffer::popItem(){
    mutex->Acquire();
    
    // With Mesa-like condition, thread has to check again condition itself
    while (bufferUsed == 0){
        waitForElement->Wait(mutex);
    }

    void* temp = buffer->Remove();

    bufferUsed--;

    waitForSpace->Broadcast(mutex);

    if(temp == NULL){
        printf("[Buffer::insertItem] No Element in Buffer\n");
        ASSERT(FALSE);
    }
    
    mutex->Release();

    int topelement = (int) temp;
    return topelement;
}
```

在`popItem`的过程中，我们必须通过`mutex`保证对`queue`和`bufferUsed`的操作是不会被打断的，因此加入了获取`mutex`和释放`mutex`的操作。

消费者会在临界区内获取`bufferUsed`的值，如果缓冲区内没有元素，当前线程就会到`waitForElement`条件变量处等待。同样，这里需要`while`循环再检查一次缓冲区有没有空，如果缓冲区空了则会再次进入`Wait`。

如果缓冲区没有空，消费者则会从队列队首取出元素，修改`bufferUsed`。取出元素后，线程会通知所有在`waitForSpace`等待的生产者，现在的缓冲区不是满的，将它们的状态修改为就绪。

**测试**

测试代码与之前使用条件变量的实现一样。事实上，我们得到的结果也是相同的：

```
(Producer 1) attemting to insert 24181 to list
(Producer 1) inserted item 24181 to list, Buffer used=1/3 elements=[24181,]
(Producer 1) attemting to insert 44381 to list
(Producer 1) inserted item 44381 to list, Buffer used=2/3 elements=[24181,44381,]
(Producer 1) attemting to insert 93468 to list
(Producer 1) inserted item 93468 to list, Buffer used=3/3 elements=[24181,44381,93468,]
(Producer 1) attemting to insert 70020 to list
(Producer 2) attemting to insert 76315 to list
(Consumer 2) trying to pop item
(Consumer 2) got item 24181 from list, Buffer used=2/3 elements=[44381,93468,]
(Consumer 2) trying to pop item
(Consumer 2) got item 44381 from list, Buffer used=1/3 elements=[93468,]
(Consumer 2) trying to pop item
(Consumer 2) got item 93468 from list, Buffer used=0/3 elements=[]
(Consumer 2) trying to pop item
(Consumer 1) trying to pop item
(Producer 1) inserted item 70020 to list, Buffer used=1/3 elements=[70020,]
(Producer 1) attemting to insert 84435 to list
(Producer 1) inserted item 84435 to list, Buffer used=2/3 elements=[70020,84435,]
(Producer 2) inserted item 76315 to list, Buffer used=3/3 elements=[70020,84435,76315,]
(Producer 2) attemting to insert 65745 to list
(Consumer 2) got item 70020 from list, Buffer used=2/3 elements=[84435,76315,]
(Consumer 2) trying to pop item
(Consumer 2) got item 84435 from list, Buffer used=1/3 elements=[76315,]
(Consumer 1) got item 76315 from list, Buffer used=0/3 elements=[]
(Consumer 1) trying to pop item
(Producer 2) inserted item 65745 to list, Buffer used=1/3 elements=[65745,]
(Producer 2) attemting to insert 83714 to list
(Producer 2) inserted item 83714 to list, Buffer used=2/3 elements=[65745,83714,]
(Producer 2) attemting to insert 64610 to list
(Producer 2) inserted item 64610 to list, Buffer used=3/3 elements=[65745,83714,64610,]
(Producer 2) attemting to insert 16276 to list
(Consumer 1) got item 65745 from list, Buffer used=2/3 elements=[83714,64610,]
(Consumer 1) trying to pop item
(Consumer 1) got item 83714 from list, Buffer used=1/3 elements=[64610,]
(Consumer 1) trying to pop item
(Consumer 1) got item 64610 from list, Buffer used=0/3 elements=[]
(Consumer 1) trying to pop item
(Producer 2) inserted item 16276 to list, Buffer used=1/3 elements=[16276,]
(Consumer 1) got item 16276 from list, Buffer used=0/3 elements=[]
```

与之前结果相比只有随机生成的元素发生了变化。这说明我们的实现没有问题。

**Challenge 1 实现barrier**

*可以使用Nachos 提供的同步互斥机制（如条件变量）来实现barrier，使得当且仅当若干个线程同时到达某一点时方可继续执行。*

首先，我们需要明白的一个问题是barrier是什么。c++20标准库实现了`std::barrier`，其功能描述如下：

STL类模板 `std::barrier` 提供允许至多为期待数量的线程阻塞直至期待数量的线程到达该屏障。

我们把以上定义翻译成人话：

百京带学的自习教室需要3个人申请才能使用。当一位同学想使用自习教室时，只有当已经有2位同学在等待时，他可以和其它2位同学一起用自习教室；否则他只能在门口等，直到等待人数凑够3人为止。

为了使我们的解法显得高级，这里还是用类对`barrier`的方法做了封装。

```cpp
class Barrier{
    public:
      Barrier(int _threadsToWait);
      ~Barrier();
      void arrive_and_wait();
    private:
      char *name;
      Condition *barrier;
      Lock *mutex;  // protect threadsArrived
      int threadsToWait;
      int threadsArrived;
};
```

这里我们用条件变量`barrier`实现线程的等待与唤醒，而`threadsToWait`记录需要等待的总线程数，`threadsArrived`记录已经调用`arrive_and_wait`的线程数。

- Barrier(int _threadsToWait)

  ```cpp
  Barrier::Barrier(int _threadsToWait){
      threadsToWait = _threadsToWait;
      threadsArrived = 0;
      mutex = new Lock("barrierLock");
      barrier = new Condition("barrierCondition");
  }
  ```

  构造函数中初始化`mutex`和`barrier`，从参数获取`threadsToWait`，并设置`threadsArrived`的初值为0。

- ~Barrier()

  ```cpp
  Barrier::~Barrier(){
      delete mutex, barrier;
  }
  ```

  析构函数中释放`mutex`和`barrier`。

- arrive_and_wait()

  ```cpp
  void Barrier::arrive_and_wait(){
      IntStatus oldLevel = interrupt->SetLevel(IntOff);
      mutex->Acquire();
      // begin critical zone
      
      threadsArrived++;
      if(threadsArrived == threadsToWait){
          barrier->Broadcast(mutex);
          threadsArrived=0;
          printf("[Barrier::arrive_and_wait] Waking up threads\n");
      }else{
          barrier->Wait(mutex);
      }
  
      // end critical zone
      mutex->Release();
      (void) interrupt->SetLevel(oldLevel);
  }
  ```

  不能有多个线程同时访问`threadsArrived`变量，否则会出现明显的问题。因此这里我们禁用了中断，并用`mutex`来保护`threadsArrived`变量。

  当线程调用`arrive_and_wait`时，线程会先把`threadsArrived`加1。

  如果到达的线程数还不够，当前线程会在`barrier`处等待。(这里有解锁再加锁的过程)

  如果等待的线程数已经到了`threadsToWait`，那当前线程会唤醒所有在`barrier`等待的线程，并且将`threadsArrived`再置为0。

**测试**

```cpp
void Lab3Thread1(int ptr){
    Barrier* barrier = (Barrier*)ptr;
    for(int i = 1; i <= 5; i++){
        printf("(%s) Reaching Stage %d\n", currentThread->getName(), i);
        barrier->arrive_and_wait();
    }
}
```

`Lab3Thread1`线程中，每一个线程执行5次`arrive_and_wait()`。

```cpp
void Lab3Test2(){
    Barrier *currBarrier = new Barrier(3);

    Thread *p1 = new Thread("Thread 1");
    Thread *p2 = new Thread("Thread 2");
    Thread *p3 = new Thread("Thread 3");
    Thread *p4 = new Thread("Thread 4");

    p1->Fork(Lab3Thread1, (void*) currBarrier);
    p2->Fork(Lab3Thread1, (void*) currBarrier);
    p3->Fork(Lab3Thread1, (void*) currBarrier);
    p4->Fork(Lab3Thread1, (void*) currBarrier);
}
```

我们让barrier每一次等待3个线程，并创建了4个这样的线程，运行结果如下：

```
(Thread 1) Reaching Stage 1
(Thread 2) Reaching Stage 1
(Thread 3) Reaching Stage 1
[Barrier::arrive_and_wait] Waking up threads
(Thread 3) Reaching Stage 2
(Thread 4) Reaching Stage 1
(Thread 1) Reaching Stage 2
[Barrier::arrive_and_wait] Waking up threads
(Thread 1) Reaching Stage 3
(Thread 2) Reaching Stage 2
(Thread 3) Reaching Stage 3
[Barrier::arrive_and_wait] Waking up threads
(Thread 3) Reaching Stage 4
(Thread 4) Reaching Stage 2
(Thread 1) Reaching Stage 4
[Barrier::arrive_and_wait] Waking up threads
(Thread 1) Reaching Stage 5
(Thread 2) Reaching Stage 3
(Thread 3) Reaching Stage 5
[Barrier::arrive_and_wait] Waking up threads
(Thread 4) Reaching Stage 3
(Thread 2) Reaching Stage 4
```

我们能够看出，每一次3个线程到达时，`barrier`会唤醒所有在等待的线程。最后只有2个线程到达`arrive_to_wait`，因此无法继续执行。

#### 遇到的困难以及收获

1. gdb的使用

   在编写生产者消费者问题的`Buffer`方法时，我很不幸地遇到了NachOS意外退出的错误。由于NachOS运行在docker内，采用GUI工具进行调试显得十分不现实。为此，我又去查阅了gdb的文档，回想起了gdb的`set args`命令，如何查看调用栈与内存的值等操作。最后发现，是`Condition`的构造函数中没有对`List`进行初始化，导致`queue`指向空指针。（非常奇怪这在之前的操作中没有发生问题）。使用`gdb`就可以能够明确的发现段错误来自于`Signal`函数，并且`queue`指针的值为`NULL`。

<img src="/Users/Apple/Desktop/屏幕快照 2020-11-19 上午1.25.29.png" alt="屏幕快照 2020-11-19 上午1.25.29" style="zoom:50%;" />

<center>[图1] gdb的运行示例</center>



#### 对课程或Lab的意见和建议

1. 求求助教把DDL时间定的阳间一点。。。。 

#### 参考文献

[volatile修饰符的作用](https://www.zhihu.com/question/23014322)

[Linux Source](https://github.com/torvalds/linux/blob/master/include/asm-generic/atomic.h)

[Linux Documentation - Locking](https://github.com/torvalds/linux/tree/master/Documentation/locking)

[NachOS 线程同步机制](https://blog.csdn.net/wyxpku/article/details/52076209)

[std::barrier - C++中文参考文档](https://www.apiref.com/cpp-zh/cpp/thread/barrier.html)
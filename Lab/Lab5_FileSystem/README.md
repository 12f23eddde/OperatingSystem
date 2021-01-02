#### 任务完成情况

| Exercise 1 | Exercise 2 | Exercise 3 | Exercise 4 | Exercise 5 | Exercise 6 | Exercise 7 | Challenge 2 |
| ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ----------- |
| Y          | Y          | Y          | Y          | Y          | Y          | Y          | Y           |

#### Exercise 1 源代码阅读

> 阅读Nachos源代码中与文件系统相关的代码，理解Nachos文件系统的工作原理。

##### 1.0 NachOS文件系统综述

<img src="https://github.com/12f23eddde/OperatingSystem/raw/master/Lab/Lab5_FileSystem/NachosFileSystemStructure.jpg" alt="Nachos Study Book Nachos File System Structure" style="zoom:50%;" />

<center>NachOS文件系统结构</center>

###### Disk

正如我们之前提到过，NachOS是一个操作系统模拟器。在/macine/disk.h(cc)中，NachOS设计了一个虚拟磁盘Disk，对Unix的read()、write()等I/O操作了封装，从而模拟了磁盘的行为。

###### SynchDisk

由于磁盘I/O是异步事件，SynchDisk提供了将异步事件同步化的接口。`SynchDisk::ReadSector`与`SynchDisk::WriteSector`让线程先进入阻塞状态，等待到I/O操作完成后再返回。需要注意到，SynchDisk类中定义了互斥锁lock，在读写操作时都有加锁解锁的过程，因此同时只能有一个线程处在等待I/O状态。

##### 1.1 code/userprog/bitmap.h和code/userprog/bitmap.cc

在userprog/bitmap.h中，定义了位图类BitMap：

```cpp
class BitMap {
  public:
    BitMap(int nitems);		// Initialize a bitmap, with "nitems" bits initially, all bits are cleared.
    ~BitMap();			// De-allocate bitmap
    void Mark(int which);   	// Set the "nth" bit
    void Clear(int which);  	// Clear the "nth" bit
    bool Test(int which);   	// Is the "nth" bit set?
    int Find();            	// Return the # of a clear bit, and as a side effect, set the bit. 
														// If no bits are clear, return -1.
    int NumClear();		// Return the number of clear bits
    void Print();		// Print contents of bitmap
 
    void FetchFrom(OpenFile *file); 	// fetch contents from disk 
    void WriteBack(OpenFile *file); 	// write contents to disk

  private:
    int numBits;			// number of bits in the bitmap
    int numWords;			// number of words of bitmap storage (rounded up if numBits is not a multiple of the number of bits in a word）
    unsigned int *map;			// bit storage
};
```

在BitMap中，map指针指向bitmap对应的内存空间。目前NachOS中的bitmap是定长的，因此在创建时我们就需要设定bitmap有几个bit（numBits），然后构造函数算出bitmap需要占几个Bytes（numWords）。

###### BitMap()

```cpp
BitMap::BitMap(int nitems) { 
    numBits = nitems;
    numWords = divRoundUp(numBits, BitsInWord);
    map = new unsigned int[numWords];
    for (int i = 0; i < numBits; i++) Clear(i);
}
```

构造函数的参数是bitmap的位数。随后构造函数设置numWords（向上取整），为map分配内存空间，并将其所有位置为0。

###### ~BitMap()

```cpp
BitMap::~BitMap(){ 
    delete map;
}
```

析构函数释放map的内存空间。

###### Mark(n)

```cpp
void BitMap::Mark(int which) { 
    ASSERT(which >= 0 && which < numBits);
    map[which / BitsInWord] |= 1 << (which % BitsInWord);
}
```

检查位数n是否合法，然后将bitmap的第n位设为1。

###### Clear(n)

```cpp
void BitMap::Clear(int which) {
    ASSERT(which >= 0 && which < numBits);
    map[which / BitsInWord] &= ~(1 << (which % BitsInWord));
}
```

检查位数n是否合法，然后将bitmap的第n位设为0。

###### Test(n)

```cpp
bool BitMap::Test(int which){
    ASSERT(which >= 0 && which < numBits);
    if (map[which / BitsInWord] & (1 << (which % BitsInWord))) return TRUE;
    else return FALSE;
}
```

若第n位1，返回True；否则返回False。

###### Find()

```cpp
int BitMap::Find() {
    for (int i = 0; i < numBits; i++){
      if (!Test(i)) {
          Mark(i);
          return i;
      }
    }
    return -1;
}
```

类似于先前虚拟内存bitmap中的allocBit函数，Find函数在bitmap中寻找为0的位，如果存在则将该位置为1，返回该位的索引；如果不存在，返回-1。这里采用了顺序查找，速度可能不如allocBit采用的__builtin_ffs宏，不过显然运算效率并不是我们关注的重点。

###### NumClear()

```cpp
int BitMap::NumClear() {
    int count = 0;
    for (int i = 0; i < numBits; i++)
      if (!Test(i)) count++;
    return count;
}
```

NumClear返回bitmap中空闲位的个数。（也许也可以用位运算加速？）

###### FetchFrom(), WriteBack()

```cpp
void BitMap::FetchFrom(OpenFile *file) {
    file->ReadAt((char *)map, numWords * sizeof(unsigned), 0);
}
void BitMap::WriteBack(OpenFile *file){
   file->WriteAt((char *)map, numWords * sizeof(unsigned), 0);
}
```

FetchFrom从文件的0位置读取bitmap，WriteBack将bitmap写入到文件的0位置。



##### 1.2 code/filesys/filehdr.h和code/filesys/filehdr.cc

在NachOS中，FileHeader起到了和Unix的Inode一样的功能。

```cpp
class FileHeader {
  public:
    bool Allocate(BitMap *bitMap, int fileSize); // Initialize a file header, including allocating space on disk for the file data
    void Deallocate(BitMap *bitMap);  		// De-allocate this file's data blocks
    void FetchFrom(int sectorNumber); 	// Initialize file header from disk
    void WriteBack(int sectorNumber); 	// Write modifications to file header back to disk
    int ByteToSector(int offset);	// Convert a byte offset into the file to the disk sector containing the byte
    int FileLength();			// Return the length of the file in bytes
    void Print();			// Print the contents of the file.
  private:
    int numBytes;			// Number of bytes in the file
    int numSectors;			// Number of data sectors in the file
    int dataSectors[NumDirect];		// Disk sector numbers for each data block in the file
};
```

FileHeader中包含了文件的组织数据结构，当前的NachOS依然采用简单映射，因此文件的大小较为有限，仅为4KB。（我们会在之后的Execise解决这个问题）此外，FileHeader的最大大小不能超过一个块，这要求我们不能把文件的所有需要记录的属性全部塞进FileHeader里。

```cpp
#define NumDirect 	((SectorSize - 2 * sizeof(int)) / sizeof(int))
```

NumDirect宏计算出一个FileHeader中剩余的空间能放下多少个int型的指针，在这里对应着直接映射的磁盘块。

在FileHeader的私有变量中，numBytes为文件的大小，numSectors为文件占用的扇区数，dataSectors保存每一个数据块的索引。

###### Allocate()

```cpp
bool FileHeader::Allocate(BitMap *freeMap, int fileSize) {
    numBytes = fileSize;
    numSectors = divRoundUp(fileSize, SectorSize);
    if (freeMap->NumClear() < numSectors)
        return FALSE;        // not enough space

    for (int i = 0; i < numSectors; i++)
        dataSectors[ i ] = freeMap->Find();
    return TRUE;
}
```

Allocate函数在bitmap中寻找能够分配的空间，如果放得下，就修改BitMap，并将数据块的索引放入dataSectors；若放不下，就返回False。

###### Deallocate()

```cpp
void FileHeader::Deallocate(BitMap *freeMap) {
    for (int i = 0; i < numSectors; i++) {
        ASSERT(freeMap->Test((int) dataSectors[ i ]));  // ought to be marked!
        freeMap->Clear((int) dataSectors[ i ]);
    }
}
```

Deallocate函数将BitMap中当前文件所对应的位清除。

###### FetchFrom()

```cpp
void FileHeader::FetchFrom(int sector) {
    synchDisk->ReadSector(sector, (char *) this);
}
```

FetchFrom从对应的扇区读出当前对象的数据。

###### WriteBack()

```cpp
void FileHeader::WriteBack(int sector) {
    synchDisk->WriteSector(sector, (char *) this);
}
```

WriteBack将当前对象的数据写入对应的扇区。

###### ByteToSector()

```cpp
int FileHeader::ByteToSector(int offset) {
    return (dataSectors[ offset / SectorSize ]);
}
```

根据数据在文件中的偏移（单位为字节）找到对应的扇区号。

##### 1.3 code/filesys/openfile.h和code/filesys/openfile.cc

OpenFile类实现了与Unix中open()类似的功能。需要注意的是，我们在这里要禁用`FILESYS_STUB`宏，因为开启这个宏会使NachOS直接使用Unix系统调用。

```cpp
class OpenFile {
  public:
    OpenFile(int sector);		// Open a file whose header is located at "sector" on the disk
    ~OpenFile();			// Close the file
    void Seek(int position); 		// Set the position from which to start reading/writing -- UNIX lseek

    int Read(char *into, int numBytes); // Read/write bytes from the file, starting at the implicit position.
					// Return the # actually read/written, and increment position in file.
    int Write(char *from, int numBytes);
    int ReadAt(char *into, int numBytes, int position);
    			// Read/write bytes from the file, bypassing the implicit position.
    int WriteAt(char *from, int numBytes, int position);
    int Length(); 			// Return the number of bytes in the file (this interface is simpler than the UNIX idiom -- lseek to end of file, tell, lseek back)
  private:
    FileHeader *hdr;			// Header for this file 
    int seekPosition;			// Current position within the file
};
```

OpenFile的私有变量中保存了当前文件的FileHeader，以及偏移量seekPosition。

###### OpenFile()

```cpp
OpenFile::OpenFile(int sector) {
    hdr = new FileHeader;
    hdr->FetchFrom(sector);
    seekPosition = 0;
}
```

调用构造函数时根据扇区号从磁盘中载入FileHeader，初始的偏移量为0。

###### ~OpenFile()

```cpp
OpenFile::~OpenFile() {
    delete hdr;
}
```

调用析构函数时释放FileHeader占用的空间。

###### Seek()

```cpp
void OpenFile::Seek(int position) {
    seekPosition = position;
}
```

将偏移量置为position。

###### Read()

```cpp
int OpenFile::Read(char *into, int numBytes) {
    int result = ReadAt(into, numBytes, seekPosition);
    seekPosition += result;
    return result;
}
```

调用readAt，从seekPosition处读numBytes的数据到into，并修改seekPosition。

###### Write()

```cpp
int OpenFile::Write(char *into, int numBytes) {
    int result = WriteAt(into, numBytes, seekPosition);
    seekPosition += result;
    return result;
}
```

调用WriteAt，从into写numBytes的数据到seekPosition处，并修改seekPosition。

###### ReadAt()

```cpp
int OpenFile::ReadAt(char *into, int numBytes, int position) {
    int fileLength = hdr->FileLength();
    int i, firstSector, lastSector, numSectors;
    char *buf;

    if ((numBytes <= 0) || (position >= fileLength))
        return 0;                // check request
    if ((position + numBytes) > fileLength)
        numBytes = fileLength - position;

    firstSector = divRoundDown(position, SectorSize);
    lastSector = divRoundDown(position + numBytes - 1, SectorSize);
    numSectors = 1 + lastSector - firstSector;

    // read in all the full and partial sectors that we need
    buf = new char[numSectors * SectorSize];
    for (i = firstSector; i <= lastSector; i++)
        synchDisk->ReadSector(hdr->ByteToSector(i * SectorSize),
                              &buf[ (i - firstSector) * SectorSize ]);

    // copy the part we want
    bcopy(&buf[ position - (firstSector * SectorSize) ], into, numBytes);
    delete[] buf;
    return numBytes;
}
```

ReadAt的返回值是写入成功的字节数。如果写入的位置超出文件长度，那ReadAt就截掉超出长度的部分，因此调用ReadAt一定不会出现读写指针超出文件区域的情况。

随后，ReadAt先将文件的position转换为物理扇区号，再调用ReadSector，从磁盘中将有该文件数据的所有扇区读入缓冲区buf。随后，ReadAt将所有我们需要的数据复制到into中，最后释放buf的内存空间。

###### WriteAt()

```cpp
int OpenFile::WriteAt(char *from, int numBytes, int position) {
    int fileLength = hdr->FileLength();
    int i, firstSector, lastSector, numSectors;
    bool firstAligned, lastAligned;
    char *buf;

    if ((numBytes <= 0) || (position >= fileLength))
        return 0;                // check request
    if ((position + numBytes) > fileLength)
        numBytes = fileLength - position;
    DEBUG('f', "Writing %d bytes at %d, from file of length %d.\n",
          numBytes, position, fileLength);

    firstSector = divRoundDown(position, SectorSize);
    lastSector = divRoundDown(position + numBytes - 1, SectorSize);
    numSectors = 1 + lastSector - firstSector;

    buf = new char[numSectors * SectorSize];

    firstAligned = (position == (firstSector * SectorSize));
    lastAligned = ((position + numBytes) == ((lastSector + 1) * SectorSize));

// read in first and last sector, if they are to be partially modified
    if (!firstAligned)
        ReadAt(buf, SectorSize, firstSector * SectorSize);
    if (!lastAligned && ((firstSector != lastSector) || firstAligned))
        ReadAt(&buf[ (lastSector - firstSector) * SectorSize ], SectorSize, lastSector * SectorSize);

// copy in the bytes we want to change 
    bcopy(from, &buf[ position - (firstSector * SectorSize) ], numBytes);

// write modified sectors back
    for (i = firstSector; i <= lastSector; i++)
        synchDisk->WriteSector(hdr->ByteToSector(i * SectorSize),
                               &buf[ (i - firstSector) * SectorSize ]);
    delete[] buf;
    return numBytes;
}
```

首先，WriteAt与ReadAt相同，会检查读写区域是否超出文件区域，如果超出则做截取操作。

由于我们写入文件的位置可能不与扇区对齐，在写入文件前，WriteAt会将我们可能覆盖的扇区的数据写入到buf。

注意这里buf的大小与我们需要写入的所有扇区的大小相同，在bcopy将我们这次写入的数据复制到buf后，buf中的内容就与扇区中的新数据一致。最后，WriteAt调用WriteSector，将buf的内容写入磁盘，并释放buf的内存空间。

##### 1.4 code/filesys/directory.h和code/filesys/directory.cc

```cpp
class DirectoryEntry {
  public:
    bool inUse;				// Is this directory entry in use?
    int sector;				// Location on disk to find the FileHeader for this file 
    char name[FileNameMaxLen + 1];	// Text name for file, with +1 for the trailing '\0'
};
```

NachOS的目录项保存：

1. 目录项是否正在被使用？
2. 文件FileHeader所在的磁盘扇区索引
3. 文件名（注意这里的FileNameMaxLen限制了文件名的最大长度）

```cpp
class Directory {
  public:
    Directory(int size); 		// Initialize an empty directory with space for "size" files
    ~Directory();			// De-allocate the directory
    void FetchFrom(OpenFile *file);  	// Init directory contents from disk
    void WriteBack(OpenFile *file);	  // Write modifications to directory contents back to disk
    int Find(char *name);		// Find the sector number of the FileHeader for file: "name"
    bool Add(char *name, int newSector);  // Add a file name into the directory
    bool Remove(char *name);		// Remove a file from the directory
    void List();			// Print the names of all the files in the directory
    void Print();			// Verbose print of the contents of the directory -- all the file names and their contents.
  private:
    int tableSize;			// Number of directory entries
    DirectoryEntry *table;		// Table of pairs:  <file name, file header location> 
    int FindIndex(char *name);		// Find the index into the directory table corresponding to "name"
};
```

`DirectoryEntry *table`保存了当前文件夹中的文件目录，大小为tableSize。

###### Directory()

```cpp
Directory::Directory(int size){
    table = new DirectoryEntry[size];
    tableSize = size;
    for (int i = 0; i < tableSize; i++)
			table[i].inUse = FALSE;
}
```

构造函数初始化一个文件目录中的所有目录项。

###### ~Directory()

```cpp
Directory::~Directory(){ 
    delete [] table;
} 
```

析构函数删除一个文件目录。

###### FetchFrom(), WriteBack()

```cpp
void Directory::FetchFrom(OpenFile *file) {
    (void) file->ReadAt((char *) table, tableSize * sizeof(DirectoryEntry), 0);
}
void Directory::WriteBack(OpenFile *file) {
    (void) file->WriteAt((char *) table, tableSize * sizeof(DirectoryEntry), 0);
}
```

FetchFrom从目录文件的0位置开始读入文件目录，WriteBack将文件目录写入0位置。

###### FindIndex()

```cpp
int Directory::FindIndex(char *name) {
    for (int i = 0; i < tableSize; i++)
        if (table[ i ].inUse && !strncmp(table[ i ].name, name, FileNameMaxLen))
            return i;
    return -1;        // name not in directory
}
```

FindIndex函数在table中顺序遍历，寻找正在使用且文件名与name匹配的目录项，返回目录项的索引；找不到则返回-1。

###### Find()

```cpp
int Directory::Find(char *name) {
    int i = FindIndex(name);

    if (i != -1)
        return table[ i ].sector;
    return -1;
}
```

Find函数封装了FindIndex，返回name对应文件FileHeader所在的扇区号；若找不到，则返回-1。

###### Add()

```cpp
bool Directory::Add(char *name, int newSector) {
    if (FindIndex(name) != -1)
        return FALSE;

    for (int i = 0; i < tableSize; i++)
        if (!table[ i ].inUse) {
            table[ i ].inUse = TRUE;
            strncpy(table[ i ].name, name, FileNameMaxLen);
            table[ i ].sector = newSector;
            return TRUE;
        }
    return FALSE;    // no space.  Fix when we have extensible files.
}
```

在文件夹中添加文件。若已有重名文件存在，则返回False；否则则找到空闲目录项，设置sector与name。如果目录项全部用完，则返回False。

###### Remove()

```cpp
bool Directory::Remove(char *name) {
    int i = FindIndex(name);

    if (i == -1)
        return FALSE;        // name not in directory
    table[ i ].inUse = FALSE;
    return TRUE;
}
```

在文件夹中找到文件名与name匹配的目录项，并将inUse置为False。若找不到，则返回-1。

###### List()

```cpp
void Directory::List() {
    for (int i = 0; i < tableSize; i++)
        if (table[ i ].inUse)
            printf("%s\n", table[ i ].name);
}
```

列出当前文件夹中所有文件。

##### 1.5 code/filesys/filesys.h和code/filesys/filesys.cc

在filesys.cc中，包含如下定义：

```cpp
#define FreeMapSector 		0
#define DirectorySector 	1
```

在NachOS中，一个文件系统中bitmap文件的file header固定存放在第0个扇区，根目录文件的file header文件固定存放在第一个扇区。也就是说，一个NachOS文件系统的结构大致如下：

```
+----+----+---------------------+
| 0# | 1# | Normal Storage Area |
+----+----+---------------------+
```

在code/filesys/filesys.h中定义了NachOS的文件系统类FileSystem：

```cpp
class FileSystem {
  public:
    FileSystem(bool format);		// Initialize the file system.
    bool Create(char *name, int initialSize);  	
    OpenFile* Open(char *name); 	// Open a file (UNIX open)
    bool Remove(char *name);  		// Delete a file (UNIX unlink)
    void List();			// List all the files in the file system
    void Print();			// List all the files and their contents
  private:
   OpenFile* freeMapFile;		// Bit map of free disk blocks, represented as a file
   OpenFile* directoryFile;		// "Root" directory -- list of file names, represented as a file
};
```

`OpenFile* freeMapFile`  NachOS采用bitmap管理空闲块，freeMapFile是指向bitmap文件的指针。

`OpenFile* directoryFile` directoryFile是指向根目录文件的指针。

###### FileSystem()

```cpp
FileSystem::FileSystem(bool format){ 
    if (format) {
        BitMap *freeMap = new BitMap(NumSectors);
        Directory *directory = new Directory(NumDirEntries);
        FileHeader *mapHdr = new FileHeader;
        FileHeader *dirHdr = new FileHeader;
        freeMap->Mark(FreeMapSector);	    
        freeMap->Mark(DirectorySector);
        ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));
        ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize));
        mapHdr->WriteBack(FreeMapSector);    
        dirHdr->WriteBack(DirectorySector);
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
        freeMap->WriteBack(freeMapFile);
        directory->WriteBack(directoryFile);
    } else {
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }
}
```

当构造函数的format参数为True时，将会对文件系统进行格式化（初始化一个文件系统）。正如上文所示，NachOS文件系统中在数据区前有BitMap区与根目录区，因此在这里我们需要先创建BitMap和根目录文件，分配FileHeader，在BitMap中标记，并写入磁盘。

如果我们不格式化文件系统，那就从磁盘的第0扇区读入BitMap、第1扇区读入根目录，并打开。

###### Create()

```cpp
bool FileSystem::Create(char *name, int initialSize) {
    Directory *directory;
    BitMap *freeMap;
    FileHeader *hdr;
    int sector;
    bool success;
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    if (directory->Find(name) != -1)
        success = FALSE;            // file is already in directory
    else {
        freeMap = new BitMap(NumSectors);
        freeMap->FetchFrom(freeMapFile);
        sector = freeMap->Find();    // find a sector to hold the file header
        if (sector == -1)
            success = FALSE;        // no free block for file header 
        else if (!directory->Add(name, sector))
            success = FALSE;    // no space in directory
        else {
            hdr = new FileHeader;
            if (!hdr->Allocate(freeMap, initialSize))
                success = FALSE;    // no space on disk for data
            else {
                success = TRUE;    // everthing worked, flush all changes back to disk
                hdr->WriteBack(sector);
                directory->WriteBack(directoryFile);
                freeMap->WriteBack(freeMapFile);
            }
            delete hdr;
        }
        delete freeMap;
    }
    delete directory;
    return success;
}
```

Create函数在根目录中创建一个文件。Create主要执行了以下工作：

1. 从directoryFile读入根目录
2. 检查是否有重名文件；如果有，则返回False。
3. 从freeMapFile读入BitMap
4. 创建FileHeader，在BitMap中分配空间；分配失败则返回False。
5. 尝试为文件块分配空间；分配失败则返回False。
6. 将FileHeader，freeMapFile，directoryFile写回磁盘。

###### Open()

```cpp
OpenFile *FileSystem::Open(char *name) {
    Directory *directory = new Directory(NumDirEntries);
    OpenFile *openFile = NULL;
    int sector;
    directory->FetchFrom(directoryFile);
    sector = directory->Find(name);
    if (sector >= 0)
        openFile = new OpenFile(sector);    // name was found in directory
    delete directory;
    return openFile;                // return NULL if not found
}
```

Open函数在根目录中打开一个文件。Open主要执行了以下工作：

1. 从directoryFile读入根目录
2. 尝试从根目录中找到文件名对应的扇区号，如果找到则调用OpenFile打开该文件。
3. 如果查找失败，返回NULL；否则返回openFile。

###### Remove()

```cpp
bool FileSystem::Remove(char *name) {
    Directory *directory;
    BitMap *freeMap;
    FileHeader *fileHdr;
    int sector;

    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    sector = directory->Find(name);
    if (sector == -1) {
        delete directory;
        return FALSE;             // file not found
    }
    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    freeMap = new BitMap(NumSectors);
    freeMap->FetchFrom(freeMapFile);

    fileHdr->Deallocate(freeMap);        // remove data blocks
    freeMap->Clear(sector);            // remove header block
    directory->Remove(name);

    freeMap->WriteBack(freeMapFile);        // flush to disk
    directory->WriteBack(directoryFile);        // flush to disk
    delete fileHdr;
    delete directory;
    delete freeMap;
    return TRUE;
}
```

Remove函数在根目录中删除一个文件。Remove主要执行了以下工作：

1. 从directoryFile读入根目录。
2. 尝试从根目录中找到文件名对应的扇区号；如果失败，返回False。
3. 从freeMapFile读入BitMap。
4. 清除当前文件的数据块，当前文件的FileHeader，以及在目录文件中的目录项
5. 将freeMap和directory写入磁盘，并释放内存空间。

###### List()

```cpp
void FileSystem::List() {
    Directory *directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    directory->List();
    delete directory;
}
```

List函数从directoryFile读入根目录，并列出根目录中的所有文件。

#### Exercise 2 扩展文件属性

> 增加文件描述信息，如“类型”、“创建时间”、“上次访问时间”、“上次修改时间”、“路径”等等。尝试突破文件名长度的限制。

###### Part 1 增加文件描述信息

```cpp
enum FileType {normalFile, dirFile, bitmapFile};
static const char* FileTypeStr [] = {"File", "Dir", "BitMap"};
```

我们在这里定义了三种文件类型：普通文件，目录和BitMap。


```cpp
class FileHeader{
  	...
		// [lab5] Made it public for convenience
    FileType fileType;
    int timeCreated = -1;
    int timeModified = -1;
    int timeAccessed = -1;
}
```

在FileHeader中，我们增加文件类型、创建时间、上次访问时间、上次修改时间四个属性。

这里为了降低实现的复杂度，”路径”将会稍后在实现多级目录的过程中一并实现，而不作为FileHeader的属性。

```cpp
// [lab5] add 4 ints
#define NumDirect    ((SectorSize - 6 * sizeof(int)) / sizeof(int))
```

从FetchFrom, WriteTo函数中我们可以发现FileHeader的大小被限制在一个块。由于我们增加了4个int型变量，因此FileHeader中能够存放的索引项也需要相应减少。

```cpp
OpenFile::OpenFile(int sector) {
    hdr = new FileHeader;
    hdr->FetchFrom(sector);
    seekPosition = 0;

    hdrSector = sector;  // [lab5] mark hdr sector
    hdr->timeAccessed = stats->totalTicks;  // [lab5] set file attributes
    hdr->WriteBack(hdrSector);  // [lab5] write changes back to disk
}
```

打开文件时需要修改文件的上次访问时间。为了避免不实时写回文件头造成的问题，我们每一次需要修改文件头时都从磁盘中读入，修改后再写回。

```cpp
int OpenFile::ReadAt(char *into, int numBytes, int position) {
    ...
    // [lab5] set file attributes
    hdr->timeAccessed = stats->totalTicks;
    hdr->WriteBack(hdrSector);  // [lab5] write changes back to disk
    return numBytes;
}
```

读取文件时需要修改文件的上次访问时间。

```cpp
int OpenFile::WriteAt(char *from, int numBytes, int position) {
		...
    // [lab5] set file attributes
    hdr->timeAccessed = hdr->timeModified = stats->totalTicks;
    hdr->WriteBack(hdrSector);  // [lab5] write changes back to disk
    return numBytes;
}
```

写入文件时需要修改文件的上次修改时间。

```cpp
bool FileSystem::Create(char *name, int initialSize, FileType fileType) {
  ...
  // [lab5] set file attrbutes
  hdr->fileType = fileType;
  hdr->timeCreated = stats->totalTicks;

  DEBUG('D', "[Create] Creating file %s (%s), size %d, time %d\n",
        name, FileTypeStr[hdr->fileType], initialSize, hdr->timeCreated);
}
```

创建文件时需要修改文件的创建时间。

###### Part 1 测试

我们在FileHeader中增加Print()函数，打印文件属性：

```cpp
void FileHeader::Print() {
		...
    // [lab5] Modified
    printf("Type:%s Created @%d, Modified @%d, Accessed @%d\n", FileTypeStr[fileType], timeCreated,
           timeModified, timeAccessed);
}
```

运行命令：

`docker run -it nachos gdb --args nachos/nachos-3.4/code/filesys/nachos -f -d D -cp nachos/nachos-3.4/code/filesys/test/small shuwarin -p shuwarin`

我们得到以下结果：

```
Bit map file header:
Type:BitMap Created @10, Modified @241520, Accessed @241520
FileHeader contents.  File size: 128.  File blocks:
2 
File contents:
\7f\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0
Directory file header:
Type:Dir Created @10, Modified @210520, Accessed @468080
FileHeader contents.  File size: 200.  File blocks:
3 4 
File contents:
\1\0\0\0\5\0\0\0shuwarin\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0
\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0
Bitmap set:
0, 1, 2, 3, 4, 5, 6, 
Directory contents:
Name: shuwarin, Sector: 5
Type:File Created @176520, Modified @451520, Accessed @531550
FileHeader contents.  File size: 38.  File blocks:
6 
File contents:
This is the spring of our discontent.\a
```

文件类型、创建时间、上次访问时间、上次修改时间都能正确地设置和输出。

###### Part 2 突破文件名长度的限制

```cpp
// [lab5]
#define FileNameMaxLen        251   // for simplicity, we assume file names are <= 251 characters long
```

`FileNameMaxLen`宏限制了文件名的长度，原先的值是9。这里我们将其修改为251。

这里需要注意到，Directory与FileHeader不同，大小并没有被严格限制在一个块以内。（参考FetchFrom()和WriteBack()函数的实现），因此我们直接修改`FileNameMaxLen`并不会出现什么问题。

###### Part 2 测试

运行以下命令：

```
/bin/bash  /Users/Apple/Documents/GitHub/OperatingSystem/Lab/Lab0_BuildNachos/build_modified_nachos.sh && docker run -it nachos gdb --args nachos/nachos-3.4/code/filesys/nachos -f -d D -cp nachos/nachos-3.4/code/filesys/test/small shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa -p  shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa
```

```cpp
Directory file header:
Type:Dir Created @10, Modified @828020, Accessed @1095650
FileHeader contents.  File size: 2600.  File blocks:
3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 
File contents:
\1\0\0\0\18\0\0\0shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuw
arin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreaming_shuwashuwa_shuwarin_dreamin
g_s\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0
```

我们可以观察到文件能够被正确的创建。我们的文件名长度超出了一个块的大小，但这并不影响Directory类的工作。



#### Exercise 3 扩展文件长度

> 改直接索引为间接索引，以突破文件长度不能超过4KB的限制。

###### 实现

首先我们需要设计间接索引表的数据结构。

```cpp
// IndirectTable does NOT do mem/disk management
// FileHeader should alloc/dealloc when Alloc, Dealloc
struct IndirectTable {
    int dataSectors[NumIndirect];
    void printSectors(int cnt=NumIndirect);
};
```

为了简单起见，间接索引表的大小被限制在一个块，除了FileHeader所在的磁盘块号什么也不存。因此，间接索引表有`((SectorSize) / sizeof(int))`项。

```cpp
// [lab5] Indirect Index Table
// For convenience, the size of 1 table is the same as 1 sector
// | 0 | 1 | ... | n-2 | n-1 |
//   .   .          ,     ,
//                 ...   ...
#define NumIndirect  ((SectorSize) / sizeof(int))
#define MaxFileSize    ((NumDirect-2) * SectorSize + 2 * NumIndirect * SectorSize)
```

我们让原先的最后两个直接索引表项指向两张简介索引表（即”二级索引”），这便将文件的大小限制由4KB扩展到了11KB。

```cpp
void FileHeader::FetchFrom(int sector, char* dest) {
    if (!dest){
        synchDisk->ReadSector(sector, (char *) this);
    }else{
        DEBUG('D', "[FetchFrom] Reading idt from sector %d at dest %p\n", sector, dest);
        synchDisk->ReadSector(sector, dest);
    }

}
void FileHeader::WriteBack(int sector, char* dest) {
    if(!dest) {
        synchDisk->WriteSector(sector, (char *) this);
    }else{
        DEBUG('D', "[WriteBack] Writing idt to sector %d at dest %p\n", sector, dest);
        synchDisk->WriteSector(sector, dest);
    }
}
```

这里我们稍微修改了FetchFrom()和WriteBack()函数，使它们能对任意的内存地址进行写入和读取。

```cpp
bool FileHeader::Allocate(BitMap *freeMap, int fileSize) {
    numBytes = fileSize;
    numSectors = divRoundUp(fileSize, SectorSize);
    if (freeMap->NumClear() < numSectors + 2)
        return FALSE;        // not enough space

    // direct indexing
    int remainedSectors = numSectors;
    for (int i = 0; i < min(numSectors, NumDirect - 2); i++){
        dataSectors[i] = freeMap->Find();
        remainedSectors --;
    }

    // [NOTE!] numSectors MUST BE RIGHT, else catastrophic mem errors can happen
    // dataSectors[NumDirect - 2]: indirect indexing
    if(remainedSectors > 0){
        // alloc sector from disk
        IndirectTable * idt = new IndirectTable;
        int idtSector;
        if ((idtSector = freeMap->Find()) == -1) return FALSE;  // no more space
        dataSectors[NumDirect - 2] = idtSector;
        // set val of idt (stop alloc if remainedSectors = 0)
        for(int j = 0; j < NumIndirect && remainedSectors > 0; j++){
            idt->dataSectors[j] = freeMap->Find();
            remainedSectors --;
        }
        // write changes to disk
        WriteBack(idtSector, (char*) idt);
    }

    // dataSectors[NumDirect - 1]: indirect indexing
    if(remainedSectors > 0){
        // alloc sector from disk
        IndirectTable * idt = new IndirectTable;
        int idtSector;
        if ((idtSector = freeMap->Find()) == -1) return FALSE;  // no more space
        dataSectors[NumDirect - 1] = idtSector;
        // set val of idt (stop alloc if remainedSectors = 0)
        for(int j = 0; j < NumIndirect && remainedSectors > 0; j++){
            idt->dataSectors[j] = freeMap->Find();
            remainedSectors --;
        }
        // write changes to disk
        WriteBack(idtSector, (char*) idt);
    }
    if (remainedSectors > 0){
        printf("\033[31m[Create] Size %d (Sectors %d) exceeds max limit/\n\033[0m", numBytes, numSectors);
        ASSERT(FALSE);
    }
    return TRUE;
}
```

当我们创建文件调用FileHeader::Allocate函数分配remainedSectors个扇区时：

1. 首先分配`NumDirect - 2`项直接索引。
2. 如果不够，在磁盘上分配第一张间接索引表的空间。
3. 分配第一张间接索引表，直到没有扇区需要分配为止。
4. 如果还是不够，在磁盘上分配第二张间接索引表的空间。
5. 分配第二张间接索引表，直到没有扇区需要分配为止。
6. 如果仍然不够，说明文件大小超过上限，戳啦！

```cpp
void FileHeader::Deallocate(BitMap *freeMap) {

//    for (int i = 0; i < numSectors; i++) {
//        ASSERT(freeMap->Test((int) dataSectors[ i ]));  // ought to be marked!
//        freeMap->Clear((int) dataSectors[ i ]);
//    }

    // just another allocate
    int remainedSectors = numSectors;
    for (int i = 0; i < min(numSectors, NumDirect - 2); i++){
        freeMap->Clear(dataSectors[i]);
        remainedSectors --;
    }
    // dataSectors[NumDirect - 2]: indirect indexing
    if(remainedSectors > 0){
        // load idt from disk (shall not be a common usage)
        int idtSector = dataSectors[NumDirect - 2];
        IndirectTable *idt = new IndirectTable;
        FetchFrom(idtSector, (char*) idt);
        // free entry
        for(int j = 0; j < NumIndirect; j++){
            freeMap->Clear(idt->dataSectors[j]);
            remainedSectors --;
        }
        // free table
        freeMap->Clear(dataSectors[NumDirect - 2]);
    }
    // dataSectors[NumDirect - 1]: indirect indexing
    if(remainedSectors > 0){
        // load idt from disk (shall not be a common usage)
        int idtSector = dataSectors[NumDirect - 1];
        IndirectTable *idt = new IndirectTable;
        FetchFrom(idtSector, (char*) idt);
        // free entry
        for(int j = 0; j < NumIndirect; j++){
            freeMap->Clear(idt->dataSectors[j]);
            remainedSectors --;
        }
        // free table
        freeMap->Clear(idtSector);
    }
}
```

当我们创建文件调用FileHeader::Allocate函数释放numSectors个扇区时：

1. 首先释放`NumDirect - 2`项直接索引对应的扇区。
2. 如果没有释放完，从磁盘里加载第一张间接索引表。
3. 释放第一张间接索引表中表项对应的扇区，直到没有扇区需要释放为止。最后释放第一张间接索引表所在的扇区。
4. 如果还没有释放完，从磁盘里加载第二张间接索引表。
5. 释放第二张间接索引表中表项对应的扇区，直到没有扇区需要释放为止。最后释放第二张间接索引表所在的扇区。

```cpp
// [lab5] Modified to support indirect indexing
int FileHeader::ByteToSector(int offset) {
    if (0 <= offset && offset < (NumDirect - 2)*SectorSize){  // direct indexing
        return (dataSectors[ offset / SectorSize ]);

    }else if((NumDirect - 2)*SectorSize <= offset && offset < (NumDirect - 2)*SectorSize + NumIndirect*SectorSize){
        // load idt from disk (shall not be a common usage)
        // f*ck the perf
        int idtSector = dataSectors[NumDirect - 2];
        IndirectTable *idt = new IndirectTable;
        FetchFrom(idtSector, (char*) idt);

        int sectorNo = (offset - (NumDirect - 2)*SectorSize)/SectorSize;
        if (sectorNo >= NumIndirect){
            printf("\033[31m[ByteToSector] Invalid sector No %d\033[0m", sectorNo);
            ASSERT(FALSE);
        }
        return idt->dataSectors[sectorNo];

    }else if((NumDirect - 2)*SectorSize + NumIndirect*SectorSize <= offset && offset < (NumDirect - 2)*SectorSize + 2*NumIndirect*SectorSize){
        // load idt from disk (shall not be a common usage)
        // f*ck the perf
        int idtSector = dataSectors[NumDirect - 1];
        IndirectTable *idt = new IndirectTable;
        FetchFrom(idtSector, (char*) idt);

        int sectorNo = (offset - ((NumDirect - 2)*SectorSize + NumIndirect*SectorSize))/SectorSize;
        if (sectorNo >= NumIndirect){
            printf("\033[31m[ByteToSector] Invalid sector No %d\033[0m", sectorNo);
            ASSERT(FALSE);
        }
        return idt->dataSectors[sectorNo];

    }else{
        printf("\033[31m[ByteToSector] Invalid offset %d\033[0m", offset);
        ASSERT(FALSE);
    }
}
```

我们还需要修改获取文件地址对应扇区的BytesToSector函数。修改后的函数：

1. 如果查找的文件地址在直接索引覆盖的范围内，返回直接索引对应的扇区号。
2. 如果查找的文件地址在第一张间接索引表覆盖的范围内，从磁盘中载入第一张间接索引表，并在第一张间接索引表中查找对应的扇区号。
3. 如果查找的文件地址在第二张间接索引表覆盖的范围内，从磁盘中载入第二张间接索引表，并在第二张间接索引表中查找对应的扇区号。
4. 如果都不是，说明文件地址有误。

###### 测试

首先我们需要修改Print()函数，使之能够输出间接索引表对应的扇区的文件内容。

```cpp
void FileHeader::Print() {
    int i, j, k;
    char *data = new char[SectorSize];

    // [lab5] Modified
    printf("Type:%s Created @%d, Modified @%d, Accessed @%d\n", FileTypeStr[fileType], timeCreated, timeModified, timeAccessed);

    printf("FileHeader contents.  File size: %d/%d  File blocks:\n", numBytes, MaxFileSize);
//    for (i = 0; i < numSectors; i++)
//        printf("%d ", dataSectors[ i ]);
    int remainedSectors = numSectors;
    for (int i = 0; i < min(numSectors, NumDirect - 2); i++){
        printf("%d ", dataSectors[i]);
        remainedSectors --;
    }
    if(remainedSectors > 0){
        // load idt from disk (shall not be a common usage)
        int idtSector = dataSectors[NumDirect - 2];
        IndirectTable  *idt = new IndirectTable;
        FetchFrom(idtSector, (char*) idt);
        idt->printSectors();
    }
    if(remainedSectors > 0){
        // load idt from disk (shall not be a common usage)
        int idtSector = dataSectors[NumDirect - 1];
        IndirectTable  *idt = new IndirectTable;
        FetchFrom(idtSector, (char*) idt);
        // free entry
        idt->printSectors();
    }

    printf("\nFile contents:\n");
    for (i = k = 0; i < numSectors; i++) {
        int destSector = ByteToSector(i*SectorSize);
        synchDisk->ReadSector(destSector, data);
        printf("[Sector %3d] ", i);
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
            if (('\032' <= data[ j ] && data[ j ] <= '\176') || data[j] == '\n')   // isprint(data[j])
                printf("%c", data[ j ]);
            else
                printf("\\%x", (unsigned char) data[ j ]);
        }
        printf("\n");
    }
    delete[] data;
}
```

这里我们选用了一个8KB大小的文件（川普推特精粹.txt）作为测试用例。

```
Bitmap set:
0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 
Directory contents:
Name: shuwarin, Sector: 14
Type:File Created @340520, Modified @39112020, Accessed @55800020
FileHeader contents.  File size: 8034/11264  File blocks:
15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 [40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,]
[73,74,75,76,77,78,79,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,]
File contents:
[Sector   0] # Following are sentences from twitter account @realDonaldTrump
# A perfect source for meaningless words with strong emotion
# T
[Sector   1] hat doesn't indicate that the author share similar political opinions with @realDonaldTrump
1 GREATEST ELECTION FRAUD IN THE HIS
[Sector   2] TORY OF OUR COUNTRY!!!
...
[Sector  61] te the fact that I went from 63,000,000 Votes to 75,000,000 Votes, a record 12,000,000 Vote increase.
49 Obama went down 3,000,0
[Sector  62] 00 Votes, and won. Rigged Election!!!
```

从输出中我们可以看出，文件的磁盘块能被正确的分配，且其内容也能够被正确地输出。

#### Exercise 4 实现多级目录

###### 多级目录的创建

```cpp
// [lab5] dirSector for parent dir
Directory::Directory(int size, int dirSector) {
    ...
    // [lab5] 增加..项
    if(dirSector >= 0){
        DEBUG('D', "[Directory] Adding .. (%d)\n", dirSector);
        Add("..", dirSector);
    }
}
```

参照Unix文件系统，当我们创建一个文件夹时，需要增加".."项指向上一级目录（根目录除外）。当我们创建的目录不是根目录时，需要将dirSector设为一个非负值。

```cpp
bool FileSystem::Create(char *name, int initialSize, FileType fileType) {
 ...
  // [lab5] set file attrbutes
  hdr->fileType = fileType;
  hdr->timeCreated = stats->totalTicks;

  DEBUG('D', "[Create] Creating file %s (%s), size %d, time %d\n",
        name, FileTypeStr[hdr->fileType], initialSize, hdr->timeCreated);

  // everthing worked, flush all changes back to disk
  hdr->WriteBack(sector);
  directory->WriteBack(directoryFile);
  freeMap->WriteBack(freeMapFile);

  // [lab5] Init a new Directory
  if(fileType == dirFile){
    OpenFile* tempDirFile = new OpenFile(sector);
    Directory *newDir = new Directory(NumDirEntries, directoryFile->hdrSector);
    // Got to write back header before open
    newDir->WriteBack(tempDirFile);

    delete newDir;
    delete tempDirFile;
  }       
}
```

当我们调用FileSystem::Create创建文件夹时，需要创建一个目录文件，并向磁盘中写入这个目录文件。（不然".."项就不会存在，这里又是一个晚上）

###### 多级目录的删除

我对于NachOS中删除文件夹的理解类似于Unix操作系统中的"rm -r"命令，即递归删除文件夹中的所有子文件，最后删除文件夹本身。

```cpp
// [lab5] Remove all files in current directory
// like unix command rm -r, but NOT REMOVING CURRENT DIR
void Directory::RemoveAll(BitMap *freeMap) {
    FileHeader *hdr = new FileHeader;
    Directory* subDir = new Directory(NumDirEntries);
    OpenFile *subFile = NULL;
    for (int i = 0; i < tableSize; i++) {
        if (table[ i ].inUse) {
            // Can not remove ..
            if (!strcmp(table[i].name, "..")) continue;
            printf("Name: %s, Sector: %d\n", table[ i ].name, table[ i ].sector);
            hdr->FetchFrom(table[ i ].sector);

            if (hdr->fileType == dirFile){
                subFile = new OpenFile(table[i].sector);
                subDir = new Directory(NumDirEntries);
                subDir->FetchFrom(subFile);
                // recursively remove subdir
                subDir->RemoveAll(freeMap);
            }
            // remove data & hdr
            hdr->Deallocate(freeMap);
            freeMap->Clear(table[i].sector);
            // remove dir entry
            table[i].inUse = FALSE;

            DEBUG('D', "[RemoveAll] Removed %s\n", table[i].name);
        }
    }
    delete hdr;
    delete subDir;
    delete subFile;
}
```

Directory::RemoveAll函数会递归删除文件夹中的所有子文件：

1. 如果子文件是文件，直接删除。
2. 如果子文件是文件夹，递归调用文件夹的RemoveAll方法，然后删除子文件。
3. 如果子文件指向上一级目录，那就什么也不应该做。

需要注意的是，RemoveAll函数不会删除当前文件夹（我杀我自己显然不合理）。

```cpp
bool FileSystem::Remove(char *name) {
    ...
    // [lab5] Dealloc folder
    if(fileHdr->fileType == dirFile){
        subFile = new OpenFile(sector);
        subDir = new Directory(NumDirEntries);
        subDir->FetchFrom(subFile);
        // recursively remove subdir
        subDir->RemoveAll(freeMap);
    }

    fileHdr->Deallocate(freeMap);        // remove data blocks
    freeMap->Clear(sector);            // remove header block
    directory->Remove(name);
		...
}
```

我们还需要修改FileSystem的Remove方法。如果子文件是目录，那就先递归调用RemoveAll方法，再删除子文件。

###### 多级目录的切换

```cpp
// [lab5] ChangeDir: only supports relative path
bool FileSystem::ChangeDir(char *name) {
    DEBUG('D', "[ChangeDir] cd to %s\n", name);
    // Find target from current Dir
    Directory *dir = new Directory(NumDirEntries);
    dir->FetchFrom(directoryFile);
    int targetSector = dir->Find(name);
    if (targetSector == -1) {
        printf("[changeDir] %s not found in pwd\n", name);
        return FALSE; // Not Found
    }
    FileHeader* tempHdr = new FileHeader;
    tempHdr->FetchFrom(targetSector);
    if(tempHdr->fileType!=dirFile){
        printf("[changeDir] %s is not a dir\n", name);
        return FALSE; // Not Found
    }
    // Close current file then open new dirFile
    delete directoryFile;
    directoryFile = new OpenFile(targetSector);
    if(!directoryFile){
        printf("\033[31m[ChangeDir] Failed to load target sector %d\n\033[0m", targetSector);
        ASSERT(FALSE);
    }
    return TRUE;
}

```

我们注意到，原先NachOS的很多函数都会从directoryFile中读取文件目录，也就是说只要能更改directoryFile指向的文件，我们就实现了Current Working Directory的功能。

ChangeDir函数相当于Unix中的cd命令：

1. 首先在当前文件夹中找到目标文件的Header所在的扇区
2. 如果目标文件不存在或目标文件不是目录，返回False；如果目标文件存在且是目录，那就关闭当前的目录文件，并打开目标目录文件。

###### 多级目录的显示

```cpp
// [lab5] find Name by Sector
// NULL if not found
char * Directory::findNameBySector(int sector){
    for (int i = 0; i < tableSize; i++) {
        if (table[ i ].inUse && table[i].sector == sector) {
            return table[i].name;
        }
    }
    return NULL;
}
```

findNameBySector函数根据FileHeader的扇区号，从文件目录中查找文件的名字。

```cpp
// [lab5] get path for pwd
char * Directory::findPath() {
    int parentDirSector = Find("..");
    int subDirSector = hdrSector;
    char *subDirName = NULL;
    Directory *parentDir = new Directory(NumDirEntries);
    OpenFile *parentFile = NULL;
    char res[10][50];
    char * resStr = new char[200];
    memset(resStr, 0, sizeof(char)*200);
    strcat(resStr,"/");
    int i;

    for(i = 0; i < 10 && parentDirSector != -1; i++){
        delete parentFile;
        parentFile = new OpenFile(parentDirSector);
        parentDir->FetchFrom(parentFile);

        strcpy(res[i], parentDir->findNameBySector(subDirSector));
        DEBUG('D', "[FindPath] i=%d name=%s\n", i, res[i]);

        subDirSector = parentDirSector;
        parentDirSector = parentDir->Find("..");
    }

    for(; i > 0; i--){
        strcat(resStr, res[i-1]);
        strcat(resStr, "/");
    }

    return resStr;
}
```

FindPath函数递归调用遍历上级目录，最终找到当前目录的绝对路径，并返回。

```cpp
// [lab5] Modified
void Directory::List() {
    FileHeader *hdr = new FileHeader;
    char* pathStr = findPath();
    printf("[List] %s:\n", pathStr);
    printf("%-20s %-10s %-10s %-10s %-10s %-10s\n","NAME", "TYPE", "SIZE", "CREATED", "ACCESSED", "MODDED");
    for (int i = 0; i < tableSize; i++) {
        if (table[ i ].inUse){
            hdr->FetchFrom(table[i].sector);

            printf("%-20s %-10s %-10d %-10d %-10d %-10d\n",
                    table[i].name, FileTypeStr[hdr->fileType], hdr->FileLength(), hdr->timeCreated, hdr->timeAccessed, hdr->timeModified);
        }
    }
    delete hdr;
}
```

我们对List()函数进行修改，使调用List()时能够显示文件的类型、创建时间、上次访问时间、上次修改时间、路径。

###### 测试

```cpp
extern void PathTest(void);
#ifdef FILESYS
else if (!strcmp(*argv, "--pathTest")) {	// pathTest
            PathTest();
}
#endif
```

我们修改main.cc中的参数解析部分，加上"--pathTest"就能调用PathTest。

```cpp
void PathTest(){
    // mkdir dirA && ls
    ASSERT(fileSystem->Create("dirA", -1, dirFile));
    fileSystem->List();
    // cd dirA && ls
    ASSERT(fileSystem->ChangeDir("dirA"));
    fileSystem->List();
    // cd Ha
    ASSERT(!fileSystem->ChangeDir("Ha"));
    // cp nachos/nachos-3.4/code/filesys/test/trump trump
    Copy("nachos/nachos-3.4/code/filesys/test/trump", "trump");
    // mkdir dirB && ls
    ASSERT(fileSystem->Create("dirB", -1, dirFile));
    fileSystem->List();
    // cd dirB
    ASSERT(fileSystem->ChangeDir("dirB"));
    // cp nachos/nachos-3.4/code/filesys/test/small small && ls
    Copy("nachos/nachos-3.4/code/filesys/test/small", "small");
    fileSystem->List();
    // cat small
    Print("small");
    // cd ..
    ASSERT(fileSystem->ChangeDir(".."));
    // cd ..
    ASSERT(fileSystem->ChangeDir(".."));
    // rm -r dirA && ls
    fileSystem->Remove("dirA");
    fileSystem->List();
}
```

PathTest函数等效执行以下Unix命令：

```shell
mkdir dirA && ls
cd dirA && ls
cd Ha
cp nachos/nachos-3.4/code/filesys/test/trump trump
mkdir dirB && ls
cp nachos/nachos-3.4/code/filesys/test/small small && ls
cat small
cd ..
cd ..
rm -r dirA && ls
```

以下是测试结果：

```
[Create] Creating file dirA (Dir), size 1320, time 320520
[Directory] Adding .. (1)
[List] /:
NAME                 TYPE       SIZE       CREATED    ACCESSED   MODDED    
dirA                 Dir        1320       320520     749020     749020    
[ChangeDir] cd to dirA
[FindPath] i=0 name=dirA
[List] /dirA/:
NAME                 TYPE       SIZE       CREATED    ACCESSED   MODDED    
..                   Dir        1320       10         822850     503020    
[ChangeDir] cd to Ha
[changeDir] Ha not found in pwd
[Create] Creating file trump (File), size 8034, time 906520
[Create] Creating file dirB (Dir), size 1320, time 42336520
[Directory] Adding .. (14)
[List] /dirA/:
NAME                 TYPE       SIZE       CREATED    ACCESSED   MODDED    
..                   Dir        1320       10         42870850   503020    
trump                File       8034       906520     42302020   42302020  
dirB                 Dir        1320       42336520   42820020   42820020  
[ChangeDir] cd to dirB
[Create] Creating file small (File), size 38, time 42992520
[List] /dirA/dirB/:
NAME                 TYPE       SIZE       CREATED    ACCESSED   MODDED    
..                   Dir        1320       320520     43549350   42557020  
small                File       38         42992520   43461020   43461020  
This is the spring of our discontent.
[ChangeDir] cd to ..
[ChangeDir] cd to ..
[RemoveAll] Removed trump
[RemoveAll] Removed small
[RemoveAll] Removed dirB
[Remove] Removed dirA
[List] /:
NAME                 TYPE       SIZE       CREATED    ACCESSED   MODDED   
```

可以看到，文件夹dirA中的内容能够被递归删除，行为符合我们的预期。

#### Exercise 5 动态调整文件长度

> 对文件的创建操作和写入操作进行适当修改，以使其符合实习要求。 

###### 实现

为了实现的简要起见，在测试时我们可以复用fstest.cc中定义的FileRead()和FileWrite()函数。FileWrite函数会首先创建一个大小为0的文件，随后动态增长。首先我们需要能实现写入时动态增大文件尺寸的功能：

```cpp
// [lab5] Allocate new Sectors, update Header
// 0 No Need -1 Error >0 Success
// Note: write Back by caller
int FileHeader::ScaleUp(BitMap *freeMap, int newSize){
    int currSectors = numSectors;
    int newSectors = divRoundUp(newSize, SectorSize) - numSectors;
    if (newSectors <= 0) {
        DEBUG('D', "[ScaleUp] No need for extension (%d/%d)\n", newSectors+currSectors, currSectors);
        numBytes = newSize;  // still updating size
        return 0;
    }
    DEBUG('D', "[ScaleUp] Extending (%d/%d)\n", newSectors+currSectors, currSectors);
    // direct indexing
    int remainedSectors = currSectors + newSectors;
    for (int i = 0; i < min(currSectors + newSectors, NumDirect - 2); i++){
        if (remainedSectors <= newSectors) {
            dataSectors[i] = freeMap->Find();
            DEBUG('f', "[ScaleUp] Allocating sector %d, i=%d\n", dataSectors[i], i);
            fileSystem->Print(); // [debug]
        }
        remainedSectors --;
    }

    // [NOTE!] numSectors MUST BE RIGHT, else catastrophic mem errors can happen
    // dataSectors[NumDirect - 2]: indirect indexing
    if(remainedSectors > 0){
        // alloc sector from disk
        IndirectTable * idt = new IndirectTable;
        int idtSector;

        if (currSectors <= NumDirect - 2) {  // 1st idt doesn't exist
            if ((idtSector = freeMap->Find()) == -1) return -1;  // no more space
            dataSectors[ NumDirect - 2 ] = idtSector;
            DEBUG('D', "[ScaleUp] IDT1: Creating IDT sector=%d\n", idtSector);
        }else{
            idtSector = dataSectors[ NumDirect - 2 ];
            FetchFrom(idtSector, (char*) idt);
            DEBUG('D', "[ScaleUp] IDT1: Using IDT sector=%d\n", idtSector);
        }

        // set val of idt (stop alloc if remainedSectors = 0)
        for(int j = 0; j < NumIndirect && remainedSectors > 0; j++){
            if (remainedSectors <= newSectors) {
                idt->dataSectors[j] = freeMap->Find();
                DEBUG('f', "[ScaleUp] IDT1: Allocating sector %d, i=%d\n", idt->dataSectors[j], j);
            }
            remainedSectors --;
        }
        // write changes to disk
        WriteBack(idtSector, (char*) idt);
    }

    // dataSectors[NumDirect - 1]: indirect indexing
    if(remainedSectors > 0){
        // alloc sector from disk
        IndirectTable * idt = new IndirectTable;
        int idtSector;

        if (currSectors <= NumDirect - 2 + NumIndirect) {  // 2nd idt doesn't exist
            if ((idtSector = freeMap->Find()) == -1) return -1;  // no more space
            dataSectors[ NumDirect - 1 ] = idtSector;
            DEBUG('D', "[ScaleUp] IDT2: Creating IDT sector=%d\n", idtSector);
        }else{
            idtSector = dataSectors[ NumDirect - 1 ];
            FetchFrom(idtSector, (char*) idt);
            DEBUG('D', "[ScaleUp] IDT2: Using IDT sector=%d\n", idtSector);
        }

        // set val of idt (stop alloc if remainedSectors = 0)
        for(int j = 0; j < NumIndirect && remainedSectors > 0; j++){
            if (remainedSectors <= newSectors) {
                idt->dataSectors[j] = freeMap->Find();
                DEBUG('f', "[ScaleUp] IDT2: Allocating sector %d, i=%d\n", idt->dataSectors[j], j);
            }
            remainedSectors --;
        }
        // write changes to disk
        WriteBack(idtSector, (char*) idt);
    }

    // update header
    numSectors = currSectors + newSectors;
    numBytes = newSize;
    return newSectors;
}
```

FileHeader::ScaleUp函数：

1. 计算需要增长的扇区数newSectors。如果不需要分配新扇区，直接返回。
2. 尝试用直接索引分配新扇区。
3. 新扇区没有分配完，需要使用第一张间接索引表。如果索引表不存在，创建索引表并分配空间；如果已经存在，从硬盘中加载索引表。随后使用第一张间接索引表分配新扇区。
4. 新扇区没有分配完，需要使用第二张间接索引表。如果索引表不存在，创建索引表并分配空间；如果已经存在，从硬盘中加载索引表。随后使用第二张间接索引表分配新扇区。
5. 更新FileHeader中的文件大小和扇区数。

```cpp
int OpenFile::WriteAt(char *from, int numBytes, int position) {
	if ((numBytes <= 0))
        return 0;                // check request
  if ((position + numBytes) > fileLength){  // [lab5] Modified to support file extension
    OpenFile *freeMapFile = new OpenFile(FreeMapSector);
    BitMap *freeMap = new BitMap(NumSectors);
    freeMap->FetchFrom(freeMapFile);
    // alloc new sectors
    hdr->ScaleUp(freeMap, (position + numBytes));
    hdr->WriteBack(hdrSector);
    freeMap->WriteBack(freeMapFile);
    delete freeMap;
    delete freeMapFile;
  }
}
```

我们还需要修改WriteAt函数在`(position + numBytes) > fileLength`时的行为，如果文件尺寸太小，那就调用ScaleUp扩展文件空间，并将修改后的FileHeader写回。

###### 测试

```
#define FileSize    ((int)(ContentSize * 1000))
```

PerformanceTest默认测试的文件大小约为50KB，超出了文件大小11KB的限制；这里将其修改为10KB。

```
docker run -it nachos gdb --args nachos/nachos-3.4/code/filesys/nachos -f -d D -t
```

我们直接调用PerformanceTest进行测试，输出如下：

```cpp
[ScaleUp] Extending (25/24)
[ScaleUp] IDT1: Creating IDT sector=39
...
[ScaleUp] Extending (57/56)
[ScaleUp] IDT1: Using IDT sector=39
[ScaleUp] IDT2: Creating IDT sector=72
...
[ScaleUp] Extending (79/78)
[ScaleUp] IDT1: Using IDT sector=39
[ScaleUp] IDT2: Using IDT sector=72
[ScaleUp] No need for extension (79/79)
Sequential read of 10000 byte file, in 10 byte chunks
```

可以看到输出中出现了扩展文件大小和新建间接索引表的过程。

```
[Sector   0] 12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678
[Sector   1] 90123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456
[Sector   2] 78901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234
...
```

我们尝试输出文件内容，结果符合我们的预期。

#### Exercise 6 源代码阅读

##### 6.1 阅读Nachos源代码中与异步磁盘相关的代码，理解Nachos系统中异步访问模拟磁盘的工作原理。(filesys/synchdisk.h和filesys/synchdisk.cc)

由于磁盘I/O是异步事件，SynchDisk提供了将异步事件同步化的接口。

```cpp
class SynchDisk {
public:
    SynchDisk(char *name);            // Initialize a synchronous disk,
    // by initializing the raw Disk.
    ~SynchDisk();            // De-allocate the synch disk data

    void ReadSector(int sectorNumber, char *data);

    // Read/write a disk sector, returning
    // only once the data is actually read
    // or written.  These call
    // Disk::ReadRequest/WriteRequest and
    // then wait until the request is done.
    void WriteSector(int sectorNumber, char *data);

    void RequestDone();            // Called by the disk device interrupt
    // handler, to signal that the
    // current disk operation is complete.

private:
    Disk *disk;                // Raw disk device
    Semaphore *semaphore;        // To synchronize requesting thread
    // with the interrupt handler
    Lock *lock;                // Only one read/write request
    // can be sent to the disk at a time
};
```

我们可以看到，SynchDisk封装了Disk，并增加了信号量和锁来保证I/O事件的同步。

```cpp
void SynchDisk::ReadSector(int sectorNumber, char *data) {
    lock->Acquire();            // only one disk I/O at a time
    disk->ReadRequest(sectorNumber, data);
    semaphore->P();            // wait for interrupt
    lock->Release();
}
void SynchDisk::WriteSector(int sectorNumber, char *data) {
    lock->Acquire();            // only one disk I/O at a time
    disk->WriteRequest(sectorNumber, data);
    semaphore->P();            // wait for interrupt
    lock->Release();
}
```

需要注意到，SynchDisk类中定义了互斥锁lock，在读写操作时都有加锁解锁的过程，因此同时只能有一个线程处在等待I/O状态。

`SynchDisk::ReadSector`与`SynchDisk::WriteSector`通过`semaphore->P()`语句，让线程在等待I/O时先进入阻塞状态。

```cpp
void SynchDisk::RequestDone() {
    semaphore->V();
}
```

当I/O请求完成后，`semaphore->V()`会使线程从I/O操作中返回。

##### 6.2 利用异步访问模拟磁盘的工作原理，在Class Console的基础上，实现Class SynchConsole。

###### 实现

```cpp
// [lab5] wraps console with lock
class SynchConsole {
public:
    SynchConsole(char *readFile, char *writeFile);
    ~SynchConsole();

    void PutChar(char ch);
    char GetChar();

private:
    // internal routines used by Console, do not call these
    static void readAvail(int ptr);
    static void writeDone(int ptr);

    Console *console;
    // take this as a bounded-buffer problem with buffer size of 1
    Semaphore *wait_element;        // To synchronize requesting thread
    Semaphore *wait_slot;
    Lock *lock;                // Only one read/write request
};
```

参照SynchDisk的实现，我们的SynchConsole封装了 Console *console， 并通过锁和信号量实现同步。

```cpp
// [lab5] SynchConsole wraps console
// Take this as an bounded-buffer problem
// buffer size = 1 (Console buffer is only 1 char), slot=1, element=0
SynchConsole::SynchConsole(char *readFile, char *writeFile) {
    console = new Console(readFile, writeFile, readAvail, writeDone, (int)this);
    wait_element = new Semaphore("sc_wait_element", 0); // 0 element
    wait_slot = new Semaphore("sc_wait_slot", 1); // 1 slot
    lock = new Lock("sc_lock");
}
SynchConsole::~SynchConsole() {
    delete console;
    delete wait_element;        // To synchronize requesting thread
    delete wait_slot;
    delete lock;
}
```

由于观察到Console内部的缓冲区仅能存放一个char变量，因此我们将SynchConsole的读写操作理解为一个缓冲区大小为1的生产者消费者问题。

```cpp
// consumer start
char SynchConsole::GetChar() {
    DEBUG('C', "[GetChar] waiting...\n");
    lock->Acquire();
    wait_element->P();
    char res = console->GetChar();
    DEBUG('C', "[GetChar] %c\n", res);
    lock->Release();
    return res;
}
// consumer finish (staticmethod)
void SynchConsole::writeDone(int ptr) {
    SynchConsole *sc = (SynchConsole*) ptr;
    sc->wait_slot->V();
}

```

消费者会通过`wait_element->P()`等待缓冲区中出现元素，并在消费完成后通过`sc->wait_slot->V()`告知生产者缓冲区中出现了空位。

```cpp
// producer start
void SynchConsole::PutChar(char ch) {
    DEBUG('C', "[PutChar] waiting...\n");
    lock->Acquire();
    wait_slot->P();
    DEBUG('C', "[PutChar] %c\n", ch);
    console->PutChar(ch);
    lock->Release();
}
// producer finish (staticmethod)
void SynchConsole::readAvail(int ptr){
    SynchConsole *sc = (SynchConsole*) ptr;
    sc->wait_element->V();
}
```

生产者会通过`wait_slot->P()`等待缓冲区中出现空位，并在生产完成后通过`sc->wait_element->V()`告知消费者缓冲区中出现了元素。

###### 测试

```cpp
// [lab5] SynchConsole Test
void SynchConsoleTest (char* in, char* out){
    SynchConsole* sc = new SynchConsole(in, out);
    char ch = sc->GetChar();
    while(ch != 'q'){
        sc->PutChar(ch);
        ch = sc->GetChar();
    }
}
```

SynchConsoleTest函数从控制台读入字符，并输出到控制台。若输入的是q，则退出。

```cpp
if (!strcmp(*argv, "-sc")) {      // test the SynchConsole
  if (argc == 1)
    SynchConsoleTest(NULL, NULL);
  else {
    ASSERT(argc > 2);
    SynchConsoleTest(*(argv + 1), *(argv + 2));
    argCount = 3;
  }
  interrupt->Halt();		// once we start the console, then
  // Nachos will loop forever waiting
  // for console input
}
```

我们参照测试控制台的"-c"参数在main.cc中增加了"-sc"参数。

```
[GetChar] waiting...
Yes!
[GetChar] Y
[PutChar] waiting...
[PutChar] Y
Y[GetChar] waiting...
[GetChar] e
[PutChar] waiting...
[PutChar] e
e[GetChar] waiting...
[GetChar] s
[PutChar] waiting...
[PutChar] s
s[GetChar] waiting...
[GetChar] !
[PutChar] waiting...
[PutChar] !
![GetChar] waiting...
[GetChar] 

[PutChar] waiting...
[PutChar] 


[GetChar] waiting...
q
[GetChar] q
Machine halting!
```

可以看到测试结果符合我们的预期。

#### Exercise 7 实现文件系统的同步互斥访问机制，达到如下效果：

> a)   一个文件可以同时被多个线程访问。且每个线程独自打开文件，独自拥有一个当前文件访问位置，彼此间不会互相干扰。

> b)   所有对文件系统的操作必须是原子操作和序列化的。例如，当一个线程正在修改一个文件，而另一个线程正在读取该文件的内容时，读线程要么读出修改过的文件，要么读出原来的文件，不存在不可预计的中间状态。

> c)   当某一线程欲删除一个文件，而另外一些线程正在访问该文件时，需保证所有线程关闭了这个文件，该文件才被删除。也就是说，只要还有一个线程打开了这个文件，该文件就不能真正地被删除。

###### Part a, b 实现

考虑到原本每个线程都有独立的OpenFile对象，因此其访问文件的位置也是独立的，不会互相干扰，Part a实质上已经完成。我们的任务就是在不修改原先OpenFile功能的情况下实现读写的互斥。

显然，为了实现同一个文件读写的互斥，我们需要一个数据结构记录当前打开了什么文件（类似于打开文件表），这个数据结构中还需要为每个文件维护一个读写锁。

```cpp
  #ifdef USE_HDRTABLE
    extern HeaderTable *hdrTable;
  #endif
```

```cpp
#ifdef USE_HDRTABLE
    hdrTable = new HeaderTable(20);
#endif
```

在这里我们增加了HeaderTable数据结构，并修改`system.h`和`system.cc`，在NachOS启动时创建一个全局对象。

```cpp
struct HeaderTableEntry {
    // [lab5] manually alloc all locks & semaphores
    HeaderTableEntry();
    ~HeaderTableEntry();

    int hdrSector;
    bool inUse;

    // [lab5] reader/writer
    int readerCount;
    Lock *readerLock;
    Lock *fileLock;
};

HeaderTableEntry::HeaderTableEntry() {
    hdrSector = -1;
    inUse = false;

    readerCount = 0;
    readerLock = new Lock("rc_lock");
    fileLock = new Lock("file_lock");
}

HeaderTableEntry::~HeaderTableEntry() {
    delete readerLock;
    delete fileLock;
}
```

HeaderTableEntry是HeaderTable中的一项，对应当前打开的一个文件。由于NachOS文件系统中一个文件FileHeader所在的扇区号是唯一的（在删除文件前必定关闭文件），因此我们采用hdrSector作为文件的唯一标识符， inUse记录当前HeaderTableEntry是否被使用。

为了实现读写的互斥，我们考虑将其作为第一类读者写者问题处理。

readerCount记录读者的数量，readerLock保护readerCount， fileLock避免读者写者冲突。

```cpp
class HeaderTable {
public:
    HeaderTable(int size);
    ~HeaderTable();

    // called by reader, parameter is hdrSector
    void beforeRead(int sector);
    void afterRead(int sector);

    // called by writer, parameter is hdrSector
    void beforeWrite(int sector);
    void afterWrite(int sector);

    int fileOpen(int sector);
    void fileClose(int sector);
    int findIndex(int sector);

private:
    int tableSize;
    HeaderTableEntry *table;
};


// [lab5] alloc headerTable
HeaderTable::HeaderTable(int size) {
    // naive checking of tableSize
    ASSERT(size > 0 && size < NumSectors);
    tableSize = size;
    table = new HeaderTableEntry[tableSize];
}

HeaderTable::~HeaderTable() {
    delete table;
}

// [lab5] find table index by hdrSector
int HeaderTable::findIndex(int sector) {
    for(int i = 0; i < tableSize; i++){
        if(table[i].inUse && table[i].hdrSector == sector) return i;
    }
    return -1;
}
```

HeaderTable中包含若干项HeaderTableEntry，项数在创建HeaderTable时指定。

findIndex从HeaderTable中寻找扇区号匹配的项，若没有找到则返回-1。

```cpp
int HeaderTable::fileOpen(int sector) {
    if (findIndex(sector) != -1) {
        DEBUG('H', "[fileOpen] exist hdrTable[%d]=%d\n", findIndex(sector), sector);
        return -1;  // already exists
    }
    for(int i = 0; i < tableSize; i++){
        if(!table[i].inUse){
            table[i].inUse = true;
            table[i].hdrSector = sector;
            DEBUG('H', "[fileOpen] alloc hdrTable[%d]=%d\n", i, sector);
            return i;
        }
    }
    printf("\033[31m[fileOpen] HeaderTable Full\n\033[0m");
    return -1;
}

OpenFile::OpenFile(int sector) {
    hdr = new FileHeader;
    hdr->FetchFrom(sector);
    seekPosition = 0;

    hdrSector = sector;  // [lab5] mark hdr sector
    hdr->timeAccessed = stats->totalTicks;  // [lab5] set file attributes
    hdr->WriteBack(hdrSector);  // [lab5] write changes back to disk

    // [lab5] alloc hdrTable
#ifdef USE_HDRTABLE
    hdrTable->fileOpen(hdrSector);
#endif
}
```

在打开文件时，我们确保HeaderTable中对应的项存在。若不存在，则创建这一项。

```cpp
void HeaderTable::fileClose(int sector) {}

OpenFile::~OpenFile() {
#ifdef USE_HDRTABLE
    // [lab5] dealloc hdrTable
    hdrTable->fileClose(hdrSector);
#endif
    delete hdr;
}
```

由于现在还没有实现引用计数，因此在关闭文件时我们什么也不做。

```cpp
// [lab5] Take this as a reader/writer problem
// allow multiple readers
void HeaderTable::beforeRead(int sector) {
    int index = findIndex(sector);
    ASSERT(index >= 0);
    table[index].readerLock->Acquire();
    table[index].readerCount++;
    DEBUG('H', "[beforeRead] sector=%d rc=%d\n", sector, table[index].readerCount);
    if(table[index].readerCount == 1){
        // is 1st reader
        table[index].fileLock->Acquire();
    }
    table[index].readerLock->Release();
}

void HeaderTable::afterRead(int sector) {
    int index = findIndex(sector);
    ASSERT(index >= 0);
    table[index].readerLock->Acquire();
    table[index].readerCount--;
    DEBUG('H', "[AfterRead] sector=%d rc=%d\n", sector, table[index].readerCount);
    if(table[index].readerCount == 0){
        // is last reader
        table[index].fileLock->Release();
    }
    table[index].readerLock->Release();
}

int OpenFile::ReadAt(char *into, int numBytes, int position) {
    ...
    // [lab5] before read
    hdrTable->beforeRead(hdrSector);
#endif

    firstSector = divRoundDown(position, SectorSize);
    lastSector = divRoundDown(position + numBytes - 1, SectorSize);
    numSectors = 1 + lastSector - firstSector;
    ...     
    // [lab5] set file attributes
    hdr->timeAccessed = stats->totalTicks;
    hdr->WriteBack(hdrSector);  // [lab5] write changes back to disk
#ifdef USE_HDRTABLE
    // [lab5] after read
    hdrTable->afterRead(hdrSector);
#endif
    return numBytes;
}
```

在第一类读者写者问题中，读者在读前需要先执行：

```coo
P(mutex);
rc ++;
if(rc == 1) P(file)
V(mutex)
```

读后需要执行：

```cpp
P(mutex);
rc --;
if(rc == 0) V(file)
V(mutex)
```

分别对应beforeRead()和afterRead()函数。

```cpp
void HeaderTable::beforeWrite(int sector) {
    int index = findIndex(sector);
    ASSERT(index >= 0);
    DEBUG('H', "[beforeWrite] sector=%d\n", sector);
    table[index].fileLock->Acquire();
}

void HeaderTable::afterWrite(int sector) {
    int index = findIndex(sector);
    ASSERT(index >= 0);
    DEBUG('H', "[afterWrite] sector=%d\n", sector);
    table[index].fileLock->Release();
}

int OpenFile::WriteAt(char *from, int numBytes, int position) {
    ...
//        numBytes = fileLength - position;
    DEBUG('f', "Writing %d bytes at %d, from file of length %d.\n",
          numBytes, position, fileLength);
#ifdef USE_HDRTABLE
    // [lab5] before write
    hdrTable->beforeWrite(hdrSector);
#endif
    // [lab5] set file attributes
    hdr->timeAccessed = hdr->timeModified = stats->totalTicks;
    hdr->WriteBack(hdrSector);  // [lab5] write changes back to disk
#ifdef USE_HDRTABLE
    // [lab5] after write
    hdrTable->afterWrite(hdrSector);
#endif
    return numBytes;
}
```

在第一类读者写者问题中，写者在写前需要先执行：

```coo
P(file)
```

读后需要执行：

```cpp
V(file)
```

分别对应beforeWrite()和afterWrite()函数。

第一类读者写者问题会造成写者饥饿，不过在这里不是我们考虑的重点。

###### Part a, b 测试

```cpp
void readerThread(int which){
    printf("[readerThread] starting %s\n",currentThread->getName());
    FileRead();
}

void writerThread(int which){
    printf("[writerThread] starting %s\n",currentThread->getName());
    FileWrite();
}

void LockTest(){
    if (!fileSystem->Create(FileName, FileSize)) {
        printf("Perf test: can't create %s\n", FileName);
        return;
    }
    Thread *w1 = new Thread("writer1");
    Thread *r1 = new Thread("reader1");
    w1->Fork(writerThread, (void*)2);
    r1->Fork(readerThread, (void*)1);
    FileRead();
}
```

在LockTest()函数中，readerThread调用FileRead()读取文件，writerThread调用FileWrite()写入文件。

```
Creating file TestFile, size 10000
Reading 1320 bytes at 0, from file of length 1320.
Reading 128 bytes at 0, from file of length 128.
[WriteBack] Writing idt to sector 39 at dest 0x8cb7460
[WriteBack] Writing idt to sector 72 at dest 0x8cb74e8
Writing 1320 bytes at 0, from file of length 1320.
Reading 40 bytes at 1280, from file of length 1320.
Writing 128 bytes at 0, from file of length 128.
Sequential read of 10000 byte file, in 10 byte chunks
Opening file TestFile
Reading 1320 bytes at 0, from file of length 1320.
[writerThread] starting writer1
Sequential write of 10000 byte file, in 10 byte chunks
Opening file TestFile
Reading 1320 bytes at 0, from file of length 1320.
[readerThread] starting reader1
Sequential read of 10000 byte file, in 10 byte chunks
Opening file TestFile
Reading 1320 bytes at 0, from file of length 1320.
[fileOpen] alloc hdrTable[0]=14
Reading 10 bytes at 0, from file of length 10000.
[beforeRead] sector=14 rc=1
[AfterRead] sector=14 rc=0
(main) Perf test: read 10 bytes: mismatch from TestFile
[fileClose] close hdrTable[0]=14
[fileOpen] exist hdrTable[0]=14
Reading 10 bytes at 0, from file of length 10000.
[beforeRead] sector=14 rc=1
[AfterRead] sector=14 rc=0
(reader1) Perf test: read 10 bytes: mismatch from TestFile
[fileClose] close hdrTable[0]=14
[fileOpen] exist hdrTable[0]=14
Writing 10 bytes at 0, from file of length 10000.
[beforeWrite] sector=14
Reading 128 bytes at 0, from file of length 10000.
[beforeRead] sector=14 rc=1
...
```

测试结果中beforeRead， afterRead， beforeWrite， afterWrite的行为符合我们的预期，读写出现了互斥。

###### Part c 实现

```cpp
struct HeaderTableEntry {
    ...
    // [lab5] safe delete
    int refCount;
    Lock *refLock;
    Lock *deletableLock;
};
```

实现安全删除的前提就是实现引用计数。在这里refLock保护refCount， deletableLock用户让删除线程等待。

```cpp
int HeaderTable::fileOpen(int sector) {
    if (sector <= 1) return -1;
    int index = findIndex(sector);
    if (index != -1) {
        DEBUG('H', "[fileOpen] exist hdrTable[%d]=%d\n", index, sector);

        // [lab5] change refcount when open existing file
        table[index].refLock->Acquire();
        table[index].refCount++;
        table[index].refLock->Release();

        return -1;  // already exists
    }
    for(int i = 0; i < tableSize; i++){
        if(!table[i].inUse){
            table[i].inUse = true;
            table[i].hdrSector = sector;
            DEBUG('H', "[fileOpen] alloc hdrTable[%d]=%d\n", i, sector);

            // [lab5] acquire deletable when create file
            table[i].refLock->Acquire();
            table[i].refCount = 1;
            table[i].deletableLock->Acquire();
            table[i].refLock->Release();

            return i;
        }
    }
    printf("\033[31m[fileOpen] HeaderTable Full\n\033[0m");
    return -1;
}
```

当打开文件时：若HeaderTable中对应项存在，增加引用计数；若不存在，新建项，并获取 deletableLock。

```cpp
void HeaderTable::fileClose(int sector) {
    if (sector <= 1) return;
    int index = findIndex(sector);
    DEBUG('H', "[fileClose] close hdrTable[%d]=%d\n", index, sector);
    ASSERT(0 <= index && index <= tableSize);

    // [lab5] change refcount when closing file
    table[index].refLock->Acquire();
    table[index].refCount--;
    if(table[index].refCount <= 0){
        table[index].deletableLock->Release();
        table[index].inUse = false;  // remove entry from table
    }
    table[index].refLock->Release();
}
```

当删除文件时：在HeaderTable中寻找对应项，减少引用计数；若引用计数为0，释放deletableLock，并删除项。

```cpp
void HeaderTable::fileRemove(int sector) {
    DEBUG('H', "[fileRemove] Trying to remove sector=%d\n", sector);
    if (sector <= 1) return;
    int index = findIndex(sector);
    // not found in header table, safe
    if(index == -1) return;
    // else wait for other threads to finish
    table[index].deletableLock->Acquire();
    // remove entry from table
    table[index].deletableLock->Release();
    table[index].inUse = false;
    DEBUG('H', "[fileRemove] Removed sector=%d\n", sector);
}
```

我们增加fileRemove()函数，在删除文件前先通过`deletableLock->Acquire()`获取锁，保证没有线程在使用当前文件。

```cpp
bool FileSystem::Remove(char *name) {
    ...
#ifdef USE_HDRTABLE
    // [lab5] safe delete
    hdrTable->fileRemove(sector);
#endif

    fileHdr->Deallocate(freeMap);        // remove data blocks
    freeMap->Clear(sector);            // remove header block
    directory->Remove(name);

```

FileSystem::Remove()需要在删除文件前调用fileRemove()。

###### Part c 测试

```cpp
void deleteThread(int which){
    printf("starting %s\n",currentThread->getName());
    fileSystem->Remove("Shuwarin");
    fileSystem->Print();
}

void closeThread(int ptr){
    printf("starting %s\n",currentThread->getName());
    OpenFile *f1 = (OpenFile *) ptr;
    delete f1;
    fileSystem->Print();
}

void DeleteTest(){
    fileSystem->Create("Shuwarin", 100);
    OpenFile *f1 = fileSystem->Open("Shuwarin");
    Thread *d1 = new Thread("delete1");
    d1->Fork(deleteThread, (void*)0);
    Thread *c1 = new Thread("close1");
    c1->Fork(closeThread, (void*) f1);
    currentThread->Yield();
}
```

我们先打开一个文件，Fork deleteThread尝试删除文件，然后再Fork closeThread关闭文件。

输出如下：

```
[Create] Creating file Shuwarin (File), size 100, time 320520
[fileOpen] alloc hdrTable[0]=14
starting delete1
starting close1
[fileClose] close hdrTable[0]=14
Bit map file header:
[fileRemove] Trying to remove sector=14
[Remove] Removed Shuwarin
```

可以看见文件当我们关闭之后才被删除，这一结果符合预期。

#### Challenge 2 实现pipe机制

> 重定向openfile的输入输出方式，使得前一进程从控制台读入数据并输出至管道，后一进程从管道读入数据并输出至控制台。

###### 实现

为了简单起见，我们就不将管道中的内容放到内核缓冲区中，而是指定2号扇区固定存放管道文件的内容。

```cpp
FileSystem::FileSystem(bool format) {
  			...
				// [lab5] pipe
        FileHeader *pipeHdr = new FileHeader;
        freeMap->Mark(PipeSector);
        pipeHdr->WriteBack(PipeSector);
}
```

当我们创建FileSystem时，需要和BitMap类似地创建管道文件。

```cpp
// [lab5] pipe
int FileSystem::ReadPipe(char *into, int numBytes) {
    OpenFile* pipeFile = new OpenFile(PipeSector);
    int res = pipeFile->Read(into, numBytes);
    delete pipeFile;
    return res;
}

int FileSystem::WritePipe(char *into, int numBytes) {
    OpenFile* pipeFile = new OpenFile(PipeSector);
    int res = pipeFile->Write(into, numBytes);
    delete pipeFile;
    return res;
}
```

ReadPipe和WritePipe是对Read和Write方法的简单包装。

```cpp
void readPipe(int numBytes){
    printf("starting %s\n",currentThread->getName());
    char* buffer = new char[numBytes];
    memset(buffer, 0 ,sizeof(char[numBytes]));
    fileSystem->ReadPipe(buffer, numBytes);
    printf("[readPipe] %s", buffer);
    delete[] buffer;
}

void writePipe(int dummy){
    printf("starting %s\n",currentThread->getName());
    char into[30] = "ShuwarinDreaming!\n";
    fileSystem->WritePipe(into, strlen(into) + 1);
}

void PipeTest(){
    Thread *w1 = new Thread("readPipe1");
    w1->Fork(writePipe, (void*)0);
    Thread *r1 = new Thread("close1");
    r1->Fork(readPipe, (void*) 19);
    currentThread->Yield();
}
```

PipeTest函数先通过writePipe线程将"ShuwarinDreaming!\n"写入管道，之后再从管道中读出。

###### 测试

```
starting readPipe1
starting close1
[ScaleUp] Extending (1/0)
[readPipe] ShuwarinDreaming!
```

可以发现测试结果符合我们的预期。

#### 遇到的困难以及收获

1. gdb的使用

   在难以使用其他调试工具的docker环境下，gdb真是帮了大忙。以下是几个常用命令：

   `p machine->printMem()` 显示结果（可以是当前上下文的变量或函数）

   `info stack` 显示函数调用栈

   `watch currentThread->space->vm` 当变量值变化时触发断点

2. ANSI `\033` 可以设置printf打印字符串的颜色，方便在浩繁的调试输出信息中找到一些关键。例如`\033[31m`可以将字符串设置为红色。


#### 对课程或Lab的意见和建议

1. 希望助教提供Lab的答疑和讨论（主要是NachOS代码相关的）。
2. 希望助教能够更加明确实验要求，希望我们得出什么样的结果。
3. 希望实验报告别卷了。
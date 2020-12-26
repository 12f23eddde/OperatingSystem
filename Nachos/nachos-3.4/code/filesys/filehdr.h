// filehdr.h 
//	Data structures for managing a disk file header.  
//
//	A file header describes where on disk to find the data in a file,
//	along with other information about the file (for instance, its
//	length, owner, etc.)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#ifndef FILEHDR_H
#define FILEHDR_H

#include "disk.h"
#include "bitmap.h"

// [lab5] add 4 ints
#define NumDirect    ((SectorSize - 6 * sizeof(int)) / sizeof(int))
#define NumIndirect  ((SectorSize) / sizeof(int))
#define MaxFileSize    ((NumDirect-2) * SectorSize + 2 * NumIndirect * SectorSize)

// The following class defines the Nachos "file header" (in UNIX terms,  
// the "i-node"), describing where on disk to find all of the data in the file.
// The file header is organized as a simple table of pointers to
// data blocks. 
//
// The file header data structure can be stored in memory or on disk.
// When it is on disk, it is stored in a single sector -- this means
// that we assume the size of this data structure to be the same
// as one disk sector.  Without indirect addressing, this
// limits the maximum file length to just under 4K bytes.
//
// There is no constructor; rather the file header can be initialized
// by allocating blocks for the file (if it is a new file), or by
// reading it from disk.

enum FileType {normalFile, dirFile, bitmapFile};
static const char* FileTypeStr [] = {"File", "Dir", "BitMap"};

// [lab5] Indirect Index Table
// For convenience, the size of 1 table is the same as 1 sector
// | 0 | 1 | ... | n-2 | n-1 |
//   .   .          ,     ,
//                 ...   ...
// IndirectTable does NOT do mem/disk management
// FileHeader should alloc/dealloc when Alloc, Dealloc
struct IndirectTable {
    int dataSectors[NumIndirect];
    void printSectors(int cnt=NumIndirect);
};

class FileHeader {
public:
    bool Allocate(BitMap *bitMap, int fileSize);// Initialize a file header, 
    //  including allocating space
    //  on disk for the file data
    void Deallocate(BitMap *bitMap);        // De-allocate this file's
    //  data blocks

    void FetchFrom(int sectorNumber, char* dest=NULL);    // Initialize file header from disk
    void WriteBack(int sectorNumber, char* dest=NULL);    // Write modifications to file header
    //  back to disk

    int ByteToSector(int offset);    // Convert a byte offset into the file
    // to the disk sector containing
    // the byte

    int FileLength();            // Return the length of the file
    // in bytes

    void Print();            // Print the contents of the file.

    // [lab5] Made it public for convenience
    int ScaleUp(BitMap *freeMap, int newSize);

    FileType fileType;
    int timeCreated = -1;
    int timeModified = -1;
    int timeAccessed = -1;

    int numBytes;            // Number of bytes in the file
    int numSectors;            // Number of data sectors in the file

    // [lab5] We Use [NumDirect -1, NumDirect -2] with 2-layer index
    // We don't put idt here, since all contents of filehdr would be write back to disk
    int dataSectors[NumDirect];  // Disk sector numbers for each data block in the file
};

#endif // FILEHDR_H

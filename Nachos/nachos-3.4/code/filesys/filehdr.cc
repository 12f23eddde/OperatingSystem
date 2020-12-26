// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "filehdr.h"

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(BitMap *freeMap, int fileSize) {
    numBytes = fileSize;
    numSectors = divRoundUp(fileSize, SectorSize);
    if (freeMap->NumClear() < numSectors + 2)
        return FALSE;        // not enough space

//    for (int i = 0; i < numSectors; i++)
//        dataSectors[ i ] = freeMap->Find();

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

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void
FileHeader::Deallocate(BitMap *freeMap) {

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

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk.
//  [lab5] or Anything else with the size of 1 sector
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector, char* dest) {
    if (!dest){
        synchDisk->ReadSector(sector, (char *) this);
    }else{
        DEBUG('f', "[FetchFrom] Reading idt from sector %d at dest %p\n", sector, dest);
        synchDisk->ReadSector(sector, dest);
    }

}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//  [lab5] or Anything else with the size of 1 sector
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector, char* dest) {
    if(!dest) {
        synchDisk->WriteSector(sector, (char *) this);
    }else{
        DEBUG('f', "[WriteBack] Writing idt to sector %d at dest %p\n", sector, dest);
        synchDisk->WriteSector(sector, dest);
    }
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

// [lab5] Modified to support indirect indexing
int
FileHeader::ByteToSector(int offset) {
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

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength() {
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void
FileHeader::Print() {
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

void IndirectTable::printSectors(int cnt) {
    printf("[");
    for(int i = 0; i < cnt; i++){
        printf("%d,", dataSectors[i]);
    }
    printf("]\n");
}

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
//            fileSystem->Print(); // [debug]
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
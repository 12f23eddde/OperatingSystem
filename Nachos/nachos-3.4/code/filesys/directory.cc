// directory.cc 
//	Routines to manage a directory of file names.
//
//	The directory is a table of fixed length entries; each
//	entry represents a single file, and contains the file name,
//	and the location of the file header on disk.  The fixed size
//	of each directory entry means that we have the restriction
//	of a fixed maximum size for file names.
//
//	The constructor initializes an empty directory of a certain size;
//	we use ReadFrom/WriteBack to fetch the contents of the directory
//	from disk, and to write back any modifications back to disk.
//
//	Also, this implementation has the restriction that the size
//	of the directory cannot expand.  In other words, once all the
//	entries in the directory are used, no more files can be created.
//	Fixing this is one of the parts to the assignment.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "utility.h"
#include "filehdr.h"
#include "directory.h"
#include "filesys.h"

//----------------------------------------------------------------------
// Directory::Directory
// 	Initialize a directory; initially, the directory is completely
//	empty.  If the disk is being formatted, an empty directory
//	is all we need, but otherwise, we need to call FetchFrom in order
//	to initialize it from disk.
//
//	"size" is the number of entries in the directory
//----------------------------------------------------------------------

// [lab5] dirSector for parent dir
Directory::Directory(int size, int dirSector) {
    table = new DirectoryEntry[size];
    tableSize = size;
    for (int i = 0; i < tableSize; i++)
        table[ i ].inUse = FALSE;

    // [lab5] 增加..项
    if(dirSector >= 0){
        DEBUG('D', "[Directory] Adding .. (%d)\n", dirSector);
        Add("..", dirSector);
    }

}

//----------------------------------------------------------------------
// Directory::~Directory
// 	De-allocate directory data structure.
//----------------------------------------------------------------------

Directory::~Directory() {
    delete[] table;
}

//----------------------------------------------------------------------
// Directory::FetchFrom
// 	Read the contents of the directory from disk.
//
//	"file" -- file containing the directory contents
//----------------------------------------------------------------------

void
Directory::FetchFrom(OpenFile *file) {
    int sizeRead = file->ReadAt((char *) table, tableSize * sizeof(DirectoryEntry), 0);
    if(sizeRead != (sizeof(DirectoryEntry) * tableSize)){
        printf("\033[31m[FetchFrom] Incorrect read size (%d/%d)\n\033[0m", sizeRead, tableSize * sizeof(DirectoryEntry));
        ASSERT(FALSE);
    }
    // [lab5] set header Sector
    this->hdrSector = file->hdrSector;
}

//----------------------------------------------------------------------
// Directory::WriteBack
// 	Write any modifications to the directory back to disk
//
//	"file" -- file to contain the new directory contents
//----------------------------------------------------------------------

void
Directory::WriteBack(OpenFile *file) {
    int sizeWritten = file->WriteAt((char *) table, tableSize * sizeof(DirectoryEntry), 0);
    if(sizeWritten != (sizeof(DirectoryEntry) * tableSize)){
        printf("\033[31m[WriteBack] Incorrect written size (%d/%d)\n\033[0m", sizeWritten, tableSize * sizeof(DirectoryEntry));
        ASSERT(FALSE);
    }
}

//----------------------------------------------------------------------
// Directory::FindIndex
// 	Look up file name in directory, and return its location in the table of
//	directory entries.  Return -1 if the name isn't in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::FindIndex(char *name) {
    for (int i = 0; i < tableSize; i++)
        if (table[ i ].inUse && !strncmp(table[ i ].name, name, FileNameMaxLen))
            return i;
    return -1;        // name not in directory
}

//----------------------------------------------------------------------
// Directory::Find
// 	Look up file name in directory, and return the disk sector number
//	where the file's header is stored. Return -1 if the name isn't 
//	in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::Find(char *name) {
    int i = FindIndex(name);

    if (i != -1)
        return table[ i ].sector;
    return -1;
}

//----------------------------------------------------------------------
// Directory::Add
// 	Add a file into the directory.  Return TRUE if successful;
//	return FALSE if the file name is already in the directory, or if
//	the directory is completely full, and has no more space for
//	additional file names.
//
//	"name" -- the name of the file being added
//	"newSector" -- the disk sector containing the added file's header
//----------------------------------------------------------------------

bool
Directory::Add(char *name, int newSector) {
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

//----------------------------------------------------------------------
// Directory::Remove
// 	Remove a file name from the directory.  Return TRUE if successful;
//	return FALSE if the file isn't in the directory. 
//
//	"name" -- the file name to be removed
//----------------------------------------------------------------------

bool
Directory::Remove(char *name) {
    int i = FindIndex(name);

    if (i == -1)
        return FALSE;        // name not in directory
    table[ i ].inUse = FALSE;
    return TRUE;
}

//----------------------------------------------------------------------
// Directory::List
// 	List all the file names in the directory. 
//----------------------------------------------------------------------

// [lab5] Modified
void
Directory::List() {
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

//----------------------------------------------------------------------
// Directory::Print
// 	List all the file names in the directory, their FileHeader locations,
//	and the contents of each file.  For debugging.
//----------------------------------------------------------------------

void
Directory::Print() {
    FileHeader *hdr = new FileHeader;

    printf("Directory contents:\n");
    for (int i = 0; i < tableSize; i++)
        if (table[ i ].inUse) {
            printf("Name: %s, Sector: %d\n", table[ i ].name, table[ i ].sector);
            hdr->FetchFrom(table[ i ].sector);
            hdr->Print();
        }
    printf("\n");
    delete hdr;
}

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
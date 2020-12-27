// fstest.cc 
//	Simple test routines for the file system.  
//
//	We implement:
//	   Copy -- copy a file from UNIX to Nachos
//	   Print -- cat the contents of a Nachos file 
//	   Perftest -- a stress test for the Nachos file system
//		read and write a really large file in tiny chunks
//		(won't work on baseline system!)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "utility.h"
#include "filesys.h"
#include "system.h"
#include "thread.h"
#include "disk.h"
#include "stats.h"

#define TransferSize    10    // make it small, just to be difficult

//----------------------------------------------------------------------
// Copy
// 	Copy the contents of the UNIX file "from" to the Nachos file "to"
//----------------------------------------------------------------------

void
Copy(char *from, char *to) {
    FILE *fp;
    OpenFile *openFile;
    int amountRead, fileLength;
    char *buffer;

// Open UNIX file
    if ((fp = fopen(from, "r")) == NULL) {
        printf("Copy: couldn't open input file %s\n", from);
        return;
    }

// Figure out length of UNIX file
    fseek(fp, 0, 2);
    fileLength = ftell(fp);
    fseek(fp, 0, 0);

// Create a Nachos file of the same length
    DEBUG('f', "Copying file %s, size %d, to file %s\n", from, fileLength, to);
    if (!fileSystem->Create(to, fileLength)) {     // Create Nachos file
        printf("Copy: couldn't create output file %s\n", to);
        fclose(fp);
        return;
    }

    openFile = fileSystem->Open(to);
    ASSERT(openFile != NULL);

// Copy the data in TransferSize chunks
    buffer = new char[TransferSize];
    while ((amountRead = fread(buffer, sizeof(char), TransferSize, fp)) > 0)
        openFile->Write(buffer, amountRead);
    delete[] buffer;

// Close the UNIX and the Nachos files
    delete openFile;
    fclose(fp);
}

//----------------------------------------------------------------------
// Print
// 	Print the contents of the Nachos file "name".
//----------------------------------------------------------------------

void
Print(char *name) {
    OpenFile *openFile;
    int i, amountRead;
    char *buffer;

    if ((openFile = fileSystem->Open(name)) == NULL) {
        printf("Print: unable to open file %s\n", name);
        return;
    }

    buffer = new char[TransferSize];
    while ((amountRead = openFile->Read(buffer, TransferSize)) > 0)
        for (i = 0; i < amountRead; i++)
            printf("%c", buffer[ i ]);
    delete[] buffer;

    // [debug]
//    printf("\033[36m");
//    fileSystem->Print();
//    printf("\033[0m");

    delete openFile;        // close the Nachos file
    return;
}

//----------------------------------------------------------------------
// PerformanceTest
// 	Stress the Nachos file system by creating a large file, writing
//	it out a bit at a time, reading it back a bit at a time, and then
//	deleting the file.
//
//	Implemented as three separate routines:
//	  FileWrite -- write the file
//	  FileRead -- read the file
//	  PerformanceTest -- overall control, and print out performance #'s
//----------------------------------------------------------------------

#define FileName    "TestFile"
#define Contents    "1234567890"
#define ContentSize    strlen(Contents)
#define FileSize    ((int)(ContentSize * 1000))

static void
FileWrite() {
    OpenFile *openFile;
    int i, numBytes;

    printf("Sequential write of %d byte file, in %d byte chunks\n",
           FileSize, ContentSize);
//    fileSystem->Create(FileName, 0);
//    if (!fileSystem->Create(FileName, 0)) {
//        printf("Perf test: can't create %s\n", FileName);
//        return;
//    }
    openFile = fileSystem->Open(FileName);
    if (openFile == NULL) {
        printf("Perf test: unable to open %s\n", FileName);
        return;
    }
    for (i = 0; i < FileSize; i += ContentSize) {
        numBytes = openFile->Write(Contents, ContentSize);
        if (numBytes < 10) {
            printf("Perf test: unable to write %s\n", FileName);
            delete openFile;
            return;
        }
    }
    delete openFile;    // close file
}

static void
FileRead() {
    OpenFile *openFile;
    char *buffer = new char[ContentSize];
    int i, numBytes;

    printf("Sequential read of %d byte file, in %d byte chunks\n",
           FileSize, ContentSize);

    if ((openFile = fileSystem->Open(FileName)) == NULL) {
        printf("Perf test: unable to open file %s\n", FileName);
        delete[] buffer;
        return;
    }
    for (i = 0; i < FileSize; i += ContentSize) {
        numBytes = openFile->Read(buffer, ContentSize);
        if ((numBytes < 10) || strncmp(buffer, Contents, ContentSize)) {
            printf("(%s) Perf test: read %d bytes: mismatch from %s\n", currentThread->getName(), numBytes, FileName);
            delete openFile;
            delete[] buffer;
            return;
        } else printf("(%s) Perf test: read %d bytes from %s\n", currentThread->getName(), numBytes, FileName);
    }
    delete[] buffer;
    delete openFile;    // close file
}

void
PerformanceTest() {
    printf("Starting file system performance test:\n");
    stats->Print();
    FileWrite();
    FileRead();
    if (!fileSystem->Remove(FileName)) {
        printf("Perf test: unable to remove %s\n", FileName);
        return;
    }
    stats->Print();
}

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


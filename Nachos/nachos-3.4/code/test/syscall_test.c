// syscall.c - test your syscalls
// Created by 12f23eddde on 2021/1/1.

#include "syscall.h"
//char filename[10] = "Shuwarin";
//char text[10] = "Dreaming";

int exitCode;
SpaceId sp;
char executable[5] = "exit";

void testExec(){
    sp = Exec(executable);
    exitCode = Join(sp);
    Exit(exitCode);
}

int main(){
//    char buffer[11];
//    int fileno;
//    Create(filename);
//    fileno = Open(filename);
//    Write(text, 9, fileno);
//    Close(fileno);
//    fileno = Open(filename);
//    Read(buffer, 9, fileno);
//    Close(fileno);

    Fork(testExec);
    Yield();
    testExec();
}

//#include "syscall.h"
//
//#define BUFFER_SIZE 11
//
//int main() {
//    char data[9]; // as file name and content
//    char buffer[9];
//    OpenFileId fid_write;
//    OpenFileId fid_read;
//    int numBytes;
//
//    data[0] = 't';
//    data[1] = 'e';
//    data[2] = 's';
//    data[3] = 't';
//    data[4] = '.';
//    data[5] = 't';
//    data[6] = 'x';
//    data[7] = 't';
//    data[8] = '\0';
//
//    Create(data);
//
//    fid_write = Open(data);
//    fid_read = Open(data);
//
//    Write(data, 8, fid_write);
//
//    numBytes = Read(buffer, 8, fid_read);
//
//    Close(fid_write);
//    Close(fid_read);
//
//    Exit(numBytes);
//}
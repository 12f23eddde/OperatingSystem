// console.cc 
//	Routines to simulate a serial port to a console device.
//	A console has input (a keyboard) and output (a display).
//	These are each simulated by operations on UNIX files.
//	The simulated device is asynchronous,
//	so we have to invoke the interrupt handler (after a simulated
//	delay), to signal that a byte has arrived and/or that a written
//	byte has departed.
//
//  DO NOT CHANGE -- part of the machine emulation
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "console.h"
#include "system.h"

// Dummy functions because C++ is weird about pointers to member functions
static void ConsoleReadPoll(int c) 
{ Console *console = (Console *)c; console->CheckCharAvail(); }
static void ConsoleWriteDone(int c)
{ Console *console = (Console *)c; console->WriteDone(); }

//----------------------------------------------------------------------
// Console::Console
// 	Initialize the simulation of a hardware console device.
//
//	"readFile" -- UNIX file simulating the keyboard (NULL -> use stdin)
//	"writeFile" -- UNIX file simulating the display (NULL -> use stdout)
// 	"readAvail" is the interrupt handler called when a character arrives
//		from the keyboard
// 	"writeDone" is the interrupt handler called when a character has
//		been output, so that it is ok to request the next char be
//		output
//----------------------------------------------------------------------

Console::Console(char *readFile, char *writeFile, VoidFunctionPtr readAvail, 
		VoidFunctionPtr writeDone, int callArg)
{
    if (readFile == NULL)
	readFileNo = 0;					// keyboard = stdin
    else
    	readFileNo = OpenForReadWrite(readFile, TRUE);	// should be read-only
    if (writeFile == NULL)
	writeFileNo = 1;				// display = stdout
    else
    	writeFileNo = OpenForWrite(writeFile);

    // set up the stuff to emulate asynchronous interrupts
    writeHandler = writeDone;
    readHandler = readAvail;
    handlerArg = callArg;
    putBusy = FALSE;
    incoming = EOF;

    // start polling for incoming packets
    interrupt->Schedule(ConsoleReadPoll, (int)this, ConsoleTime, ConsoleReadInt);
}

//----------------------------------------------------------------------
// Console::~Console
// 	Clean up console emulation
//----------------------------------------------------------------------

Console::~Console()
{
    if (readFileNo != 0)
	Close(readFileNo);
    if (writeFileNo != 1)
	Close(writeFileNo);
}

//----------------------------------------------------------------------
// Console::CheckCharAvail()
// 	Periodically called to check if a character is available for
//	input from the simulated keyboard (eg, has it been typed?).
//
//	Only read it in if there is buffer space for it (if the previous
//	character has been grabbed out of the buffer by the Nachos kernel).
//	Invoke the "read" interrupt handler, once the character has been 
//	put into the buffer. 
//----------------------------------------------------------------------

void
Console::CheckCharAvail()
{
    char c;

    // schedule the next time to poll for a packet
    interrupt->Schedule(ConsoleReadPoll, (int)this, ConsoleTime, 
			ConsoleReadInt);

    // do nothing if character is already buffered, or none to be read
    if ((incoming != EOF) || !PollFile(readFileNo))
	return;	  

    // otherwise, read character and tell user about it
    Read(readFileNo, &c, sizeof(char));
    incoming = c ;
    stats->numConsoleCharsRead++;
    (*readHandler)(handlerArg);	
}

//----------------------------------------------------------------------
// Console::WriteDone()
// 	Internal routine called when it is time to invoke the interrupt
//	handler to tell the Nachos kernel that the output character has
//	completed.
//----------------------------------------------------------------------

void
Console::WriteDone()
{
    putBusy = FALSE;
    stats->numConsoleCharsWritten++;
    (*writeHandler)(handlerArg);
}

//----------------------------------------------------------------------
// Console::GetChar()
// 	Read a character from the input buffer, if there is any there.
//	Either return the character, or EOF if none buffered.
//----------------------------------------------------------------------

char
Console::GetChar()
{
   char ch = incoming;

   incoming = EOF;
   return ch;
}

//----------------------------------------------------------------------
// Console::PutChar()
// 	Write a character to the simulated display, schedule an interrupt 
//	to occur in the future, and return.
//----------------------------------------------------------------------

void
Console::PutChar(char ch)
{
    ASSERT(putBusy == FALSE);
    WriteFile(writeFileNo, &ch, sizeof(char));
    putBusy = TRUE;
    interrupt->Schedule(ConsoleWriteDone, (int)this, ConsoleTime,
					ConsoleWriteInt);
}

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
/*
 * Trace.cc
 *
 *  Created on: 08.06.2009
 *     Copyright & Author: dbaldin
 */
#include "SCLConfig.hh"

#ifdef HAS_Kernel_TracerCfd

#include "Trace.hh"
#include "kernel/Kernel.hh"
#include "inc/memtools.hh"
#include "comm/Socket.hh"
#include "inet.h"

extern Kernel* theOS;
extern Kernel_ThreadCfdCl* pCurrentRunningThread;
extern Task* pCurrentRunningTask;
extern Board_ClockCfdCl* theClock;

#ifndef DEBUG_TRACE_LOCATION
#define DEBUG_TRACE_LOCATION 0
#endif

#ifndef DEBUG_TRACE_SIZE
#define DEBUG_TRACE_SIZE 0
#endif

/**
 * 40 KB of memory for trace
 */
#define NUM_TRACE_ENTRIES 1024

typedef struct {
    unint4 TraceID;
    unint4 SystemId;
    Trace_Entry buffer[NUM_TRACE_ENTRIES];
}__attribute__((packed, aligned(4))) Trace_Buffer;

static Trace_Buffer trace_buffer1;
static Trace_Buffer trace_buffer2;

static unint2 current_entry;
static unint4 num_entries;
static Trace_Buffer* current_buffer;

typedef struct {
    unint1 taskid;
    unint2 sourceid;
    char name[11];
}__attribute__((packed)) Trace_Source;

typedef struct {
    unint4 PlatformID;                      // ARM, PPC e.t.c
    unint4 SystemID;// to be assigned by the host
    unint4 clockTicksPerSecond;// the number of clock ticks per second
}__attribute__((packed)) PlatformInfo;

static PlatformInfo platformInfo;

static Trace_Source sources[50];

static Socket* commandSocket;
static Socket* streamSocket;

#define cTCP                        0x6
#define cIPV4                       0x800
#define cUDP                        17

#define cGETPLATFORMINFO            0x1
#define cSETDESTINATION             0x2
#define cGETSOURCES                 0x3
#define cSTARTDATASTREAM            0x4
#define cSTOPDATASTREAM             0x5

#define COMMAND_THREAD               0
#define STREAM_THREAD                1

static char rcvbuf[100];
static unint4 eventCounter = 0;

static bool streaming = false;
KernelThread* streamThread;

Trace::Trace() {
    current_entry = 0;
    current_buffer = &trace_buffer1;
    num_entries = 0;
    streamThread = 0;
    memset(sources, 0, sizeof(sources));

    trace_buffer1.TraceID = 0x23551a3f;
    trace_buffer2.TraceID = 0x23551a3f;

    platformInfo.clockTicksPerSecond = CLOCK_RATE;
    platformInfo.SystemID = 0;
    platformInfo.PlatformID = PLATFORM;

    streaming = false;
}

sockaddr streamDest;

/*****************************************************************************
 * Method: Trace::init()
 *
 * @description
 *
 *******************************************************************************/
void Trace::init() {
    sockaddr addr;
    addr.port_data = 46;
    addr.sa_data = 0;

    // setup the two sockets
    commandSocket = new Socket(cIPV4, SOCK_STREAM, cTCP);
    commandSocket->bind(&addr);

    streamSocket = new Socket(cIPV4, SOCK_DGRAM, cUDP);
    addr.port_data = 0;
    addr.sa_data = 0;
    streamSocket->bind(&addr);

    // use one workerthread "permanently" for the command socket handler
    Thread* thread = theOS->getKernelTask()->getCallbackThread(0, this, 200 ms, 50, COMMAND_THREAD);
    if (thread) {
        thread->setName("tracer");
    }
}

/*****************************************************************************
 * Method: Trace::callbackFunc(void* param)
 *
 * @description
 *
 *******************************************************************************/
void Trace::callbackFunc(void* param) {
    if (param == COMMAND_THREAD) {
        // wait for incoming tcp connection
        while (true) {
            int newSock = commandSocket->listen(pCurrentRunningThread);

            Socket* sessionSock = static_cast<Socket*>(pCurrentRunningThread->getOwner()->getOwnedResourceByFileDescriptor(newSock));
            printf("Incoming Trace control connection.\n");
            int len = sessionSock->recvfrom(pCurrentRunningThread, rcvbuf, 100, MSG_WAIT, 0);

            while (len >= 0) {
                // handle msg
                switch (rcvbuf[0]) {
                    case cGETPLATFORMINFO: {
                        printf("Sending platform info.\n");
                        // send platform info
                        sessionSock->sendto(&platformInfo, sizeof(platformInfo), reinterpret_cast<sockaddr*>(0));
                        break;
                    }
                    case cSETDESTINATION: {
                        // port and ip are big endian encoded
                        unint2 port = (rcvbuf[1] << 8) | rcvbuf[2];
                        unint4 ipaddress = (rcvbuf[3] << 24) | (rcvbuf[4] << 16) | (rcvbuf[5] << 8) | (rcvbuf[6]);

                        printf("Setting destination : port: %u ip: %x\n", port, ipaddress);
                        streamDest.port_data = htons(port);
                        streamDest.sa_data = htonl(ipaddress);
                        break;
                    }
                    case cGETSOURCES: {
                        printf("Sending sources.\n");
                        sessionSock->sendto(&sources, sizeof(sources), reinterpret_cast<sockaddr*>(0));
                        break;
                    }
                    case cSTARTDATASTREAM: {
                        if (streaming)
                        break;

                        streaming = true;

                        // start the workerthread for the stream
                        printf("Starting  data stream.\n");

                        if (streamThread == 0) {
                            // use one workerthread "permanently" for the command socket handler
                            streamThread = theOS->getKernelTask()->getPeriodicThread(0, this, 0, 10 ms, 41, reinterpret_cast<void*>(STREAM_THREAD));
                            if (streamThread) {
                                streamThread->setName("tracer tx");
                            } else {
                                printf("No thread available\n");
                            }
                        }

                        break;
                    }
                    case cSTOPDATASTREAM: {
                        streaming = false;
                        printf("Stopping data stream.\n");
                        if (streamThread) {
                            streamThread->stop();
                            streamThread = 0;
                        }
                    }
                }  // switch

                len = sessionSock->recvfrom(pCurrentRunningThread, rcvbuf, 100, MSG_WAIT, 0);
            }

            printf("Trace control connection closed.\n");
        }

    } else {
        // we must be the stream thread
        if (streaming) {
            /* initiate a buffer switch */
            Trace_Buffer* tosend_buffer = current_buffer;
            unint4 numEvents = num_entries;

            if (numEvents > NUM_TRACE_ENTRIES) {
                numEvents = NUM_TRACE_ENTRIES;
            }

            num_entries = 0;
            current_entry = 0;
            if (current_buffer == &trace_buffer1) {
                current_buffer = &trace_buffer2;
            } else {
                current_buffer = &trace_buffer1;
            }

            // send the events in UDP packets of maximum size 1400
            unint4 sendpos = 0;
            while (sendpos < numEvents) {
                int eventsToSend = 60;
                if (numEvents - sendpos < 60) {
                    eventsToSend = numEvents - sendpos;
                }

                int bytes = sizeof(Trace_Entry) * (eventsToSend) + 8;

                // copy header infront
                if (sendpos > 0) {
                    memcpy(reinterpret_cast<void*> ((unint4) (&tosend_buffer->buffer[sendpos]) - 8), tosend_buffer, 8);
                }

                streamSocket->sendto(reinterpret_cast<void*> ((unint4) (&tosend_buffer->buffer[sendpos]) - 8), bytes, &streamDest);
                sendpos += eventsToSend;
            }
        }
    }
}

/*****************************************************************************
 * Method: Trace::addSource(unint1 taskid, unint1 sourceid, const char* name)
 *
 * @description
 *
 *******************************************************************************/
ErrorT Trace::addSource(unint1 taskid, unint2 sourceid, const char* name) {
    // search for a free entry. sourceid == 0 may never be used
    for (int i = 0; i < 50; i++) {
        if (sources[i].sourceid == 0) {
            sources[i].taskid = taskid;
            sources[i].sourceid = sourceid;

            if (name != 0) {
                int len = strlen(name);
                if (len > 9)
                len = 9;
                memset(sources[i].name, 0, 10);
                memcpy(sources[i].name, name, len);
            }

            return (cOk);
        }
    }

    return (cError);
}

/*****************************************************************************
 * Method: Trace::removeSource(unint1 taskid, unint1 sourceid)
 *
 * @description
 *
 *******************************************************************************/
ErrorT Trace::removeSource(unint1 taskid, unint2 sourceid) {
    // search for the entry
    for (int i = 0; i < 50; i++) {
        if (sources[i].sourceid == sourceid && sources[i].taskid == taskid) {
            sources[i].taskid = 0;
            sources[i].sourceid = 0;
            return (cOk);
        }
    }

    return (cError);
}

ErrorT Trace::renameSource(unint1 taskid, unint2 sourceid, const char* name) {
    // search for the entry
    for (int i = 0; i < 50; i++) {
        if (sources[i].sourceid == sourceid && sources[i].taskid == taskid) {
            if (name != 0) {
                int len = strlen(name);
                if (len > 9)
                len = 9;
                memset(sources[i].name, 0, 10);
                memcpy(sources[i].name, name, len);
            }
            return (cOk);
        }
    }

    return (cError);
}

/*****************************************************************************
 * Method: Trace::trace_memAlloc(unint4 address, unint4 size)
 *
 * @description
 *
 *******************************************************************************/
void Trace::trace_memAlloc(unint4 address, unint4 size) {

}

/*****************************************************************************
 * Method: Trace::trace_memFree(unint4 address)
 *
 * @description
 *
 *******************************************************************************/
void Trace::trace_memFree(unint4 address) {

}

/*****************************************************************************
 * Method: Trace::trace_addEntry(unint1 type, unint1 taskid, unint1 sourceid)
 *
 * @description
 *
 *******************************************************************************/
void Trace::trace_addEntry(unint1 type, unint1 taskid, unint2 sourceid, unint4 argument) {
    TimeT time = 0;
    if (theClock) {
        time = theClock->getClockCycles();
    }
    current_buffer->buffer[current_entry].timestamp = time;
    current_buffer->buffer[current_entry].id = ((type) | (taskid << 8) | (sourceid << 16));
    current_buffer->buffer[current_entry].cnt = eventCounter++;
    current_buffer->buffer[current_entry].arg = argument;
    current_buffer->buffer[current_entry].arg2 = 0;
    current_entry = (current_entry + 1) & (NUM_TRACE_ENTRIES - 1);
    num_entries++;
}

/*****************************************************************************
 * Method: Trace::trace_addEntryStr(unint1 type, unint1 taskid, unint1 sourceid, const char* argument)
 *
 * @description
 *
 *******************************************************************************/
void Trace::trace_addEntryStr(unint1 type, unint1 taskid, unint2 sourceid, const char* argument) {
    TimeT time = 0;
    if (theClock) {
        time = theClock->getClockCycles();
    }
    current_buffer->buffer[current_entry].timestamp = time;
    current_buffer->buffer[current_entry].id = ((type) | (taskid << 8) | (sourceid << 16));
    current_buffer->buffer[current_entry].cnt = eventCounter++;
    current_buffer->buffer[current_entry].arg = 0;
    current_buffer->buffer[current_entry].arg2 = 0;
    if (argument != 0) {
        strncpy((char*) &current_buffer->buffer[current_entry].arg, argument, 8);
    }
    current_entry = (current_entry + 1) & (NUM_TRACE_ENTRIES - 1);
    num_entries++;
}
#endif

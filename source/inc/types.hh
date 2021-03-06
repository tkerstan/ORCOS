/*
 ORCOS - an Organic Reconfigurable Operating System
 Copyright (C) 2008 University of Paderborn

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _TYPES_HH
#define _TYPES_HH

/*
 * This file includes KERNEL ONLY Types.
 * Place cross kernelspace/userspace types inside the user library types.h
 */

/* include syscall library types (cross kernel/userspace used types) */
#include "inc/orcos_types.h"
#include <archtypes.h>
/* Inclusion from the user space library */
#include "inc/defines.h"

#define MB * 0x100000
#define KB * 0x400

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#define GETBITS(a, UP, LOW) ((a & (( (1 << (UP - LOW + 1)) -1) << LOW)) >> LOW)
#define SETBITS(a, UP, LOW, val) a = ((a & ~(( (1 << (UP - LOW + 1)) -1) << LOW)) | ((val & ((1 << (UP - LOW + 1)) -1)) << LOW) )

#define ATTR_CACHE_INHIBIT __attribute__((section (".cache_inhibit")))

typedef unsigned int size_t;

typedef int ErrorT;
typedef unint2 EndianT;
typedef unint2 CpuClassT;
typedef unint2 CpuVersionT;

/*!
 *  Type-defines for process management
 */
typedef unint1 TaskIdT;
typedef unint2 ThreadIdT;
typedef unint4 ResourceIdT;

/*!
 * General type defines
 */
typedef unsigned char  unchar;
typedef unsigned short unshort;
typedef unsigned int   unint;
typedef unsigned long  unlong;
typedef unint4 BitmapT;
typedef unint1 byte;
typedef void* PhysAddrT;

/* types for time representations */
/// seconds
typedef unint2 SecT;
/// milli seconds
typedef unint4 MilliSecT;
/// micro seconds
typedef unint4 MicroSecT;
/// nano  seconds
typedef unint8 NanoSecT;

/* types for frequency representation */
/// kilo hertz
typedef unint4 KHzT;
/// Mega hertz
typedef unint2 MHzT;


/*!
 * \brief Structure representing the description of a driver (name, version,services)
 */
typedef struct {
    const char* driverName, driverVersion;
    const int driverServices;
} DriverDescription;

/*!
 * The orcos module control block (MCB)
 * contains all references needed for executing.
 */
typedef struct {
    unint4 pf_module_init;
    unint4 pf_exit;
    unint4 pb_argument_area;
    unint4 i_max_argument_size;
    unint4 pf_low_level_send;     // address of the low_level_send method
    unint4 pf_getMacAddr;
    unint4 pf_getMacAddrSize;
    unint4 pf_getMTU;
    unint4 pf_getHardwareAddressSpaceId;
    unint4 pf_getBroadcastAddr;
    unint4 pf_enableIRQ;
    unint4 pf_disableIRQ;
    unint4 pf_clearIRQ;
    unint4 pf_recv;
    unint4 pf_readByte;
    unint4 pf_writeByte;
    unint4 pf_readBytes;
    unint4 pf_writeBytes;
} ORCOS_MCB;

typedef struct {
    void* argument1;
    void* argument2;
    void* argument3;
} ORCOS_module_args;

#ifdef __cplusplus

class CallableObject;

/*!
 * \brief Structure passed as the job parameter to the workertask if the job is a timed function call.
 */
typedef struct {
    //! absolute time in clock ticks to execute the function call
    TimeT time;
    //! object we want to call
    CallableObject* objectptr;
    //! parameter to pass to the method
    void* parameterptr;

} TimedFunctionCall;

/*!
 * \brief Structure passed as the job parameter to the workertask if the job is a periodic function call.
 *
 * First element in this structure has to be the TimedFunctionCall structure.
 */
typedef struct {
    //! the functioncall parameters
    TimedFunctionCall functioncall;
    //! the period of the function call in clock ticks
    TimeT period;
} PeriodicFunctionCall;

#endif

/*! \brief A linked list based packet layer structure
 *
 *  a packet_layer contains a number of bytes (e.g a IPV4 Header)
 *  and maybe a following layer.
 *
 *  Possible linked list:
 *  IPV4 -> UDP -> Payload
 */
typedef struct packet_layer {
    struct packet_layer* next;     // next packet layer
    const char* bytes;       // pointer to the block
    unint2 total_size;  // total size of all blocks in the linked list starting with this packet
    unint2 size;        // size of this block
} packet_layer;



/*****************************************************
 *
 * Definition of valid 'task_next_header' field values
 *
 ****************************************************/

#define TASK_CB_NONE        0    /* no next task cb header field */#define TASK_CB_CRC32        1    /* next task cb field is a crc32 sum of the task */#define TASK_CB_AUTH        2    /* next task cb field is an authentication header */
/********************************************
 *             PLATFORM IDENTIFIER as used
 *             inside the task CB -> platform
 *
 *    PLATFORM DEPENDENT       PLATFORM
 * ---------------------------------------
 * |     24 Bit             |   8 Bit    |
 * ---------------------------------------
 *
 *                  ARM platform:
 * ---------------------------------------
 * |  23 BIT         |  T   |     0x1    |
 * ---------------------------------------
 *   T BIT: start task in thumb mode
 *
 ********************************************/

#define PLATFORM_ARM        0x1
#define PLATFORM_PPC        0x2
#define PLATFORM_SPARC      0x3
#define PLATFORM_X86        0x4

/*!
 * \brief Structure holding informations about a task.
 *
 * This structure is used to read informations written by the linker about
 * into the binary file of a task.
 *
 * logical == virtual (if MMU enabled), physical otherwise
 */
typedef struct {
    unint4 task_magic_word;               // needs to be 0x230f7ae9
    unint4 task_next_header;            // defines the following task header field
    unint4 platform;                    // platform identifier, see platform defines

    long   task_start;                   // logical task start address

    long   task_entry_addr;               // logical entry function address
    long   task_thread_exit_addr;       // logical address of the thread_exit method inside the task

    long   task_heap_start;               // logical data start address
    long   task_end;                     // logical task end address

    thread_attr_t initial_thread_attr;     // attributes of the initial thread
} taskTable;

/*!
 * \brief CRC32 optional task header. Contains the CRC32 of the task area + taskTable.
 */
typedef struct {
    unint4 next_header;
    unint4 crcStart;    /* Start address the checksum is calculated over */
    unint4 crcEnd;      /* End address the checksum is calculated over */
    unint4 taskCRC32;   /* The CRC32 value in target architecture endianess */
} taskCRCHeader;

/*!
 * \brief Resource types as bit flags of ORCOS resources. As a resource can fulfill multiple roles
 *        these types are defined as flags.
 */
typedef enum {
    cDirectory          = 1 << 0,
    cStreamDevice       = 1 << 1,
    cCommDevice         = 1 << 2,
    cGenericDevice      = 1 << 3,
    cFile               = 1 << 4,
    cSocket             = 1 << 5,
    cUSBDriver          = 1 << 6,
    cBlockDevice        = 1 << 7,
    cPartition          = 1 << 8,
    cSharedMem          = 1 << 9,
    cKernelVariable     = 1 << 10,
    cOverlay            = 1 << 11,
    cTimerDevice        = 1 << 12,
    cThread             = 1 << 13,
    cTask               = 1 << 14,
    cFifo               = 1 << 15,
    cNonRemovableResource = cStreamDevice | cOverlay | cKernelVariable| cPartition | cBlockDevice | cCommDevice | cGenericDevice | cSocket | cUSBDriver | cTimerDevice | cThread | cTask,
    cAnyNoDirectory       = cStreamDevice | cCommDevice | cGenericDevice | cFile | cSocket | cUSBDriver | cBlockDevice | cSharedMem | cTimerDevice | cFifo,
    cAnyResource          = cStreamDevice | cCommDevice | cGenericDevice | cFile | cSocket | cUSBDriver | cDirectory | cBlockDevice | cSharedMem | cOverlay | cTimerDevice | cFifo
} ResourceType;

#endif /* _TYPES_HH */


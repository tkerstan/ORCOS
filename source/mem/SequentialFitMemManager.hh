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

#ifndef SEQUENTIALFITMEMMANAGER_HH_
#define SEQUENTIALFITMEMMANAGER_HH_

#include "MemManager.hh"
#include <inc/types.hh>
#include <inc/error.hh>
#include <Alignment.hh>


typedef struct Chunk_Header {
	Chunk_Header *prev_chunk;
	Chunk_Header *next_chunk;
	unint1 state : 1;
	unint4 size  : 31; //(sizeof(unint4)-1);
} __attribute__((packed)) Chunk_Header;

// possible States of a memory chunk
#define FREE 0
#define OCCUPIED 1

/*
 * Defines the number of bytes placed between two consecutive chunks
 * in order to cope with erroneous data bounds accesses.
 */
#define SAFETY_BUFFER 0x0

// size of the Chunk Header
//#define SZ_HEADER 16
// Minimum payload needed for a chunk when splitting
#define MINIMUM_PAYLOAD 0x20

// activating precisely validity checks checks a freed chunk on correct placement
// inside the chunk list. However, takes a lot more time.
#define PRECISE_VALIDITY_CHECK 0

// MEM_LAST_FIT will start searching at the last freed object for chunks to allocated
// this is faster than starting to search from the beginning
// however: this will lead to a higher number of chunks, thus, increasing the number
// of reserved (used) bytes not allocateable for the user.
#ifndef MEM_LAST_FIT
#define MEM_LAST_FIT 1
#endif

/*!
 * \ingroup memmanager
 *
 * \brief Class is a concrete implementation of a memory manager,
 *  providing allocation and release of memory. The memory is organized in a linked list of memory chunks
 *
 * SequentialFitMemManager only deals with a contiguous memory segment
 *
 * The memory is divided in chunks which are organized in alinked list
 * The organization of the linked list of memory chunks is as follows:
 * every chunk has a Header containing management information and a payload containing the data. So there exists
 * no additional structure in the memory to keep track of free and allocated chunks
 * The Header is 16 bytes and consists of: <br>
 * 0      4      8       12      16 <br>
 * ------------------------------ <br>
 * | PREV | NEXT | STATE | SIZE | with <br>
 * ------------------------------ <br>
 * - PREV: the preceding chunk in the list <br>
 * - NEXT: the following chunk in the list <br>
 * - STATE: the state of the chunk: free(possible to allocate) or occupied(contains already data) <br>
 * - SIZE: the payload size of the chunk <br>
 *
 * Directly after the Header starts the payload section. Every chunk is addressed by the start address of its payload.
 *
 * If memory should be allocated the linked list of chunks is traversed to find a free chunk of sufficient size. If there are many
 * chunks which fulfill these conditions, the chunk is chosen according to a placement policy which can be chosen by SCL configuration:
 * - FirstFit -> choose the first chunk fulfilling the conditions (DEFAULT)
 * - NextFit -> the same as FirstFit, but the search is not always started from the beginning of the linked list but from the last allocated chunk
 * - BestFit -> choose the chunk with the smallest unused rest of payload
 * - WorstFit -> choose the chunk with the biggest unused rest of payload
 *
 * If a chunk is chosen for allocation and its payload leaves a rest of unused memory which is bigger than the Header size + the Minimum Payload,
 * then a split operation is performed on this chunk meaning the chunk is divided in two chunks: one containing the allocated memory and one containing the unused rest.
 *
 * If memory is released a merge operation is performed if the chunk containing the released memory has a neighbor chunk which is tagged as free. These free chunks are merged into
 * one chunk.
 *
 */
class SequentialFitMemManager: public MemManager {
private:
    //! Pointer to the first memory chunk in the linked list
    Chunk_Header* startChunk;

#if MEM_CACHE_INHIBIT
    Chunk_Header* startIChunk;
#endif

    unint4 free_mem;

    unint4 overhead;

    /*!
     * \brief the memory chunk at the given address is splitted into a chunk of the given size and a chunk containing the remaining unused memory (if possible)
     *
     * The splitting of the chunk is only possible if the following condition holds:
     * size of the payload of this chunk is bigger or equal to requested size + size of header + minimum payload size
     *
     * If true -> the chunk is splitted in two chunks A and B where A is of the requested size and will contain the data for which the allocation has been called and
     *            B will contain the remaining unused memory of the payload
     *
     * If false -> the whole chunk is used and its state is set to occupied.
     */
    void split( Chunk_Header*, size_t );

    /*!
     * \brief the memory chunk at the given address and his free neighbor chunks are merged into one chunk
     *
     * There are four possible scenarios: <br>
     *    Prev Chunk     Freed Chunk     Next Chunk <br>
     * 1)      O              O               O  <br>
     * 2)      F              O               O  <br>
     * 3)      O              O               F <br>
     * 4)      F              O               F <br>
     *
     * in 1) no merging of chunks is performed, only the state of the freed chunk is set to FREE. <br>
     * in 2) the Prev Chunk and the Freed Chunk are merged - the PREV field of the Next Chunk is set to <br>
     *       the Prev Chunk, the NEXT field of the Prev Chunk is set to teh Next Chunk, the SIZE field of <br>
     *       the Prev Chunk is set to its old SIZE + size of header + SIZE(Freed Chunk) <br>
     * in 3) the Next Chunk and the Freed Chunk are merged - the NEXT field of the Freed Chunk is set to the NEXT field <br>
     *       of the Next Chunk, the PREV field of the chunk following the Next Chunk is set to Freed Chunk, the SIZE field of the <br>
     *       Freed Chunk is set to its old  SIZE + size of header + SIZE(Next Chunk) <br>
     * in 4) the Freed Chunk is merged with both neighbors - the NEXT field of the Prev Chunk is set to the NEXT field of the Next Chunk, <br>
     *       the PREV field of the chunk following the Next Chunk is set to the Prev Chunk, the SIZE field of the Prev Chunk is set to its <br>
     *       old SIZE + 2*size of header + SIZE(Freed Chunk) + SIZE(Next Chunk)  <br>
     */
    void merge( Chunk_Header* );

    /*!
     * \brief it is checked for the given address if it points to a memory chunk in the linked list
     *
     * The linked list of memory chunks is traversed. If the given address is the starting address of the payload section of a chunk, the method
     * returns true, otherwise false.
     */
    bool isValidChunkAddress( Chunk_Header* );

    /*!
     * \brief a free memory chunk of sufficient size is searched according to the configured placement policy
     *
     * This chunk is searched by traversing the linked list of memory chunks.
     * The chunk is chosen according to the configured placement policy (which can be configured by the user using SCL).
     *
     */
    Chunk_Header* getFittingChunk( size_t, bool, unint4, Chunk_Header* );

#if MEM_LAST_FIT == 1
    //! Pointer to the memory chunk which has been lastly allocated - only used for NextFit-Policy
    Chunk_Header* lastAllocatedChunk;

	#if MEM_CACHE_INHIBIT
		Chunk_Header* lastAllocatedIChunk;
	#endif

#endif
protected:

public:



    /*!
     * \brief creates a new SequentialFitMemManager
     *
     * Constructor. The managed memory segment starts at parameter startAddr and end at parameter endAddr
     */
    SequentialFitMemManager( void* startAddr, void* endAddr);

#if MEM_CACHE_INHIBIT
    SequentialFitMemManager( void* startAddr, void* endAddr, void* istartAddr, void* iendAddr);
#endif

    /*!
     * \brief allocates memory of given size from the managed memory segment
     *
     * Implements the corresponding method in MemManager.hh
     *
     * The linked list of memory chunks is traversed in a search for a free chunk with sufficient payload size.
     * The chunk to be allocated is chosen according to a configured placement policy(FirstFit, NextFit, BestFit, WorstFit).
     * After that a split operation is performed by calling the split method on the allocated chunk: the chunk is divided into two chunks (if possible)
     */
    void* alloc( size_t, bool = true, unint4 align_val = ALIGN_VAL);

#if MEM_CACHE_INHIBIT
    /*!
     *Allows allocation inside the Cache Inhibit data section
     */
    void* alloci( size_t, bool = true, unint4 align_val = ALIGN_VAL);
#endif

    /*!
     * \brief releases allocated memory
     *
     * Implements the corresponding method in MemManager.hh
     *
     * If the given address is a valid address of a memory chunk,
     * it is attempted by calling the merge method to merge the memory chunk which is freed with his neighboring chunks (if these are free)
     *
     */
    ErrorT free( void* );


    /*!
     * \brief method to get the used heap size
     *
     * Implements corresponding method in MemManager.hh
     *
     * The linked list is traversed and returned is the sum of all Headers of free chunks and the Headers and Payload of occupied chunks
     */
    size_t getUsedMemSize(size_t &u_overhead, size_t &u_free_mem);
};

#endif /*SEQUENTIALFITMEMMANAGER_HH_*/
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

#ifndef SINGLECPUDISPATCHER_H_
#define SINGLECPUDISPATCHER_H_

#include "process/IdleThread.hh"
#include "db/LinkedListDatabase.hh"
#include "process/Task.hh"
#include Kernel_Scheduler_hh

/*!
 * \brief Class managing the execution of threads on a single cpu.
 *
 *
 * This class is a management object which is responsible of starting/stopping
 * threads on a single cpu. For this a ThreadScheduler is needed which decides
 * which thread is the next thread to run on the cpu by its implemented scheduling
 * policy in the Scheduler::getNext() method.
 *
 * The CPUManager restores also threads that have a context (thus have at least run one time)
 * by calling the restoreContext(Thread*) method which needs to be implemented for the architecture
 * the operating systems is supposed to run on.
 */
class SingleCPUDispatcher {

    //! scheduler maintaining the ready list.
DEF_Kernel_SchedulerCfd

private    :
    //! list containing the currently blocked threads
    LinkedListDatabase* blockedList;

    //! List containing the currently sleeping threads
    LinkedListDatabase* sleepList;

    //! List containing the threads currently waiting for a signal
    LinkedListDatabase* waitList;

    //! The idle Thread
    IdleThread* idleThread;

    //! The shortest sleeptime of all threads currently at sleep
    unint4 shortest_sleepcycles;

public:
    SingleCPUDispatcher();
    ~SingleCPUDispatcher();

    //! Returns the current number of elements in the dispatchers blockedList
    unint getSizeOfBlockedList() {return blockedList->getSize();};

    //! Returns the current number of elements in the dispatchers sleepList
    unint getSizeOfSleepList() {return sleepList->getSize();};

    //! Set the idleThread of this CPU
    inline
    void setIdleThread(IdleThread* idleT)
    {   idleThread = idleT;};

    inline
    LinkedListDatabase* getSleeplist()
    {   return this->sleepList;}

    /*!
     * \brief The dispatch function triggered by the TimerDevice on timer interrupt
     *
     * Calling this functison results in rescheduling the cpu. Thus the next
     * thread is taken from the cpu and assigned to the cpu (started/resumed).
     * This might be the currently running thread again depending on the schedulers
     * policy.
     *
     * dt is the amount of cycles passed since last time executing this method
     */
    void dispatch(unint4 dt);

    /*!
     * Calls the dispatch(dt) method while calculating the time spent
     */
    void dispatch();

    /*!
     * \brief sleep method which sends the given / current thread to sleep mode
     */
    void sleep(int cycles, LinkedListDatabaseItem* pSleepDbItem = pRunningThreadDbItem);

    /*!
     * \brief block method which sends the current thread to blocked mode
     */
    void block(Thread* thread);

    /*!
     *  \brief Unblocks the given thread
     */
    void unblock(Thread* thread);

    void sigwait(Thread* thread);

    void signal(void* sig);

    /*!
     * \brief Terminates the given thread
     */
    void terminate_thread(Thread* thread);
};

#endif /*SingleCPUDispatcher_H_*/
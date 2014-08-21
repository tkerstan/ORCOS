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

#include "process/Task.hh"
#include "kernel/Kernel.hh"
#include "filesystem/Resource.hh"
#include "inc/memtools.hh"
#include "filesystem/SharedMemResource.hh"
// the kernel object
extern Kernel* theOS;
extern Kernel_ThreadCfdCl* pCurrentRunningThread;

// static non-const member variable initialization
// will be executed in ctor
TaskIdT Task::globalTaskIdCounter;

ArrayList *Task::freeTaskIDs;

/*--------------------------------------------------------------------------*
 ** Task::Task
 *---------------------------------------------------------------------------*/
Task::Task( Kernel_MemoryManagerCfdCl* memoryManager, taskTable* tasktbl) :
        aquiredResources(20),
        exitValue(0),
        memManager(memoryManager),
        stopped(false),
        myTaskDbItem(0),
        platform_flags(0)
{
    myTaskId = (TaskIdT) ((unint4) Task::freeTaskIDs->removeHead());

    /* store reference to my tasktable */
    this->tasktable = tasktbl;
    /* Set initial name and working directory */
    strcpy(this->name,"No Name");
    strcpy(this->workingDirectory,"/");
    stdOutput = theOS->getStdOutputDevice();

    /* create initial thread for this task */
    new Kernel_ThreadCfdCl((void*) tasktbl->task_entry_addr, (void*) tasktbl->task_thread_exit_addr, this, memoryManager,
    DEFAULT_USER_STACK_SIZE, (void*) (&tasktbl->initial_thread_attr), false);

}

Task::Task() :
        aquiredResources(10),
        exitValue(0),
        memManager(0),
        stopped(false),
        myTaskDbItem(0),
        platform_flags(0)
{
    tasktable = 0;
    myTaskId = (TaskIdT) ((unint4) Task::freeTaskIDs->removeHead());
    /* Set initial name and working directory */
    strcpy(this->name,"No Name");
    strcpy(this->workingDirectory,"/");
    stdOutput = theOS->getStdOutputDevice();
}

Task::~Task() {

    LinkedListItem* litem = this->suspendedThreadDb.getHead();
    while (litem != 0)
    {

        Thread* t = (Thread*) litem->getData();
        litem = litem->getSucc();
        this->suspendedThreadDb.remove(t);

        delete t;
    }

    delete this->myTaskDbItem;

    delete this->memManager;

}

Resource* Task::getOwnedResourceById(ResourceIdT id) {
    // parse database for a resource with id 'id'
    for (int i = 0; i < this->aquiredResources.size(); i++)
    {
        Resource* res = (Resource*) aquiredResources.getItemAt(i);
        if (res->getId() == id)
            return (res);
    }

    return (0);
}

ErrorT Task::acquireResource(Resource* res, Thread* t, bool blocking) {
    //REMARK: check wheter t is a thread belonging to this task
    // check whether the task already owns this resource
    if (getOwnedResourceById(res->getId()) != 0)
    {  // task already owns this resource

        LOG(PROCESS, TRACE, "Task: Resource already owned");
        // increase the reference count of the res in this task
        // return the id of the resource so thread can continue working
        return (res->getId());
    }
    else
    {
        LOG(PROCESS, TRACE, "Task: acquiring resource %d", res->getId());
        int error = res->acquire(t, blocking);
        if (isError(error)) {
            LOG(PROCESS,ERROR,"Task::aquireResource() acquire failed: %d",error);
        }
        return (error);
    }
}

ErrorT Task::releaseResource(Resource* res, Thread* t) {
    //REMARK: check wheter t is a thread belonging to this task
    // get the resource to close by id from the tasks owned resource database
    if (res != 0)
    {
        if (res->getType() == cSocket)
        {
            res->release(t);
            Socket* s = (Socket*) res;
            LOG(KERNEL, DEBUG, "Task::removeThread(): destroying socket!");
            delete s;
            return (cOk );
        }
        else if (res->getType() == cSharedMem)
        {
            SharedMemResource* shmres = (SharedMemResource*) res;
            shmres->unmapFromTask(t->getOwner());
            int retval = shmres->release(t);
            /* Cleanup unused areas */
            if (shmres->getMappedCount() == 0)
                delete shmres;

            return (retval);
        }


       int error = res->release(t);
       if (isError(error)) {
           LOG(PROCESS,ERROR,"Task::releaseResource() release failed: %d",error);
       }
       return (error);
    }

    return (cError );
}

void Task::run() {
    // run the very first thread!
    // this is supposed to be the thread at the head of the threaddb
    LinkedListItem* litem = this->threadDb.getHead();
    if (litem != 0)
    {
        Kernel_ThreadCfdCl* thread = (Kernel_ThreadCfdCl*) litem->getData();

#ifdef REALTIME
        // set the arrival time of this realtime thread
        thread->arrivalTime = theOS->getClock()->getClockCycles() + thread->phase;
        theOS->getCPUScheduler()->computePriority(thread);
#endif

        // announce the thread to the scheduler (this does not mean running it directly)
        thread->run();
    }
}

void Task::terminate() {
    LOG(KERNEL, DEBUG, "Task::terminate()");
    LinkedListItem* litem = this->threadDb.getHead();
    while (litem != 0)
    {
        Thread* t = (Thread*) litem->getData();

        // first remove all other threads than the currentRunningThread
        if (t != pCurrentRunningThread)
        {
            litem = litem->getSucc();
            t->terminate();
        }
        else
            litem = litem->getSucc();
    }

    // add our id back to the database
    Task::freeTaskIDs->addTail((ListItem*) (unint4) this->getId());

    litem = this->threadDb.getHead();
    if (litem != 0)
    {
        // thread must have been running. terminate it as last thread
        Thread* t = (Thread*) litem->getData();
        t->terminate();
    }

#ifdef HAS_Board_HatLayerCfd
    /* We may only unmap the entries of the task
     * if we are not executing in its address space..
     * The Kernel code would be unaccessible then. We may also
     * be running on the tasks stack..
     **/
    /*if (pCurrentRunningTask != this)
     theOS->getHatLayer()->unmap((void*)this->tasktable->task_start_addr,this->getId());*/
    //theOS->getHatLayer()->unmapAll(this->getId());
#endif

}

void Task::addThread(Thread* t) {
    this->threadDb.addTail(t);
}

void Task::removeThread(Thread* t) {
    LOG(KERNEL, DEBUG, "Task::removeThread() removing %x",t);

    LinkedListItem* litem = this->threadDb.getItem(t);
    if (litem != 0)
    {
        // ok valid thread. remove it from its database (this->threadDb)
        litem->remove();

        // store in the suspended database
        suspendedThreadDb.addTail(litem);
        LOG(KERNEL, TRACE, "Task::removeThread() added to suspended list");

        // check if all threads are terminated
        if (this->threadDb.isEmpty())
        {
            // no more threads. this task can be destroyed.
            // free all acquired resources if applicable

            LOG(KERNEL, DEBUG, "Task::removeThread(): being destroyed! aq_size = %d", this->aquiredResources.size());

            Resource* res = (Resource*) this->aquiredResources.removeTail();
            while (res != 0)
            {
                LOG(KERNEL, DEBUG,"Task::removeThread(): destroying resource of type %d",res->getType());
                // the last thread forces all resources to be released
                res->release(t);

                if (res->getType() == cSocket)
                {
                    Socket* s = (Socket*) res;
                    LOG(KERNEL, DEBUG, "Task::removeThread(): destroying socket!");
                    delete s;
                }

                res = (Resource*) this->aquiredResources.removeTail();
            }

            if (myTaskDbItem != 0)
                this->myTaskDbItem->remove();

            // do additional cleanup

        }
    }
}

LinkedListItem* Task::getSuspendedThread(unint4 stacksize) {
    LinkedListItem* litem = this->suspendedThreadDb.getHead();
    while (litem != 0)
    {
        Kernel_ThreadCfdCl* t = (Kernel_ThreadCfdCl*) litem->getData();
        if (((unint4) t->threadStack.endAddr - (unint4) t->threadStack.startAddr)
                >= stacksize)
        {
            litem->remove();
            return (litem);
        }
    }

    return (0);
}

Kernel_ThreadCfdCl* Task::getThreadbyId(ThreadIdT threadid) {
    LinkedListItem* litem = threadDb.getHead();

    while (litem != 0
            && ((Kernel_ThreadCfdCl*) litem->getData())->getId() != threadid)
        litem = litem->getSucc();

    if (litem != 0)
        return ((Kernel_ThreadCfdCl*) litem->getData());
    else
        return (0);
}

TaskIdT Task::getIdOfNextCreatedTask() {
    return ((TaskIdT) ((unint4) Task::freeTaskIDs->getHead()));
}

void Task::stop() {
    // stop the execution of all child threads
    LinkedListItem* litem = this->threadDb.getHead();
    while (litem != 0)
    {
        ((Thread*) litem->getData())->stop();
        litem = litem->getSucc();
    }
    // done this task is stopped
}

void Task::resume() {
    // resume the execution of all child threads
    LinkedListItem* litem = this->threadDb.getHead();
    while (litem != 0)
    {
        ((Thread*) litem->getData())->resume();
        litem = litem->getSucc();
    }
    // done this task is resumed
}


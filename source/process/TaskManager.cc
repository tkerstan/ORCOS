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

#include "TaskManager.hh"
#include <kernel/Kernel.hh>
#include <db/LinkedListDatabase.hh>
#include "inc/crc32.h"

extern Kernel* theOS;
extern Task* pCurrentRunningTask;

//! table containing references to the tasktables of every task (generated by linker), which
// are available at startup
extern unint4 tasktable;
extern Kernel* theOS;
extern void* __KERNELEND;
extern void* __RAM_END;



void TaskManager::registerMemPages() {

	unint4 num_tasks = tasktable;
	for ( unint4 i = 1; i <= num_tasks * 3; i+=3 ) {

		// get the taskTable of task number i
		taskTable* task_info = (taskTable*) *( &tasktable + i );
		unint4 task_end = (unint4) *( &tasktable + i + 2 );

		theOS->getRamManager()->markAsUsed((unint4) task_info,task_end,-1);
	}
}


Task* TaskManager::getTask( int taskId ) {

    for ( LinkedListDatabaseItem* lldi = this->taskDatabase->getHead(); lldi != 0; lldi = lldi->getSucc() ) {
        Task* task = (Task*) lldi->getData();
        if ( task->getId() == taskId ) {
            return task;
        }
    }

    return 0;
}


ErrorT TaskManager::checkValidTask(taskTable* taskCB) {

	if (taskCB->task_magic_word != 0x230f7ae9) return cInvalidCBHeader;
	if ((void*) taskCB->task_heap_end <= (void*) taskCB->task_heap_start) return cInvalidCBHeader;
	if ((taskCB->platform & 0xff) != PLATFORM) return cInvalidCBHeader;

	#ifndef HAS_Board_HatLayerCfd
		if ((void*) taskCB->task_heap_start < &__KERNELEND) return cInvalidCBHeader;
		if ((void*) taskCB->task_heap_end < &__KERNELEND) return cInvalidCBHeader;
		if ((void*) taskCB->task_heap_start > &__RAM_END) return cInvalidCBHeader;
		if ((void*) taskCB->task_heap_end > &__RAM_END) return cInvalidCBHeader;

		if ((void*) taskCB->task_start_addr < &__KERNELEND) return cInvalidCBHeader;
		if ((void*) taskCB->task_start_addr > &__RAM_END) return cInvalidCBHeader;
		if ((void*) taskCB->task_data_end < &__KERNELEND) return cInvalidCBHeader;

		// check for overlapping tasks if we do not virtual memoryS
		for ( LinkedListDatabaseItem* lldi = this->taskDatabase->getHead(); lldi != 0; lldi = lldi->getSucc() ) {
			Task* task = (Task*) lldi->getData();
			// the only task holding no task table would be the workertask!
			if (task->tasktable != 0) {
				if (   task->tasktable->task_start_addr > taskCB->task_start_addr
					&& task->tasktable->task_start_addr < taskCB->task_data_end) return cInvalidCBHeader;

				if (   task->tasktable->task_data_end > taskCB->task_start_addr
					&& task->tasktable->task_data_end < taskCB->task_data_end) return cInvalidCBHeader;
			}
		}
	#endif


	//TODO: check for crc header field and check crc value

	return cOk;

}

void TaskManager::initialize() {
    unint4 num_tasks = tasktable;
	register Kernel_MemoryManagerCfdCl* OSMemManager = theOS->getMemoryManager();

	LOG(KERNEL,INFO,(KERNEL,INFO,"Creating Initial Tasks"));

	// create the initial set of tasks
	for ( unint4 i = 1; i <= num_tasks * 3; i+=3 ) {

		// get the taskTable of task number i
		taskTable* task_info = (taskTable*) *( &tasktable + i );
		LOG(KERNEL,INFO,(KERNEL,INFO,"TaskCB @0x%x", task_info));

		// create the memory manager for the task. The memory manager will be inside the
		// kernel space
		void* memaddr = OSMemManager->alloc(sizeof(Kernel_MemoryManagerCfdCl),true);

		#ifndef HAS_Board_HatLayerCfd
			Kernel_MemoryManagerCfdCl* task_memManager =
					   new ( memaddr ) Kernel_MemoryManagerCfdCl( (void*) task_info->task_heap_start, (void*) task_info->task_heap_end);


			if (checkValidTask(task_info) != cOk) {
				LOG(KERNEL,ERROR,(KERNEL,ERROR,"Task invalid.. dropping"));
				continue;
			}

		   /*
			* create a new Task CB in Kernel Space!! only holds information about the task.
			* The task code itself remains at task_info->task_start_addr
			*/
		   Task* task = new Task( task_memManager, task_info );

		   LOG(KERNEL,INFO,(KERNEL,INFO,"TaskID %d:" ,task->getId()));
		   LOG(KERNEL,INFO,(KERNEL,INFO,"start at 0x%x, end  at 0x%x" , task->getTaskTable()->task_start_addr, task->getTaskTable()->task_heap_end));
		   LOG(KERNEL,INFO,(KERNEL,INFO,"entry at 0x%x, exit at 0x%x" , task->getTaskTable()->task_entry_addr, task->getTaskTable()->task_thread_exit_addr));
		   LOG(KERNEL,INFO,(KERNEL,INFO,"heap  at 0x%x, size %d" , task->getTaskTable()->task_heap_start, (int) task->getTaskTable()->task_heap_end - (int)  task->getTaskTable()->task_heap_start));

		#else
		   // we get the ID of the task we will create next
		   TaskIdT tid = Task::getIdOfNextCreatedTask();

		   // get the heap start address
		   unint4 task_heap_start = (unint4) *( &tasktable + i + 1 );
		   // get the task end address
		   unint4 task_end = (unint4) *( &tasktable + i + 2 );
		   // get the size of the task area
		   unint4 size = task_end - (unint4) task_info;

		   LOG(KERNEL,INFO,(KERNEL,INFO,"Task %d:" ,tid));
		   LOG(KERNEL,INFO,(KERNEL,INFO,"start at 0x%x, end at 0x%x" , task_info, task_end));
		   LOG(KERNEL,INFO,(KERNEL,INFO,"heap at 0x%x, size %d" , task_heap_start, size));

		   theOS->getRamManager()->markAsUsed((unint4) task_info,task_end,tid);

		   // create the vm map for the task! protection = 7 = RWX, ZoneSelect = 3
		   theOS->getHatLayer()->map((void*) LOG_TASK_SPACE_START,(void*) task_info, size ,7,3,tid, !ICACHE_ENABLE);
		   // now since the task is mapped activate its virtual memory map by setting the pid
		   //theOS->getHatLayer()->dumpPageTable(tid);
		   SETPID(tid);

		   if (checkValidTask((taskTable*) LOG_TASK_SPACE_START) != cOk) {
				LOG(KERNEL,ERROR,(KERNEL,ERROR,"Task invalid.. dropping"));
				theOS->getHatLayer()->unmap((void*) LOG_TASK_SPACE_START);
				SETPID(0);
				continue;
			}


			Kernel_MemoryManagerCfdCl* task_memManager =
					new(memaddr)  Kernel_MemoryManagerCfdCl((void*) (LOG_TASK_SPACE_START + ( task_heap_start -  (unint4) task_info)),
							(void*) (LOG_TASK_SPACE_START + size ) );


			/*
			 * create a new Task CB in Kernel Space!! only holds information about the task.
			 * The task code itself remains at task_info->task_start_addr
			 */
			Task* task = new Task( task_memManager, (taskTable*) LOG_TASK_SPACE_START );
			task->platform_flags = ((taskTable*) LOG_TASK_SPACE_START)->platform;
		#endif

		this->taskDatabase->addTail( task );
		task->myTaskDbItem = this->taskDatabase->getTail();
		task->run();


	#ifdef HAS_Board_HatLayerCfd
		SETPID(0);
	#endif
	}



}

ErrorT TaskManager::removeTask(Task* task) {
	if (task == 0) return (cError);

	// mark the used pages as free
	theOS->getRamManager()->freeAll(task->getId());

	task->terminate();

	// now remove all its threads from the scheduler
	delete task;

	return (cOk);
}

ErrorT TaskManager::loadTaskFromFile(File* file, TaskIdT& tid, char* arguments,unint2 arg_length) {

	// task loading only supported if virtual memory is activated
	#ifndef HAS_Board_HatLayerCfd
		return cError;
	#endif

	LOG(KERNEL,DEBUG,(KERNEL,DEBUG,"TaskManager::loadTaskFromFile: loading file."));

    // we get the ID of the task we will create next
	tid = Task::getIdOfNextCreatedTask();
	if (tid == 0) {
		LOG(KERNEL,WARN,(KERNEL,WARN,"TaskManager: no more free Task IDs."));
		return cError;
	}

	unint4 task_start = (unint4) theOS->getRamManager()->alloc(file->getFileSize(),tid);
	LOG(KERNEL,INFO,(KERNEL,INFO,"TaskManager: new Task will be placed at 0x%x", task_start));

	// no free page available
	if (task_start == 0) return cError;

	// load the file into memory
	// create a temporary vm map
	theOS->getHatLayer()->map((void*) task_start,(void*) task_start, file->getFileSize() ,7,3,pCurrentRunningTask->getId(), !ICACHE_ENABLE);

	// read the file into memory
	unint4 size = file->getFileSize();

	// TODO: this is a hotfix to be sure the task is loaded correctly...
	// however, the current running task may be reading / modifying it? (should this be possible)
	// thats why we need to reset the position to the beginning.
	file->resetPosition();

	// read the complete task into main memory
	ErrorT error = file->readBytes((char*) task_start, size);

	if (error < 0) {
		theOS->getHatLayer()->unmap((void*) task_start);
		theOS->getRamManager()->freeAll(tid);
		return error;
	}

	unint4 crc = crc32((char*) task_start, (size_t)size);

	LOG(KERNEL,INFO,(KERNEL,INFO,"TaskManager::loadTaskFromFile: Task CRC32: 0x%x",crc));

	error = this->checkValidTask((taskTable*) task_start);
	if (error < 0) {
		LOG(KERNEL,ERROR,(KERNEL,ERROR,"TaskManager::loadTaskFromFile: Invalid Task.. checkValidTask() failed: %d",error));
		theOS->getHatLayer()->unmap((void*) task_start);
		theOS->getRamManager()->freeAll(tid);
		return error;
	}

	// align arg_length
	if (arg_length > 0)
		arg_length = (arg_length + 4) & (~0x3);

	LOG(KERNEL,DEBUG,(KERNEL,DEBUG,"TaskManager::loadTaskFromFile: valid task."));

	register taskTable* tt = (taskTable*) task_start;

    // create the vm map for the task! protection = 7 = RWX, ZoneSelect = 3
    theOS->getHatLayer()->map((void*) LOG_TASK_SPACE_START,(void*) task_start, tt->task_heap_end - tt->task_start_addr ,7,3,tid, !ICACHE_ENABLE);

    // create the memory manager for the task. The memory manager will be inside the
	// kernel space
	void* memaddr = theOS->getMemoryManager()->alloc(sizeof(Kernel_MemoryManagerCfdCl),true);

	LOG(KERNEL,INFO,(KERNEL,INFO,"TaskManager: new Task memory: [0x%x - 0x%x]", tt->task_heap_start,tt->task_heap_end));

	// be sure its in register as we switch stack afterwards
	register Kernel_MemoryManagerCfdCl* task_memManager =
			new(memaddr)  Kernel_MemoryManagerCfdCl((void*) (tt->task_heap_start + DEFAULT_USER_STACK_SIZE + arg_length), (void*) tt->task_heap_end );

	LOG(KERNEL,DEBUG,(KERNEL,DEBUG,"TaskManager::loadTaskFromFile: creating Task object."));
	/*
	 * create a new Task CB in Kernel Space!! only holds information about the task.
	 * The task code itself remains at task_info->task_start_addr
	 */
	Task* task = new Task( task_memManager, tt );

	// copy arguments
	if (arguments != 0) {
		char* args = (char*) (task_start + (tt->task_heap_start - LOG_TASK_SPACE_START) + DEFAULT_USER_STACK_SIZE);
		memcpy(args,arguments,arg_length);
		// set argument of the initial thread
		Kernel_ThreadCfdCl* thread = (Kernel_ThreadCfdCl*) task->getThreadDB()->getHead()->getData();
		thread->arguments = (void*) (tt->task_heap_start + DEFAULT_USER_STACK_SIZE);
	}

	// for the future set it to the correct VM address
	task->tasktable = (taskTable*) LOG_TASK_SPACE_START;
	task->platform_flags = tt->platform;

	// unmap the new task from the current task virtual memory map
	theOS->getHatLayer()->unmap((void*) task_start);

	this->taskDatabase->addTail( task );
	task->myTaskDbItem = this->taskDatabase->getTail();
	LOG(KERNEL,INFO,(KERNEL,INFO,"TaskManager::loadTaskFromFile: running Task."));
	task->run();


	return cOk;

}

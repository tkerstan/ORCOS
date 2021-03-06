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

#include "EarliestDeadlineFirstThreadScheduler_TBS.hh"
#include "kernel/Kernel.hh"
#include "inc/const.hh"

extern Kernel* theOS;

/*****************************************************************************
 * Method: EarliestDeadlineFirstThreadScheduler_TBS::enter(LinkedListItem* item)
 *
 * @description
 *
 *---------------------------------------------------------------------------*/
ErrorT EarliestDeadlineFirstThreadScheduler_TBS::enter(LinkedListItem* item) {
    RealTimeThread* pRTThread = static_cast<RealTimeThread*>(item->getData());

    /* If a phase has been specified, put the thread to sleep for the specified duration. */
    if (pRTThread->phase > 0) {
        pRTThread->sleep(pRTThread->phase, item);
        pRTThread->phase = 0;
        return (cOk );
    }

    /* enter Thread in the database in accordance with it's priority */
    return (PriorityThreadScheduler::enter(item));
}


/*****************************************************************************
 * Method: EarliestDeadlineFirstThreadScheduler_TBS::getNextTimerEvent(LinkedList* sleepList, unint4 dt)
 *
 * @description
 *
 *---------------------------------------------------------------------------*/
unint4 EarliestDeadlineFirstThreadScheduler_TBS::getNextTimerEvent(LinkedList* sleepList, unint4 dt) {
    // it makes no sense to return an unint8 here, since theOS->getTimerDevice()->setTimer(nextevent) will take
    // a unint4 anyways (and the return value of this function is used to set the timer event).
    TimeT sleeptime = ((1 << sizeof(TimeT)) - 1);

    // only return a value smaller than sleeptime if there is some other competing threads inside the sleeplist!
    LinkedListItem* pDBSleepItem = sleepList->getHead();
    if (pDBSleepItem != 0) {
        LinkedListItem* pDBNextItem = database.getHead();

        // set variables which are needed to compare to later on, so we do not need to set these for every
        // iteration of the while loop
        TimeT nextPriority;

        if (pDBNextItem != 0) {
            nextPriority = (static_cast<RealTimeThread*>(pDBNextItem->getData()))->effectivePriority;
        } else {
            nextPriority = 0;
        }

        // this is the actual computation of the needed intervall. it compares the priority of the current
        // thread with the future prioritys of the threads in the sleeplist. The smallest time intervall where
        // the sleeping threads priority is higher than the current priority will be set as sleeptime.
        do {
            RealTimeThread* pSleepThread = static_cast<RealTimeThread*>(pDBSleepItem->getData());

            pSleepThread->sleepCycles -= dt;
            if (pSleepThread->sleepCycles <= 0) {
                pSleepThread->status.setBits(cReadyFlag);
                LinkedListItem* litem2 = pDBSleepItem;
                pDBSleepItem = pDBSleepItem->getSucc();

                this->enter(litem2);
            } else {
                if ((pSleepThread->getSleepTime() < sleeptime) && (pSleepThread->effectivePriority > nextPriority))
                    sleeptime = pSleepThread->getSleepTime();

                pDBSleepItem = pDBSleepItem->getSucc();
            }
        } while (pDBSleepItem != 0);
    }

    return (sleeptime);
}


/*
*   This file is part of Luma3DS
*   Copyright (C) 2016-2020 Aurora Wright, TuxSH
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
*       * Requiring preservation of specified reasonable legal notices or
*         author attributions in that material or in the Appropriate Legal
*         Notices displayed by works containing it.
*       * Prohibiting misrepresentation of the origin of that material,
*         or requiring that modified versions of such material be marked in
*         reasonable ways as different from the original version.
*/

#include "svc/PauseUnpauseProcess.h"
#include "synchronization.h"

Result PauseUnpauseProcess(Handle processHandle, bool pause)
{
    KProcessHandleTable *handleTable = handleTableOfProcess(currentCoreContext->objectContext.currentProcess);
    KProcess *process;
    // No point in not allowing this?
    if(processHandle == CUR_PROCESS_HANDLE)
    {
        process = currentCoreContext->objectContext.currentProcess;
        KAutoObject__AddReference((KAutoObject *)process);
    }
    else
        process = KProcessHandleTable__ToKProcess(handleTable, processHandle);

    if(process == NULL)
        return 0xD8E007F7; // invalid handle

    if (pause)
    {
        bool currentThreadsFound = false;

        KRecursiveLock__Lock(criticalSectionLock);
        for(KLinkedListNode *node = threadList->list.nodes.first; node != (KLinkedListNode *)&threadList->list.nodes; node = node->next)
        {
            KThread *thread = (KThread *)node->key;
            if(thread->ownerProcess != process || (thread->schedulingMask & 0xF) == 2)
                continue;
            if(thread == coreCtxs[thread->coreId].objectContext.currentThread)
                currentThreadsFound = true;
            else
            {
                u8 oldMask = thread->schedulingMask;
                thread->schedulingMask &= 0xF0;
                KSynchronizationObject__RegisterSyncWithThread((KSynchronizationObject*)process, thread);
                KScheduler__AdjustThread(currentCoreContext->objectContext.currentScheduler, thread, oldMask);
            }
        }

        if(currentThreadsFound)
        {
            for(KLinkedListNode *node = threadList->list.nodes.first; node != (KLinkedListNode *)&threadList->list.nodes; node = node->next)
            {
                KThread *thread = (KThread *)node->key;
                if(thread->ownerProcess != process)
                    continue;
                else
                {
                    u8 oldMask = thread->schedulingMask;
                    thread->schedulingMask &= 0xF0;
                    KSynchronizationObject__RegisterSyncWithThread((KSynchronizationObject*)process, thread);
                    KScheduler__AdjustThread(currentCoreContext->objectContext.currentScheduler, thread, oldMask);

                    KRecursiveLock__Lock(criticalSectionLock);
                    if(thread->coreId != getCurrentCoreID())
                    {
                        u32 cpsr = __get_cpsr();
                        __disable_irq();
                        coreCtxs[thread->coreId].objectContext.currentScheduler->triggerCrossCoreInterrupt = true;
                        currentCoreContext->objectContext.currentScheduler->triggerCrossCoreInterrupt = true;
                        __set_cpsr_cx(cpsr);
                    }
                    KRecursiveLock__Unlock(criticalSectionLock);
                }
            }
            KScheduler__TriggerCrossCoreInterrupt(currentCoreContext->objectContext.currentScheduler);
        }
        KRecursiveLock__Unlock(criticalSectionLock);
    }
    else
    {
        for (KLinkedListNode* list = ((KSynchronizationObject*)process)->syncedThreads.nodes.first; list != ((KSynchronizationObject*)process)->syncedThreads.nodes.last; list = list->next)
        {
            KThread* thread = (KThread*) list->key;
            // Cancel the synchronization for all non-terminated threads: it doesn't make sense to ever wait on your own process
            if (thread->ownerProcess == process && (thread->schedulingMask & 0x0F) != 2)
            {
                KSynchronizationObject__UnRegisterSyncWithThread((KSynchronizationObject*)process, (KThread*)list->key);
                u8 oldMask = thread->schedulingMask;
                thread->schedulingMask = (thread->schedulingMask & 0xF0) | 1;
                KScheduler__AdjustThread(currentCoreContext->objectContext.currentScheduler, thread, oldMask);
            }
        }
    }

    ((KAutoObject *)process)->vtable->DecrementReferenceCount((KAutoObject *)process);

    return 0;
}

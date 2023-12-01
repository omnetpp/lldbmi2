
#include <lldb/API/SBUnixSignals.h>
#include <chrono>
#include <thread>
#include <unistd.h>
#include "lldbmi2.h"
#include "log.h"
#include "events.h"
#include "frames.h"

extern LIMITS limits;


// wait thread
void* processListener(void* arg) {
    logprintf(LOG_TRACE, "processListener (0x%x)\n", arg);
    Lldbmi2* pstate = (Lldbmi2*)arg;
    SBProcess process = pstate->process;

    if (!process.IsValid())
        return NULL;

    if (!pstate->listener.IsValid())
        return NULL;

    while (!pstate->eof && !pstate->procstop) {
        SBEvent event;
        bool gotevent = pstate->listener.WaitForEvent(1, event);
        if (!gotevent || !event.IsValid())
            continue;
        uint32_t eventtype = event.GetType();
        if (SBProcess::EventIsProcessEvent(event)) {
            StateType processstate = process.GetState();
            switch (eventtype) {
            case SBProcess::eBroadcastBitStateChanged:
                logprintf(LOG_EVENTS | LOG_RAW, "eBroadcastBitStateChanged\n");
                switch (processstate) {
                case eStateRunning:
                    if (pstate->wanttokill) {
                        //	logprintf (LOG_INFO, "console kill: send SIGINT\n");
                        //	pstate->process.Signal(SIGINT);
                        logprintf(LOG_INFO, "console kill: terminateProcess\n");
                        pstate->terminateProcess(PRINT_GROUP | AND_EXIT);
                    }
                    logprintf(LOG_EVENTS, "eStateRunning\n");
                    break;
                case eStateExited:
                    logprintf(LOG_EVENTS, "eStateExited\n");
                    checkThreadsLife(pstate, process); // not useful. threads are not stopped before exit
                    pstate->terminateProcess(PRINT_GROUP);
                    // cdtprintf ("*stopped,reason=\"exited-normally\"\n(gdb)\n");
                    logprintf(LOG_INFO, "processlistener. eof=%d\n", pstate->eof);
                    break;
                case eStateStopped:
                    logprintf(LOG_EVENTS, "eStateStopped\n");
                    pstate->onStopped();
                    break;
                default:
                    logprintf(LOG_WARN, "unexpected process state %d\n", processstate);
                    break;
                }
                break;
            case SBProcess::eBroadcastBitInterrupt:
                logprintf(LOG_EVENTS, "eBroadcastBitInterrupt\n");
                break;
            case SBProcess::eBroadcastBitProfileData:
                logprintf(LOG_EVENTS, "eBroadcastBitProfileData\n");
                break;
            case SBProcess::eBroadcastBitSTDOUT:
            case SBProcess::eBroadcastBitSTDERR:
                // pass stdout and stderr from application to pty
                long iobytes;
                char iobuffer[LINE_MAX];
                if (eventtype == SBProcess::eBroadcastBitSTDOUT)
                    logprintf(LOG_EVENTS, "eBroadcastBitSTDOUT\n");
                else
                    logprintf(LOG_EVENTS, "eBroadcastBitSTDERR\n");
                iobytes = process.GetSTDOUT(iobuffer, sizeof(iobuffer));
                if (iobytes > 0) {
                    // remove \r
                    char *ps = iobuffer, *pd = iobuffer;
                    do {
                        if (*ps == '\r' && *(ps + 1) == '\n') {
                            ++ps;
                            --iobytes;
                        }
                        *pd++ = *ps++;
                    } while (*(ps - 1));
                    writelog((pstate->ptyfd != EOF) ? pstate->ptyfd : STDOUT_FILENO, iobuffer, iobytes);
                }
                logdata(LOG_PROG_IN, iobuffer, iobytes);
                break;
            default:
                logprintf(LOG_WARN, "unknown event type 0x%x\n", eventtype);
                break;
            }
        }
        if (SBWatchpoint::EventIsWatchpointEvent(event)) {
            printf("Is Watchpoint event\n");
        } else
            logprintf(LOG_EVENTS, "event type 0x%x\n", eventtype);
    }
    logprintf(LOG_EVENTS, "processlistener exited. pstate->eof=%d\n", pstate->eof);
    return NULL;
}

void checkThreadsLife(Lldbmi2* pstate, SBProcess process) {
    logprintf(LOG_TRACE, "checkThreadsLife (0x%x, 0x%x)\n", pstate, &process);
    if (!process.IsValid())
        return;
    SBThread thread;
    const size_t nthreads = process.GetNumThreads();
    int indexlist;
    bool stillalive[THREADS_MAX];
    for (indexlist = 0; indexlist < THREADS_MAX; indexlist++) // init live list
        stillalive[indexlist] = false;
    for (size_t indexthread = 0; indexthread < nthreads; indexthread++) {
        SBThread thread = process.GetThreadAtIndex(indexthread);
        if (thread.IsValid()) {
            int stopreason = thread.GetStopReason();
            int threadindexid = thread.GetIndexID();
            logprintf(LOG_NONE, "thread threadindexid=%d stopreason=%d\n", threadindexid, stopreason);
            for (indexlist = 0; indexlist < THREADS_MAX; indexlist++) {
                if (threadindexid == pstate->threadids[indexlist]) // existing thread
                    break;
            }
            if (indexlist < THREADS_MAX) // existing thread. mark as alive
                stillalive[indexlist] = true;
            else { // new thread. add to the thread list list
                for (indexlist = 0; indexlist < THREADS_MAX; indexlist++) {
                    if (pstate->threadids[indexlist] == 0) {
                        pstate->threadids[indexlist] = threadindexid;
                        stillalive[indexlist] = true;
                        cdtprintf("=thread-created,id=\"%d\",group-id=\"%s\"\n", threadindexid, pstate->threadgroup);
                        break;
                    }
                }
                if (indexlist >= THREADS_MAX)
                    logprintf(LOG_ERROR, "threads table too small (%d)\n", THREADS_MAX);
            }
        }
    }
    for (indexlist = 0; indexlist < THREADS_MAX; indexlist++) { // find finished threads
        if (pstate->threadids[indexlist] > 0 && !stillalive[indexlist]) {
            cdtprintf("=thread-exited,id=\"%d\",group-id=\"%s\"\n", pstate->threadids[indexlist], pstate->threadgroup);
            pstate->threadids[indexlist] = 0;
        }
    }
}

void updateSelectedThread(SBProcess process) {
    logprintf(LOG_TRACE, "updateSelectedThread (0x%x)\n", &process);
    if (!process.IsValid())
        return;
    SBThread currentThread = process.GetSelectedThread();
    const StopReason eCurrentThreadStoppedReason = currentThread.GetStopReason();
    if (!currentThread.IsValid() || (eCurrentThreadStoppedReason == eStopReasonInvalid) ||
        (eCurrentThreadStoppedReason == eStopReasonNone)) {
        // Prefer a thread that has just completed its plan over another thread as current thread
        SBThread planThread;
        SBThread otherThread;
        const size_t nthreads = process.GetNumThreads();
        for (size_t indexthread = 0; indexthread < nthreads; indexthread++) {
            //  GetThreadAtIndex() uses a base 0 index
            //  GetThreadByIndexID() uses a base 1 index
            SBThread thread = process.GetThreadAtIndex(indexthread);
            if (!thread.IsValid()) {
                logprintf(LOG_ERROR, "thread invalid in updateSelectedThread\n");
                return;
            }
            const StopReason eThreadStopReason = thread.GetStopReason();
            switch (eThreadStopReason) {
            case eStopReasonTrace:
            case eStopReasonBreakpoint:
            case eStopReasonWatchpoint:
            case eStopReasonSignal:
            case eStopReasonException:
                if (!otherThread.IsValid())
                    otherThread = thread;
                break;
            case eStopReasonPlanComplete:
                if (!planThread.IsValid())
                    planThread = thread;
                break;
            case eStopReasonInvalid:
            case eStopReasonNone:
            default:
                break;
            }
        }
        if (planThread.IsValid())
            process.SetSelectedThread(planThread);
        else if (otherThread.IsValid())
            process.SetSelectedThread(otherThread);
        else {
            SBThread thread;
            if (currentThread.IsValid())
                thread = currentThread;
            else
                thread = process.GetThreadAtIndex(0);
            if (thread.IsValid())
                process.SetSelectedThread(thread);
        }
    }
}

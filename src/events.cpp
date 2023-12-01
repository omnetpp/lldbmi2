
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
                    pstate->checkThreadsLife(); // not useful. threads are not stopped before exit
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

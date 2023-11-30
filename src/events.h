
#ifndef EVENTS_H
#define EVENTS_H

#define PRINT_THREAD 1
#define PRINT_GROUP 2
#define AND_EXIT 4

class Lldbmi2;
#include <lldb/API/SBProcess.h>

using namespace lldb;

int startProcessListener(Lldbmi2* pstate);
void waitProcessListener();
void* processListener(void* arg);
void onStopped(Lldbmi2* pstate, SBProcess process);
void checkThreadsLife(Lldbmi2* pstate, SBProcess process);
void updateSelectedThread(SBProcess process);

#endif // EVENTS_H

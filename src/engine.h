#ifndef ENGINE_H
#define ENGINE_H

#include "lldbmi2.h"

#define MAX_ARGS 200

typedef struct {
    int sequence;
    char arguments[LINE_MAX];
    int argc;
    const char* argv[MAX_ARGS];
    char threadgroup[NAME_MAX];
    int thread;
    int frame;
    int available;
    int all;
} CDT_COMMAND;
// the environment

int scanArgs(CDT_COMMAND* cdt_command);

#endif // ENGINE_H

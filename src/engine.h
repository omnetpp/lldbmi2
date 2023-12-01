#ifndef ENGINE_H
#define ENGINE_H

#include "lldbmi2.h"

#define MAX_ARGS 200

class CDT_COMMAND {

public:
    int sequence;
    char arguments[LINE_MAX];
    int argc;
    const char* argv[MAX_ARGS];
    char threadgroup[NAME_MAX];
    int thread;
    int frame;
    int available;
    int all;

    int scanArgs();
};


#endif // ENGINE_H

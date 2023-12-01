
#include <cstdio>
#include <cctype>
#include <cstring>
#include <cstdlib>

#include <fcntl.h>
#include <unistd.h>

#include "lldbmi2.h"
#include "strlxxx.h"
#include "log.h"
#include "engine.h"
#include "events.h"
#include "frames.h"
#include "variables.h"
#include "names.h"
#include "test.h"

extern LIMITS limits;


// convert argument line in a argv vector
// take care of "
int CDT_COMMAND::scanArgs() {
    logprintf(LOG_TRACE, "scanArgs (0x%x)\n", this);
    argc = 0;
    char *pa = arguments, *ps;
    while (*pa) {
        if (argc >= MAX_ARGS - 2) { // keep place for final NULL
            logprintf(LOG_ERROR, "arguments table too small (%d)\n", MAX_ARGS);
            break;
        }
        while (isspace(*pa))
            ++pa;
        if (*pa == '"') {
            int ndx = 0;
            ps = pa;
            while (*pa) {
                pa++;
                if (*pa == '\\' && *(pa + 1) == '"')
                    pa = pa + 2;
                if (*pa == '"')
                    pa++;
                ps[ndx++] = *pa;
            }
            ps[ndx] = '\0';
        } else {
            ps = pa;
            while (*pa && !isspace(*pa))
                ++pa;
            if (isspace(*pa))
                *pa++ = '\0';
        }
        argv[argc++] = ps;
    }
    argv[argc] = NULL;
    return argc;
}

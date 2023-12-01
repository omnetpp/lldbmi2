
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


// decode command line and fill the cc CDT_COMMAND structure
//   get sequence number
//   convert arguments line in a argv vector
//   decode optional (--option) arguments
int evalCDTCommand(Lldbmi2* pstate, const char* cdtcommand, CDT_COMMAND* cc) {
    logprintf(LOG_NONE, "evalCDTLine (0x%x, %s, 0x%x)\n", pstate, cdtcommand, cc);
    cc->sequence = 0;
    cc->argc = 0;
    cc->arguments[0] = '\0';
    if (cdtcommand[0] == '\0') // just ENTER
        return 0;
    // decode command with sequence number. ^\n should be ^\0
    int fields = sscanf(cdtcommand, "%d%[^\n]", &cc->sequence, cc->arguments);
    if (fields == 0) {
        // try decode command without sequence number. ^\n should be ^\0
        fields = sscanf(cdtcommand, "%[^\n]", cc->arguments);
        if (fields != 1)
            return 0;
        cc->sequence = 0;
    } else if (fields < 2) {
        logprintf(LOG_WARN, "invalid command format: ");
        logdata(LOG_NOHEADER, cdtcommand, strlen(cdtcommand));
        cdtprintf("%d^error,msg=\"%s\"\n(gdb)\n", cc->sequence, "invalid command format.");
        return 0;
    }

    cc->threadgroup[0] = '\0';
    cc->thread = cc->frame = cc->available = cc->all = -1;

    fields = scanArgs(cc);

    int field;
    for (field = 1; field < fields; field++) { // arg 0 is the command
        if (strcmp(cc->argv[field], "--thread-group") == 0) {
            strlcpy(cc->threadgroup, cc->argv[++field], sizeof(cc->threadgroup));
            strlcpy(pstate->threadgroup, cc->threadgroup, sizeof(pstate));
        } else if (strcmp(cc->argv[field], "--thread") == 0) {
            int actual_thread = cc->thread;
            sscanf(cc->argv[++field], "%d", &cc->thread);
            if (cc->thread != actual_thread && cc->thread >= 0)
                pstate->process.SetSelectedThreadByIndexID(cc->thread);
        } else if (strcmp(cc->argv[field], "--frame") == 0) {
            int actual_frame = cc->frame;
            sscanf(cc->argv[++field], "%d", &cc->frame);
            if (cc->frame != actual_frame && cc->frame >= 0) {
                SBThread thread = pstate->process.GetSelectedThread();
                if (thread.IsValid())
                    thread.SetSelectedFrame(cc->frame);
                else
                    cdtprintf("%d^error,msg=\"%s\"\n(gdb)\n", cc->sequence, "Can not select frame. thread is invalid.");
            }
        } else if (strcmp(cc->argv[field], "--available") == 0)
            cc->available = 1;
        else if (strcmp(cc->argv[field], "--all") == 0)
            cc->all = 1;
        else if (strncmp(cc->argv[field], "--", 2) == 0) {
            logprintf(LOG_WARN, "unexpected qualifier %s\n", cc->argv[field]);
            break;
        } else
            break;
    }

    return field;
}

// convert argument line in a argv vector
// take care of "
int scanArgs(CDT_COMMAND* cc) {
    logprintf(LOG_TRACE, "scanArgs (0x%x)\n", cc);
    cc->argc = 0;
    char *pa = cc->arguments, *ps;
    while (*pa) {
        if (cc->argc >= MAX_ARGS - 2) { // keep place for final NULL
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
        cc->argv[cc->argc++] = ps;
    }
    cc->argv[cc->argc] = NULL;
    return cc->argc;
}

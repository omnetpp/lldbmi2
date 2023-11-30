
#include "lldbmi2.h"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#ifdef __APPLE__
#include <util.h>
#endif
#ifdef UNLINKED
#include <dlfcn.h>
#endif

#include "engine.h"
#include "variables.h"
#include "log.h"
#include "test.h"
#include "version.h"

#include "events.h"

Lldbmi2::Lldbmi2()
{
    logprintf(LOG_TRACE, "Lldbmi2 ctor (0x%x)\n", this);
    SBDebugger::Initialize();
    debugger = SBDebugger::Create();
    debugger.SetAsync(true);
    listener = debugger.GetListener();
}

void Lldbmi2::help()
{
    std::cerr << lldbmi2Prompt <<
R"(Description:
   A MI2 interface to LLDB
Authors:
   Didier Bertrand, 2015, 2016, 2018
   Eduard Matveev, 2016
   David Jenkins, 2018
Syntax:
   lldbmi2 --version [options]
   lldbmi2 --interpreter mi2 [options]
Arguments:
   --version:           Return GDB's version (GDB 7.7.1) and exits.
   --interpreter mi2:   Standard mi2 interface.
   --interpreter=mi2:   Standard mi2 interface.
Options:
   --log:                Create log file in project root directory.
   --logmask mask:       Select log categories. 0xFFF. See source code for values.
   --arch arch_name:     Force a different architecture from host architecture: arm64, x86_64, i386
   --test n:             Execute test sequence (to debug lldmi2).
   --script file_path:   Execute test script or replay logfile (to debug lldmi2).
   --nx:                 Ignored.
)"
    << "   --frames frames:      Max number of frames to display (" << FRAMES_MAX << ")." << std::endl
    << "   --children children:  Max number of children to check for update (" << CHILDREN_MAX << ")." << std::endl
    << "   --walkdepth depth:    Max walk depth in search for variables (" << WALK_DEPTH_MAX << ")." << std::endl
    << "   --changedepth depth:  Max depth to check for updated variables (" << CHANGE_DEPTH_MAX << ")." << std::endl;
}

bool Lldbmi2::addEnvironment(const char* entrystring) {
    logprintf(LOG_NONE, "addEnvironment (0x%x, %s)\n", this, entrystring);
    size_t entrysize = strlen(entrystring);
    if (envpentries >= ENV_ENTRIES - 2) { // keep size for final NULL
        logprintf(LOG_ERROR, "addEnvironment: envp size (%d) too small\n", sizeof(envs));
        return false;
    }
    if (envspointer - envs + 1 + entrysize >= sizeof(envs)) {
        logprintf(LOG_ERROR, "addEnvironment: envs size (%d) too small\n", sizeof(envs));
        return false;
    }
    envp[envpentries++] = envspointer;
    envp[envpentries] = NULL;
    strcpy(envspointer, entrystring);
    envspointer += entrysize + 1;
    logprintf(LOG_ARGS | LOG_RAW, "envp[%d]=%s\n", envpentries - 1, envp[envpentries - 1]);
    return true;
}

Lldbmi2::~Lldbmi2() {
    logprintf(LOG_TRACE, "Lldbmi2 dtor\n");
    waitProcessListener();
    SBDebugger::Terminate();
}

LIMITS limits;
Lldbmi2* gpstate;

int main(int argc, char** argv, char** envp) {
#ifdef UNLINKED
    void* ret = dlopen("liblldb.so", RTLD_NOW | RTLD_GLOBAL);
    if (ret == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return EXIT_FAILURE;
    }
#endif

    gpstate = new Lldbmi2();

    char commandLine[BIG_LINE_MAX]; // data from cdt
    int isVersion = 0, isInterpreter = 0;
    int isLog = 0;
    unsigned int logmask = LOG_ALL;

    gpstate->ptyfd = EOF;
    gpstate->gdbPrompt = "GNU gdb (GDB) 7.7.1\n";
    snprintf(gpstate->lldbmi2Prompt, NAME_MAX, "lldbmi2 version %s\n", LLDBMI2_VERSION);
    gpstate->cdtbufferB.grow(BIG_LINE_MAX);

    limits.frames_max = FRAMES_MAX;
    limits.children_max = CHILDREN_MAX;
    limits.walk_depth_max = WALK_DEPTH_MAX;
    limits.change_depth_max = CHANGE_DEPTH_MAX;

    // get args
    for (int narg = 0; narg < argc; narg++) {
        logarg(argv[narg]);
        if (strcmp(argv[narg], "--version") == 0)
            isVersion = 1;
        else if (strcmp(argv[narg], "--interpreter") == 0) {
            isInterpreter = 1;
            if (++narg < argc)
                logarg(argv[narg]);
        } else if (strcmp(argv[narg], "--interpreter=mi2") == 0)
            isInterpreter = 1;
        else if ((strcmp(argv[narg], "-i") == 0) && (strcmp(argv[narg + 1], "mi") == 0))
            isInterpreter = 1;
        else if (strcmp(argv[narg], "--arch") == 0) {
            if (++narg < argc)
                strcpy(gpstate->arch, logarg(argv[narg]));
        } else if (strcmp(argv[narg], "--test") == 0) {
            limits.istest = true;
            if (++narg < argc)
                sscanf(logarg(argv[narg]), "%d", &(gpstate->test_sequence));
            if (gpstate->test_sequence)
                setTestSequence(gpstate->test_sequence);
        } else if (strcmp(argv[narg], "--script") == 0) {
            limits.istest = true;
            if (++narg < argc)
                strcpy(gpstate->test_script, logarg(argv[narg])); // no spaces allowed in the name
            if (gpstate->test_script[0])
                setTestScript(gpstate->test_script);
        } else if (strcmp(argv[narg], "--log") == 0)
            isLog = 1;
        else if (strcmp(argv[narg], "--logmask") == 0) {
            isLog = 1;
            if (++narg < argc)
                sscanf(logarg(argv[narg]), "%x", &logmask);
        } else if (strcmp(argv[narg], "--frames") == 0) {
            if (++narg < argc)
                sscanf(logarg(argv[narg]), "%d", &limits.frames_max);
        } else if (strcmp(argv[narg], "--children") == 0) {
            if (++narg < argc)
                sscanf(logarg(argv[narg]), "%d", &limits.children_max);
        } else if (strcmp(argv[narg], "--walkdepth") == 0) {
            if (++narg < argc)
                sscanf(logarg(argv[narg]), "%d", &limits.walk_depth_max);
        } else if (strcmp(argv[narg], "--changedepth") == 0) {
            if (++narg < argc)
                sscanf(logarg(argv[narg]), "%d", &limits.change_depth_max);
        }
    }

    // create a log filename from program name and open log file
    if (isLog) {
        if (limits.istest)
            setlogfile(gpstate->logfilename, sizeof(gpstate->logfilename), argv[0], "lldbmi2t.log");
        else
            setlogfile(gpstate->logfilename, sizeof(gpstate->logfilename), argv[0], "lldbmi2.log");
        openlogfile(gpstate->logfilename);
        setlogmask(logmask);
    }

    // log program args
    addlog("\n");
    logprintf(LOG_ARGS, NULL);

    gpstate->envp[0] = NULL;
    gpstate->envpentries = 0;
    gpstate->envspointer = gpstate->envs;
    const char* wl = "PWD="; // want to get eclipse project_loc if any
    int wll = strlen(wl);
    // copy environment for tested program
    for (int ienv = 0; envp[ienv]; ienv++) {
        gpstate->addEnvironment(envp[ienv]);
        if (strncmp(envp[ienv], wl, wll) == 0)
            strcpy(gpstate->project_loc, envp[ienv] + wll);
    }

    // return gdb version if --version
    if (isVersion) {
        writetocdt(gpstate->gdbPrompt);
        writetocdt(gpstate->lldbmi2Prompt);
        return EXIT_SUCCESS;
    }
    // check if --interpreter mi2
    else if (!isInterpreter) {
        gpstate->help();
        return EXIT_FAILURE;
    }

    signal(SIGINT, signalHandler);
    signal(SIGSTOP, signalHandler);

    cdtprintf("(gdb)\n");

    fd_set set;
    FD_ZERO(&set);
    // main loop
    while (!gpstate->eof) {
        logprintf(LOG_INFO, "main loop\n");
        // get inputs
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        // check command from CDT
        FD_SET(STDIN_FILENO, &set);
        if (gpstate->ptyfd != EOF) {
            // check data from Eclipse's console
            FD_SET(gpstate->ptyfd, &set);
            select(gpstate->ptyfd + 1, &set, NULL, NULL, &timeout);
        } else
            select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);

        if (FD_ISSET(STDIN_FILENO, &set) && !gpstate->eof && !limits.istest) {
            long chars = read(STDIN_FILENO, commandLine, sizeof(commandLine) - 1);
            if (chars > 0) {
                commandLine[chars] = '\0';
                while (fromCDT(gpstate, commandLine, sizeof(commandLine)) == MORE_DATA) {
                    commandLine[0] = '\0';
                }
            } else
                gpstate->eof = true;
        }

        if (gpstate->ptyfd != EOF && gpstate->isrunning) { // input from user to program
            if (FD_ISSET(gpstate->ptyfd, &set) && !gpstate->eof && !limits.istest) {
                char consoleLine[LINE_MAX]; // data from eclipse's console
                long chars = read(gpstate->ptyfd, consoleLine, sizeof(consoleLine) - 1);
                if (chars > 0) {
                    consoleLine[chars] = '\0';
                    SBProcess process = gpstate->process;
                    if (process.IsValid())
                        process.PutSTDIN(consoleLine, chars);
                }
            }
        }

        // execute test command if test mode
        if (!gpstate->eof && limits.istest && !gpstate->isrunning) {
            const char* testCommand = NULL;
            if ((testCommand = getTestCommand()) != NULL) {
                snprintf(commandLine, sizeof(commandLine), "%s\n", testCommand);
                fromCDT(gpstate, commandLine, sizeof(commandLine));
            }
        }
        // execute stacked commands if many command arrived once
        if (!gpstate->eof && gpstate->cdtbufferB.size() > 0) {
            commandLine[0] = '\0';
            while (fromCDT(gpstate, commandLine, sizeof(commandLine)) == MORE_DATA)
                ;
        }
    }

    if (gpstate->ptyfd != EOF)
        close(gpstate->ptyfd);

    logprintf(LOG_INFO, "main exit\n");
    closelogfile();
    delete gpstate;

    return EXIT_SUCCESS;
}

void Lldbmi2::setSignals()
{
    logprintf(LOG_TRACE, "setSignals (0x%x)\n", this);
    SBUnixSignals us = process.GetUnixSignals();
    if (!limits.istest || true) {
        const char* signame = "SIGINT";
        int signo = us.GetSignalNumberFromName(signame);
        logprintf(LOG_NONE, "signals before for %s (%d): suppress=%d, stop=%d, notify=%d\n", signame, signo,
                  us.GetShouldSuppress(signo), us.GetShouldStop(signo), us.GetShouldNotify(signo));
        us.SetShouldSuppress(signo, false); // !pass
        us.SetShouldStop(signo, false);
        us.SetShouldNotify(signo, true);
        logprintf(LOG_NONE, "signals after for %s (%d): suppress=%d, stop=%d, notify=%d\n", signame, signo,
                  us.GetShouldSuppress(signo), us.GetShouldStop(signo), us.GetShouldNotify(signo));
    }
}

void Lldbmi2::terminateProcess(int how)
{
    logprintf(LOG_TRACE, "terminateProcess (0x%x, 0x%x)\n", this, how);
    procstop = true;
    if (process.IsValid()) {
        SBThread thread = process.GetSelectedThread();
        int tid = thread.IsValid() ? thread.GetIndexID() : 0;
        if ((how & PRINT_THREAD))
            cdtprintf("=thread-exited,id=\"%d\",group-id=\"%s\"\n", tid, threadgroup);
        process.Destroy();
        //	pstate->process.Kill();
    } else
        logprintf(LOG_INFO, "pstate->process not valid\n");
    if ((how & PRINT_GROUP))
        cdtprintf("=thread-group-exited,id=\"%s\",exit-code=\"0\"\n", threadgroup);
    if ((how & AND_EXIT))
        eof = true;
}

// log an argument and return the argument
const char* logarg(const char* arg) {
    addlog(arg);
    addlog(" ");
    return arg;
}

void writetocdt(const char* line) {
    logprintf(LOG_NONE, "writetocdt (...)\n", line);
    logdata(LOG_CDT_OUT, line, strlen(line));
    writelog(STDOUT_FILENO, line, strlen(line));
}

void cdtprintf(const char* format, ...) {
    logprintf(LOG_NONE, "cdtprintf (...)\n");
    static StringB buffer(BIG_LINE_MAX);
    va_list args;

    if (format != NULL) {
        va_start(args, format);
        buffer.vosprintf(0, format, args);
        va_end(args);
        if ((buffer.c_str()[0] == '0') && (buffer.c_str()[1] == '^'))
            buffer.clear(1, 0);
        writetocdt(buffer.c_str());
    }
}

std::string ReplaceString(std::string subject, const std::string& search, const std::string& replace) {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos) {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
    }
    return subject;
}

void strrecprintf(const char* typestr, const char* format, va_list args) {
    logprintf(LOG_NONE, "srcprintf (...)\n");
    static StringB buffer(BIG_LINE_MAX);
    static StringB lineout(BIG_LINE_MAX);
    static StringB prepend(10);

    buffer.vosprintf(0, format, args);
    prepend.clear();
    prepend.append(typestr);
    prepend.append("\"");
    lineout.clear();
    lineout.append(prepend.c_str());
    for (int ndx = 0; ndx < buffer.size(); ndx++) {
        if (buffer.c_str()[ndx] == '\"')
            lineout.append("\\\"");
        else if (buffer.c_str()[ndx] == '\n') {
            lineout.append("\\n\"\n");
            writetocdt(lineout.c_str());
            lineout.clear();
            lineout.append(prepend.c_str());
        } else
            lineout.append(buffer.c_str()[ndx]);
    }
    if (lineout.size() > 2) {
        lineout.append("\n\"");
        writetocdt(lineout.c_str());
    }
}

void srcprintf(const char* format, ...) {
    va_list args;

    if (format != NULL) {
        va_start(args, format);
        strrecprintf("&", format, args);
        va_end(args);
    }
}

void srlprintf(const char* format, ...) {
    va_list args;

    if (format != NULL) {
        va_start(args, format);
        strrecprintf("~", format, args);
        va_end(args);
    }
}

static int signals_received = 0;

void signalHandler(int signo) {
    logprintf(LOG_TRACE, "signalHandler (%d)\n", signo);
    if (signo == SIGINT)
        logprintf(LOG_INFO, "signal SIGINT\n");
    else
        logprintf(LOG_INFO, "signal %s\n", signo);
    if (signo == SIGINT) {
        if (gpstate->process.IsValid() && signals_received == 0) {
            int selfPID = getpid();
            int processPID = gpstate->process.GetProcessID();
            logprintf(LOG_INFO, "signal_handler: signal SIGINT. self PID = %d, process pid = %d\n", selfPID,
                      processPID);
            logprintf(LOG_INFO, "send signal SIGSTOP to process %d\n", processPID);
            //	gpstate->process.Signal (SIGSTOP);
            logprintf(LOG_INFO, "Stop process\n");
            gpstate->process.Stop();
            //	++signals_received;
        } else
            gpstate->debugger.DispatchInputInterrupt();
    }
}

/*

BUTTON PAUSE (SIGSTOP or ^Z)

173728.927 ---  signal_handler: signal SIGINT. self PID = 15659, process pid = 15660
173728.927 ---  send signal SIGSTOP to process 15660
173728.930 ###  eStateStopped
173728.930 <<=  |=thread-created,id="2",group-id="i1"\n|
173728.931 <<=
|*stopped,reason="signal-received",signal-name="SIGSTOP",frame={addr="0x000000000001710a",func="__semwait_signal",args=[],file="libsystem_kernel.dylib"}thread-id="1",stopped-threads="all"\n(gdb)\n|
173728.939 >>=  |32thread|
173728.939 !!!  command not understood: 173728.939   |thread|
173728.940 <<=  |32^error,msg="Command unimplemented."\n(gdb)\n|

BUTTON STOP (SIGINT or ^C)

173504.979 <<<  |loop 0\n|
173505.222 ---  signal SIGINT
173505.222 ---  signal_handler: signal SIGINT. self PID = 15615, process pid = 15616
173505.222 ---  send signal SIGSTOP to process 15616
173505.223 >>=  |32-interpreter-exec --thread-group i1 console kill|
173505.223 ---  console kill: send SIGINT
173505.223 <<=  |32^done\n(gdb)\n|
173505.233 ###  eStateStopped
173505.233 <<=  |=thread-created,id="2",group-id="i1"\n|

005910.640 <<=
|*stopped,reason="breakpoint-hit",disp="keep",bkptno="1",frame={addr="0x000000000000127a",func="waitthread()",args=[],file="tests.cpp",fullname="/Users/didier/Projets/git-lldbmi2/lldbmi2/tests/src/tests.cpp",line="50"},thread-id="1",stopped-threads="all"\n(gdb)\n|
005910.722 <<=
|30^done,groups=[{id="i1",type="process",pid="20408",executable="/Users/didier/Projets/git-lldbmi2/lldbmi2/build/tests"}]\n(gdb)\n|

005610.309 <<=
|*stopped,reason="signal-received",signal-name="SIGSTOP",frame={addr="0x0000000000001286",func="waitthread()",args=[],file="tests.cpp",fullname="/Users/didier/Projets/git-lldbmi2/lldbmi2/tests/src/tests.cpp",line="50"},thread-id="1",stopped-threads="all"\n(gdb)\n|
005610.319 <<=
|30^done,groups=[{id="i1",type="process",pid="20359",executable="/Users/didier/Projets/git-lldbmi2/lldbmi2/build/tests"}]\n(gdb)\n|

*/

#include "signal_handling.h"
#include <signal.h>
#include <system_error>

static std::atomic<bool> signal_received_ = false;

#ifndef __WIN32
void signal_handler(int signum)
{
    //std::cerr << "signal " << signum << "\n";
    //std::cerr.flush();
    signal_received_ = true;
}

void setup_signal_handlers()
{
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    for (auto signum: { SIGINT, SIGHUP, SIGTERM })
    {
        if (sigaction(signum, &new_action, NULL) != 0)
            throw std::system_error(errno, std::generic_category(), "setup_signal_handlers");
    }
}
#else
#include <windows.h>
BOOL CtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
        printf("\n\nCTRL-C pressed, exiting.\n\n");
        signal_received_ = true;
        return (TRUE);
    default:
        return (FALSE);
    }
}

void setup_signal_handlers()
{
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE))
    {
        throw std::runtime_error("Error setting Console-Ctrl Handler\n");
    }
}
#endif

bool signal_received()
{
    return signal_received_;
}

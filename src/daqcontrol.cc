#include "daqcontrol.h"

DAQControl::DAQControl(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_context(context)
{
}

void DAQControl::startDAQ(u32 nCycles, bool keepHistoContents)
{
    if (m_context->getDAQState() != DAQState::Idle)
        return;

    if (m_context->getMode() == GlobalMode::DAQ)
    {
        m_context->startDAQReadout(nCycles, keepHistoContents);
    }
    else if (m_context->getMode() == GlobalMode::ListFile)
    {
        m_context->startDAQReplay(nCycles, keepHistoContents);
    }
}

void DAQControl::pauseDAQ()
{
    if (m_context->getDAQState() != DAQState::Running)
        return;

    m_context->pauseDAQ();
}

void DAQControl::resumeDAQ(u32 nCycles)
{
    if (m_context->getDAQState() != DAQState::Paused)
        return;

    m_context->resumeDAQ(nCycles);
}

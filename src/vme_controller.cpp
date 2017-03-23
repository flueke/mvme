#include "vme_controller.h"
#include <QThread>

static const QMap<VMEError::ErrorType, QString> errorNames = 
{
    { VMEError::NoError,        QSL("No error") },
    { VMEError::UnknownError,   QSL("Unknown error") },
    { VMEError::NotOpen,        QSL("Controller not open") },
    { VMEError::WriteError,     QSL("Write error") },
    { VMEError::ReadError,      QSL("Read error") },
    { VMEError::CommError,      QSL("Communication error") },
    { VMEError::BusError,       QSL("VME Bus Error") },
    { VMEError::NoDevice,       QSL("No device found") },
    { VMEError::DeviceIsOpen,   QSL("Device is open") },
    { VMEError::Timeout,        QSL("Timeout") },
};

QString VMEError::toString() const
{
    if (error() == VMEError::UnknownError && !m_message.isEmpty())
        return m_message;

    QString result(errorName());
    if (!m_message.isEmpty())
        result += QSL(": ") + m_message;
    return result;
}

QString VMEError::errorName() const
{
    return VMEError::errorName(error());
}

QString VMEError::errorName(ErrorType type)
{
    return errorNames.value(type, QSL("Unknown error"));
}

VMEController::VMEController(QObject *parent)
    : QObject(parent)
{}

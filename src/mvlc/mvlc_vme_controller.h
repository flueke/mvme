#ifndef __MVME_MVLC_VME_CONTROLLER_H__
#define __MVME_MVLC_VME_CONTROLLER_H__

#include <QTimer>
#include "vme_controller.h"
#include "mvlc/mvlc_qt_object.h"

// Implementation of the VMEController interface for the MVLC.

namespace mesytec
{
namespace mvlc
{

class LIBMVME_MVLC_EXPORT MVLC_VMEController: public VMEController
{
    Q_OBJECT
    signals:
        void stackErrorNotification(const QVector<u32> &notification);

    public:
        MVLC_VMEController(MVLCObject *mvlc, QObject *parent = nullptr);

        //
        // VMEController implementation
        //
        bool isOpen() const override;
        VMEError open() override;
        VMEError close() override;
        ControllerState getState() const override;
        QString getIdentifyingString() const override;
        VMEControllerType getType() const override;

        VMEError write32(u32 address, u32 value, u8 amod) override;
        VMEError write16(u32 address, u16 value, u8 amod) override;

        VMEError read32(u32 address, u32 *value, u8 amod) override;
        VMEError read16(u32 address, u16 *value, u8 amod) override;

        VMEError blockRead(u32 address, u32 transfers,
                           QVector<u32> *dest, u8 amod, bool fifo) override;

        //
        // MVLC specific methods
        //
        MVLCObject *getMVLCObject() { return m_mvlc; }

        void enableNotificationPolling() { m_notificationPoller.enablePolling(); }
        void disableNotificationPolling() { m_notificationPoller.disablePolling(); }

    private slots:
        void onMVLCStateChanged(const MVLCObject::State &oldState,
                                const MVLCObject::State &newState);

    private:
        MVLCObject *m_mvlc;
        MVLCNotificationPoller m_notificationPoller;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_VME_CONTROLLER_H__ */

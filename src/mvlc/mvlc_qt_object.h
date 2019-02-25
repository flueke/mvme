#ifndef __MVLC_QT_OBJECT_H__
#define __MVLC_QT_OBJECT_H__

#include <memory>
#include <mutex>
#include <QObject>

#include "mvlc/mvlc_abstract_impl.h"

namespace mesytec
{
namespace mvlc
{

class MVLCObject: public QObject
{
    Q_OBJECT
    public:
        enum State
        {
            Disconnected,
            Connecting,
            Connected,
        };

    signals:
        void stateChanged(const State &oldState, const State &newState);
        void errorSignal(const std::error_code &ec);

    public:
        MVLCObject(std::unique_ptr<AbstractImpl> impl, QObject *parent = nullptr);
        virtual ~MVLCObject();

        AbstractImpl &getImpl();
        bool isConnected() const { return m_state == Connected; }


    public slots:
        std::error_code connect();
        std::error_code disconnect();

    private:
        void setState(const State &newState);

        std::unique_ptr<AbstractImpl> m_impl;
        //std::atomic<State> m_state;
        State m_state;
        std::mutex m_cmdMutex;
        std::mutex m_dataMutex;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_QT_OBJECT_H__ */

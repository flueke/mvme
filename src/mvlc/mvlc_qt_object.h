#ifndef __MVLC_QT_OBJECT_H__
#define __MVLC_QT_OBJECT_H__

#include <memory>
#include <mutex>
#include <QObject>
#include <QVector>

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

    public:
        MVLCObject(std::unique_ptr<AbstractImpl> impl, QObject *parent = nullptr);
        virtual ~MVLCObject();

        bool isConnected() const;
        State getState() const { return m_state; }

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred);

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred);

        std::pair<std::error_code, size_t> write(Pipe pipe, const QVector<u32> &buffer);

        AbstractImpl *getImpl();

        void setReadTimeout(Pipe pipe, unsigned ms);
        void setWriteTimeout(Pipe pipe, unsigned ms);
        unsigned getReadTimeout(Pipe pipe) const;
        unsigned getWriteTimeout(Pipe pipe) const;

    public slots:
        std::error_code connect();
        std::error_code disconnect();

    private:
        void setState(const State &newState);

        std::unique_ptr<AbstractImpl> m_impl;
        State m_state;
        std::mutex m_cmdMutex;
        std::mutex m_dataMutex;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_QT_OBJECT_H__ */

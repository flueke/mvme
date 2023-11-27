#ifndef C4787F9C_70E5_4D8A_9CCF_E80BDC949D09
#define C4787F9C_70E5_4D8A_9CCF_E80BDC949D09

#include <QHash>
#include <QObject>
#include <QString>
#include <mutex>
#include <unordered_map>

class LogHandler: public QObject
{
    Q_OBJECT
    signals:
        void messageLogged(const QString &msg, const QString &category);

    public:
        using QObject::QObject;
        ~LogHandler() override;

    public slots:
        void logMessage(const QString &msg, const QString &category = {})
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                logBuffers_[category].push_back(msg);
            }

            emit messageLogged(msg, category);
        }

        QStringList getCategories()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return logBuffers_.keys();
        }

        QStringList getBuffer(const QString &category)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return logBuffers_.value(category);
        }

    private:
        std::mutex mutex_;
        QHash<QString, QStringList> logBuffers_;
};


#endif /* C4787F9C_70E5_4D8A_9CCF_E80BDC949D09 */

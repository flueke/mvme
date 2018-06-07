#include "util/leaky_bucket.h"
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QSpinBox>
#include <QPushButton>

int main(int argc, char *argv[])
{
    LeakyBucketMeter bucket(2, std::chrono::seconds(1));

    size_t suppressed = 0;
    size_t messageNum = 0;

    while (true)
    {
        if (!bucket.eventOverflows())
        {
            qDebug() << QDateTime::currentDateTime() << "message number" << messageNum
                     << "suppressed =" << suppressed << ", overflow =" << bucket.overflow();
            suppressed = 0;
            QThread::msleep(250);
        }
        else
        {
            qDebug() << "message number" << messageNum << "got suppressed, overflow =" << bucket.overflow();
            suppressed++;
            QThread::msleep(1000);
        }

        messageNum++;

        if (messageNum > 10)
            break;
    }

    return 0;
}

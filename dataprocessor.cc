#include "dataprocessor.h"
#include <QDebug>
#include <QThread>

DataProcessor::DataProcessor(QObject *parent)
    : QObject(parent)
{
}

void DataProcessor::processBuffer(DataBuffer *buffer)
{
    qDebug() << __PRETTY_FUNCTION__ << QThread::currentThread();
    qDebug() << "received buffer of size" << buffer->used;
    qDebug() << "emitting bufferProcessed";
    emit bufferProcessed(buffer);
}

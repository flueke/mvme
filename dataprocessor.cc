#include "dataprocessor.h"
#include "databuffer.h"
#include "mvme_context.h"
#include "vmusb.h"
#include <QDebug>
#include <QThread>

using namespace VMUSBConstants;

struct DataProcessorPrivate
{
    MVMEContext *context;
};

DataProcessor::DataProcessor(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_d(new DataProcessorPrivate)
{
    m_d->context = context;
}

void DataProcessor::processBuffer(DataBuffer *buffer)
{
    //auto vmusb = dynamic_cast<VMUSB *>(m_d->context->getController());
    //BufferIterator iter(buffer->data, buffer->size, vmusb->getMode() &&

    emit bufferProcessed(buffer);
}

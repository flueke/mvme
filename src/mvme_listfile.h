#ifndef UUID_c25729bd_96ba_4b27_9d14_f53626039101
#define UUID_c25729bd_96ba_4b27_9d14_f53626039101

/*
 * ===== MVME Listfile format =====

 * Section Header:
 *  Type
 *  Size
 *  Type specific info
 *
 * Header Types:
 * Config
 * Event
 *
 */

/*
 *  ------- Section Header ----------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |ttt         eeeessssssssssssssss|
 * +--------------------------------+
 *
 * t =  3 bit section type
 * e =  4 bit event type (== event number/index) for event sections
 * s = 16 bit size in units of 32 bit words (fillwords added to data if needed) -> 256k section max size
 *
 * Section size is the number of following 32 bit words not including the header word itself.

 * Sections with SectionType_Event contain subevents with the following header:

 *  ------- Subevent Header --------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |              mmmmmm  ssssssssss|
 * +--------------------------------+
 *
 * m =  6 bit module type (VMEModuleType enum from globals.h)
 * s = 10 bit size in units of 32 bit words
 *
 * The last word of each event section is the EndMarker (globals.h)
 *
*/

#include "globals.h"
#include "databuffer.h"
#include "util.h"

#include <QTextStream>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>

namespace listfile
{
    enum SectionType
    {
        /* The config section contains the mvmecfg as a json string padded with
         * spaces to the next 32 bit boundary. If the config data size exceeds
         * the maximum section size multiple config sections will be written at
         * the start of the file. */
        SectionType_Config = 0,
        SectionType_Event  = 1,
        SectionType_End    = 2,

        SectionType_Max    = 7
    };

    static const int SectionMaxWords  = 0xffff;
    static const int SectionMaxSize   = SectionMaxWords * sizeof(u32);

    static const int SectionTypeMask  = 0xe0000000; // 3 bit section type
    static const int SectionTypeShift = 29;
    static const int SectionSizeMask  = 0xffff;    // 16 bit section size in 32 bit words
    static const int SectionSizeShift = 0;
    static const int EventTypeMask  = 0xf0000;   // 4 bit event type
    static const int EventTypeShift = 16;

    // Subevent containing module data
    static const int ModuleTypeMask  = 0x3f000; // 6 bit module type
    static const int ModuleTypeShift = 12;

    static const int SubEventMaxWords  = 0x3ff;
    static const int SubEventMaxSize   = SubEventMaxWords * sizeof(u32);
    static const int SubEventSizeMask  = 0x3ff; // 10 bit subevent size in 32 bit words
    static const int SubEventSizeShift = 0;
}

void dump_mvme_buffer(QTextStream &out, const DataBuffer *eventBuffer, bool dumpData=false);

class DAQConfig;

class ListFile
{
    public:
        ListFile(const QString &fileName);
        bool open();
        QJsonObject getDAQConfig();
        bool seek(qint64 pos);
        bool readNextSection(DataBuffer *buffer);
        s32 readSectionsIntoBuffer(DataBuffer *buffer);
        const QFile &getFile() const { return m_file; }
        qint64 size() const { return m_file.size(); }
        QString getFileName() const { return m_file.fileName(); }

    private:
        QFile m_file;
        QJsonDocument m_configJson;
};

class ListFileReader: public QObject
{
    Q_OBJECT
    signals:
        void stateChanged(DAQState);
        void mvmeEventBufferReady(DataBuffer *);
        void logMessage(const QString &);
        void replayStopped();
        void progressChanged(qint64, qint64);

    public:
        ListFileReader(DAQStats &stats, QObject *parent = 0);
        ~ListFileReader();
        void setListFile(ListFile *listFile);
        ListFile *getListFile() const { return m_listFile; }

    public slots:
        void startFromBeginning();
        void readNextBuffer();
        void stopReplay();

    private:
        DAQStats &m_stats;
        DataBuffer *m_buffer;
        ListFile *m_listFile = 0;
        qint64 m_bytesRead = 0;
        qint64 m_totalBytes = 0;
        bool m_stopped = false;
};

class ListFileWriter: public QObject
{
    Q_OBJECT
    public:
        explicit ListFileWriter(QObject *parent = 0);
        ListFileWriter(QIODevice *outputDevice, QObject *parent = 0);

        void setOutputDevice(QIODevice *device);
        QIODevice *outputDevice() const { return m_out; }
        u64 bytesWritten() const { return m_bytesWritten; }

        bool writeConfig(QByteArray contents);
        bool writeBuffer(const char *buffer, size_t size);
        bool writeEndSection();

    private:
        QIODevice *m_out = nullptr;
        u64 m_bytesWritten = 0;
};

namespace listfile
{
    // This is currently broken: on replay events are lost.
    // invalid mod value for  "event0" "mqdc" 1
    // listfile::SubEvent listfile::Section::operator[](int) index= 1 , subEventHeader >= onePastEnd
#if 0
    struct ModuleValue
    {
        ModuleValue()
            : valid(false)
        {}

        ModuleValue(u32 dataWord)
            : dataWord(dataWord)
            , valid(true)
        {}

        bool isValid() const { return valid; }

        u32 getAddress()
        {
            int address = (dataWord & 0x003F0000) >> 16;
            return address;
        }

        u32 getValue()
        {
            u32 value = (dataWord & 0x00001FFF); // FIXME: data width depends on module type and configuration
            return value;
        }


        u32 dataWord = 0;
        bool valid = false;
    };

    struct SubEvent
    {
        SubEvent()
        {}

        SubEvent(u32 *subEventHeader, u32 *onePastEndOfBuffer)
            : subEventHeader(subEventHeader)
        {
            if (subEventHeader + size() + 2 < onePastEndOfBuffer)
                onePastEnd = subEventHeader + size() + 2;
            else
                onePastEnd = subEventHeader;
        }

        SubEvent(u32 *subEventHeader_, size_t wordsInBuffer)
            : SubEvent(subEventHeader_, subEventHeader_ + wordsInBuffer + 1) // FIXME: last buffer missing
        {}

        int size()
        {
            u32 sectionSize = (*subEventHeader & SubEventSizeMask) >> SubEventSizeShift;
            return sectionSize;
        }

        ModuleValue getValueByIndex(int index)
        {
            // add 2 to skip the subEventHeader and the modules header word
            // XXX: mesytec specific
            if (isValid() && (subEventHeader + index + 2 < onePastEnd))
                return ModuleValue(*(subEventHeader + index + 2));

            return ModuleValue();
        }

        // XXX: mesytec specific
        ModuleValue getValueByAddress(int address)
        {
            if (isValid())
            {
                u32 *currentWord = subEventHeader + 2; // skip to first data word
                const u32 *onePastLastWord = onePastEnd - 1; // skip the EOE word

                for (; currentWord < onePastLastWord; ++currentWord)
                {
                    bool isDataWord = ((*currentWord & 0xF0000000) == 0x10000000) // MDPP
                        || ((*currentWord & 0xFF800000) == 0x04000000); // MxDC

                    if (isDataWord)
                    {
                        int currentAddress = (*currentWord & 0x003F0000) >> 16;
                        if (currentAddress == address)
                            return ModuleValue(*currentWord);
                    }
                }
            }

            return ModuleValue();
        }

        ModuleValue operator[](int address)
        {
            return getValueByAddress(address);
        }

        bool isValid()
        {
            return subEventHeader < onePastEnd;
        }

        u32 *subEventHeader = 0;
        u32 *onePastEnd = 0;
    };

    struct Section
    {
        Section()
        {}

        Section(u32 *sectionHeader_, u32 *onePastEndOfBuffer)
            : sectionHeader(sectionHeader_)
        {
            int sz = size();
            u32 *end = sectionHeader + sz + 2;

            if(end < onePastEndOfBuffer)
                onePastEnd = end;
            else
                onePastEnd = sectionHeader;
        }

        Section(u32 *sectionHeader_, size_t wordsInBuffer)
            : Section(sectionHeader_, sectionHeader_ + wordsInBuffer + 1)
        {}

        int size()
        {
            u32 sectionSize = (*sectionHeader & SectionSizeMask) >> SectionSizeShift;
            return sectionSize;
        }

        int type()
        {
            int sectionType = (*sectionHeader & SectionTypeMask) >> SectionTypeShift;
            return sectionType;
        }

        bool isValid()
        {
            return sectionHeader < onePastEnd;
        }

        // get SubEvent by index
        SubEvent operator[](int index)
        {
            if (!isValid() || type() != SectionType_Event)
            {
                qDebug() << __PRETTY_FUNCTION__ << "not valid or not event";
                return SubEvent();
            }

            int currentIndex = 0;
            u32 *subEventHeader = sectionHeader + 1;

            while (currentIndex < index)
            {
                u32 subEventSize = (*subEventHeader & SubEventSizeMask) >> SubEventSizeShift;
                subEventHeader += subEventSize + 1;
                if (subEventHeader >= onePastEnd)
                {
                    qDebug() << __PRETTY_FUNCTION__ << "index=" << index << ", subEventHeader >= onePastEnd";
                    return SubEvent();
                }
            }
            return SubEvent(subEventHeader, onePastEnd);
        }

        u32 *sectionHeader = 0;
        u32 *onePastEnd = 0;
    };
#endif
}

#endif

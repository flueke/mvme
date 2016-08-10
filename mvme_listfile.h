#ifndef UUID_c25729bd_96ba_4b27_9d14_f53626039101
#define UUID_c25729bd_96ba_4b27_9d14_f53626039101

/*
 * ===== MVME Listfile format =====
 *
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
 * e =  4 bit event type for event sections
 * s = 16 bit size in units of 32 bit words (fillwords added to data if needed) -> 256k section max size

 * Sections with SectionType_Event contain subevent headers:

 *  ------- Subevent Header --------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |              mmmmmm  ssssssssss|
 * +--------------------------------+
*/

#include "databuffer.h"
#include "util.h"

#include <QTextStream>
#include <QFile>
#include <QJsonDocument>

namespace listfile
{
    enum SectionType
    {
        /* The config section contains the mvmecfg as a json string padded with
         * zeros to the next 32 bit boundary. If the config data size exceeds
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

    static const int SubEventSizeMask  = 0x3ff; // 10 bit subevent size in 32 bit words
    static const int SubEventSizeShift = 0;
}

void dump_event_buffer(QTextStream &out, const DataBuffer *eventBuffer);

class DAQConfig;

class ListFile
{
    public:
        ListFile(const QString &fileName);
        bool open();
        DAQConfig *getConfig();
        bool seek(qint64 pos);
        bool readNextSection(DataBuffer *buffer);
        s32 readSectionsIntoBuffer(DataBuffer *buffer);

    private:
        QFile m_file;
        QJsonDocument m_configJson;
};

class ListFileWorker: public QObject
{
    Q_OBJECT
    signals:
        void mvmeEventBufferReady(DataBuffer *);
        void logMessage(const QString &);

    public:
        ListFileWorker(QObject *parent = 0);
        ~ListFileWorker();
        void setListFile(ListFile *listFile);

    public slots:
        void readNextBuffer();
        //void start();
        //void addFreeBuffer(DataBuffer *buffer);

    private:
        DataBuffer *m_buffer;
        ListFile *m_listFile = 0;
};

#endif

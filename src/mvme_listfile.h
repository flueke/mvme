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
 * End
 *
 */


#include "globals.h"
#include "databuffer.h"
#include "util.h"
#include "threading.h"

#include <QTextStream>
#include <QFile>
#include <QJsonObject>
#include <QDebug>

namespace ListfileSections
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
};

/*  ===== VERSION 0 =====
 *
 *  ------- Section (Event) Header ----------
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

 *  ------- Subevent (Module) Header --------
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
struct listfile_v0
{
    static const int Version = 0;
    static const int FirstSectionOffset = 0;

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
};

/*  ===== VERSION 1 =====
 *
 * Differences to version 0:
 * - Starts with the FourCC "MVME" followed by a 32 bit word containing the
 *   listfile version number.
 * - Larger section and subevent sizes: 16 -> 20 bits for sections and 10 -> 20
 *   bits for subevents.
 * - Module type is now 8 bit instead of 6.
 *
 *  ------- Section (Event) Header ----------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |ttteeee     ssssssssssssssssssss|
 * +--------------------------------+
 *
 * t =  3 bit section type
 * e =  4 bit event type (== event number/index) for event sections
 * s = 16 bit size in units of 32 bit words (fillwords added to data if needed) -> 256k section max size
 *
 * Section size is the number of following 32 bit words not including the header word itself.

 * Sections with SectionType_Event contain subevents with the following header:

 *  ------- Subevent (Module) Header --------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |mmmmmmmm    ssssssssssssssssssss|
 * +--------------------------------+
 *
 * m =  8 bit module type (VMEModuleType enum from globals.h)
 * s = 10 bit size in units of 32 bit words
 *
 * The last word of each event section is the EndMarker (globals.h)
 *
*/
struct listfile_v1
{
    static const int Version = 1;
    constexpr static char * const FourCC = "MVME";

    static const int FirstSectionOffset = 8;

    static const int SectionMaxWords  = 0xfffff;
    static const int SectionMaxSize   = SectionMaxWords * sizeof(u32);

    static const int SectionTypeMask  = 0xe0000000; // 3 bit section type
    static const int SectionTypeShift = 29;
    static const int SectionSizeMask  = 0x000fffff; // 20 bit section size in 32 bit words
    static const int SectionSizeShift = 0;
    static const int EventTypeMask    = 0x1e000000; // 4 bit event type
    static const int EventTypeShift   = 25;

    // Subevent containing module data
    static const int ModuleTypeMask  = 0xff000000;  // 8 bit module type
    static const int ModuleTypeShift = 24;

    static const int SubEventMaxWords  = 0xfffff;
    static const int SubEventMaxSize   = SubEventMaxWords * sizeof(u32);
    static const int SubEventSizeMask  = 0x000fffff; // 20 bit subevent size in 32 bit words
    static const int SubEventSizeShift = 0;
};

void dump_mvme_buffer_v0(QTextStream &out, const DataBuffer *eventBuffer, bool dumpData=false);
void dump_mvme_buffer(QTextStream &out, const DataBuffer *eventBuffer, bool dumpData=false);

class DAQConfig;
class QuaZipFile;

class ListFile
{
    public:
        ListFile(const QString &fileName);
        ListFile(QuaZipFile *inFile);
        ~ListFile();

        bool open();
        QJsonObject getDAQConfig();
        bool seekToFirstSection();
        bool readNextSection(DataBuffer *buffer);
        s32 readSectionsIntoBuffer(DataBuffer *buffer);
        const QIODevice *getInputDevice() const { return m_input; }
        qint64 size() const;
        QString getFileName() const;
        u32 getFileVersion() const { return m_fileVersion; }

    private:
        bool seek(qint64 pos);

        QIODevice *m_input = nullptr;
        QJsonObject m_configJson;
        u32 m_fileVersion = 0;
        u32 m_sectionHeaderBuffer = 0;
};

class ListFileReader: public QObject
{
    Q_OBJECT
    signals:
        void stateChanged(DAQState);
        void logMessage(const QString &);
        void replayStopped();
        void progressChanged(qint64, qint64);

    public:
        ListFileReader(DAQStats &stats, QObject *parent = 0);
        ~ListFileReader();
        void setListFile(ListFile *listFile);
        ListFile *getListFile() const { return m_listFile; }

        bool isRunning() const { return m_state != DAQState::Idle; }

        void setEventsToRead(u32 eventsToRead);

        ThreadSafeDataBufferQueue *m_freeBufferQueue = nullptr;
        ThreadSafeDataBufferQueue *m_filledBufferQueue = nullptr;

    public slots:
        void start();
        void stop();
        void pause();
        void resume();

    private:
        void mainLoop();
        void setState(DAQState state);

        DAQStats &m_stats;

        DAQState m_state = DAQState::Idle;
        DAQState m_desiredState = DAQState::Idle;

        ListFile *m_listFile = 0;

        qint64 m_bytesRead = 0;
        qint64 m_totalBytes = 0;

        u32 m_eventsToRead = 0;
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

        bool writePreamble();
        bool writeConfig(QByteArray contents);
        bool writeBuffer(const char *buffer, size_t size);
        bool writeEndSection();

    private:
        QIODevice *m_out = nullptr;
        u64 m_bytesWritten = 0;
};

#endif

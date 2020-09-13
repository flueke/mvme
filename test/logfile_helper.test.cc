#include <gtest/gtest.h>
#include "logfile_helper.h"
#include <QDebug>
#include <thread>

using namespace mesytec::mvme;

namespace
{

static const QString logDirName = "logfile_helper.test.dir";

QString read_file(const QString &filepath)
{
    QFile f(filepath);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    auto bytes = f.readAll();
    return QString::fromUtf8(bytes);
}

QStringList list_logfiles()
{
    QDir logDir(logDirName);
    return logDir.entryList({"*.log"}, QDir::Files, QDir::Time | QDir::Reversed);
}

}

class LogfileHelperTestFixture: public ::testing::Test
{
    protected:
        LogfileHelperTestFixture()
        {
            // Create the logfile directory

            QDir dir;

            if (!dir.exists(logDirName))
            {
                if (!dir.mkdir(logDirName))
                    throw std::runtime_error("Cannot create LogfileHelper test directory");

                qDebug() << "created LogFileHelper test directory" << dir.filePath(logDirName);
            }
        }

        ~LogfileHelperTestFixture()
        {
            // Remove all files from the log directory, then rmdir the directory itself

            QDir dir(logDirName);

            auto filenames = list_logfiles();

            for (const auto &filename: filenames)
            {
                auto absfile = dir.absoluteFilePath(filename);

                qDebug() << __PRETTY_FUNCTION__ << "removing logfile" << absfile;

                if (!QFile::remove(absfile))
                {
                    qDebug() << "Could not remove logfile" << absfile;
                    std::abort();
                }
            }

            dir.cdUp();

            if (!dir.rmdir(logDirName))
            {
                qDebug() << "Could not remove LogfileHelper test directory";
                std::abort();
            }
        }
};

// Tests the case where the log directory does not exist (or a logfile cannot
// be created for some other reason).
TEST(LogFileHelperTestNoFixture, FileCreationFails)
{
    ASSERT_TRUE(!QDir().exists(logDirName));

    LogfileHelper lf(logDirName, 10);

    ASSERT_FALSE(lf.logMessage("theMessage"));
    ASSERT_FALSE(lf.beginNewFile("thePrefix"));
    ASSERT_FALSE(lf.logMessage("theMessage"));
    ASSERT_FALSE(lf.flush());
    ASSERT_FALSE(lf.closeCurrentFile());
}

TEST_F(LogfileHelperTestFixture, BeginNewLogfile)
{
    LogfileHelper lf(logDirName, 10);

    ASSERT_FALSE(lf.logMessage("foobar"));
    ASSERT_FALSE(lf.hasOpenFile());

    lf.beginNewFile("thePrefix");

    ASSERT_TRUE(lf.hasOpenFile());
    ASSERT_TRUE(lf.logMessage("foobar"));
    ASSERT_TRUE(lf.flush());
    ASSERT_EQ(read_file(lf.currentAbsFilepath()), QString("foobar"));
    ASSERT_TRUE(lf.closeCurrentFile());
    ASSERT_FALSE(lf.logMessage("foobar"));
}

TEST_F(LogfileHelperTestFixture, ExceedMaxFiles)
{
    // Note: the sleeps are in here to make sure the files have unique
    // timestamps and thus the time based sorting in LogfileHelper and in here
    // yields predictable results.

    {
        const unsigned MaxFiles = 10;

        LogfileHelper lf(logDirName, MaxFiles);

        for (unsigned i = 0; i < MaxFiles; i++)
        {
            ASSERT_TRUE(lf.beginNewFile("logfile" + QString::number(i)));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        ASSERT_EQ(static_cast<unsigned>(list_logfiles().size()), MaxFiles);

        lf.beginNewFile("logfile" + QString::number(MaxFiles));

        auto filenames = list_logfiles();

        qDebug() << filenames;

        ASSERT_EQ(static_cast<unsigned>(filenames.size()), MaxFiles);
        ASSERT_EQ(filenames.last(), QString("logfile10.log"));
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Now we have 10 files from logfile1.log to logfile10.log
    // Create another instance but this time with a lower MaxFiles value.

    {
        const unsigned MaxFiles = 5;

        LogfileHelper lf(logDirName, MaxFiles);

        ASSERT_TRUE(lf.beginNewFile("logfile" + QString::number(11)));

        auto filenames = list_logfiles();

        ASSERT_EQ(static_cast<unsigned>(filenames.size()), MaxFiles);
        ASSERT_EQ(filenames.last(), QString("logfile11.log"));
    }
}

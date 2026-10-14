#include <gtest/gtest.h>
#include "logfile_helper.h"
#include <QDebug>
#include <stdexcept>
#include <thread>
#include <chrono>

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

void sleep_for_mtime_change()
{
    static const auto duration = std::chrono::milliseconds(100);
    std::this_thread::sleep_for(duration);
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
                    throw std::runtime_error("Cannot create LogfileCountLimiter test directory");
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

                if (!QFile::remove(absfile))
                {
                    std::cerr << "Could not remove logfile " << absfile.toStdString() << std::endl;
                    std::abort();
                }
            }

            dir.cdUp();

            if (!dir.rmdir(logDirName))
            {
                std::cerr << "Could not remove LogfileCountLimiter test directory "
                    << dir.filePath(logDirName).toStdString() << std::endl;
                std::abort();
            }
        }
};

// Tests the case where the log directory does not exist (or a logfile cannot
// be created for some other reason).
TEST(LogfileCountLimiter, FileCreationFails)
{
    ASSERT_TRUE(!QDir().exists(logDirName));

    LogfileCountLimiter lf(logDirName, 10);

    ASSERT_FALSE(lf.logMessage("theMessage"));
    ASSERT_FALSE(lf.beginNewFile("thePrefix"));
    ASSERT_FALSE(lf.logMessage("theMessage"));
    ASSERT_FALSE(lf.flush());
    ASSERT_FALSE(lf.closeCurrentFile());
}

TEST(LogfileCountLimiter, ThrowOnZeroMaxFiles)
{
    ASSERT_THROW(LogfileCountLimiter(logDirName, 0), std::runtime_error);
}

TEST_F(LogfileHelperTestFixture, BeginNewLogfile)
{
    LogfileCountLimiter lf(logDirName, 10);

    ASSERT_FALSE(lf.logMessage("foobar"));
    ASSERT_FALSE(lf.hasOpenFile());

    lf.beginNewFile("thePrefix");

    ASSERT_EQ(lf.currentFilename(), QString("thePrefix.log"));
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
    // timestamps and thus the time based sorting in LogfileCountLimiter and in this
    // test yield predictable results.

    {
        const unsigned MaxFiles = 10;

        LogfileCountLimiter lf(logDirName, MaxFiles);

        for (unsigned i = 0; i < MaxFiles; i++)
        {
            ASSERT_TRUE(lf.beginNewFile("logfile" + QString::number(i)));


            const auto message = "message" + QString::number(i);

            ASSERT_TRUE(lf.logMessage(message));
            ASSERT_TRUE(lf.flush());
            ASSERT_EQ(read_file(lf.currentAbsFilepath()), message);

            sleep_for_mtime_change();
        }

        ASSERT_EQ(static_cast<unsigned>(list_logfiles().size()), MaxFiles);

        lf.beginNewFile("logfile" + QString::number(MaxFiles));

        {
            const auto message = "message" + QString::number(MaxFiles);

            ASSERT_TRUE(lf.logMessage(message));
            ASSERT_TRUE(lf.flush());
            ASSERT_EQ(read_file(lf.currentAbsFilepath()), message);
        }

        auto filenames = list_logfiles();

        ASSERT_EQ(static_cast<unsigned>(filenames.size()), MaxFiles);
        ASSERT_EQ(filenames.last(), QString("logfile10.log"));
    }

    sleep_for_mtime_change();

    // Now we have 10 files from logfile1.log to logfile10.log
    // Create another instance but this time with a lower MaxFiles value.

    {
        const unsigned MaxFiles = 5;

        LogfileCountLimiter lf(logDirName, MaxFiles);

        ASSERT_TRUE(lf.beginNewFile("logfile" + QString::number(11)));

        auto filenames = list_logfiles();

        ASSERT_EQ(static_cast<unsigned>(filenames.size()), MaxFiles);
        ASSERT_EQ(filenames.last(), QString("logfile11.log"));
        ASSERT_EQ(lf.currentFilename(), QString("logfile11.log"));
    }
}

// Note: for correctness the cases in the LastlogHelper constructor should be
// checked. This means file permissions would have to be changed so that
// removing, renaming and file creation fail :-(
TEST(LastlogHelper, MissingDirConstructorHasNoOpenFile)
{
    LastlogHelper helper(QDir(logDirName), "foo.log", "last_foo.log");
    ASSERT_FALSE(helper.hasOpenFile());
}

// Testing the LastlogHelper:
// - start with an empty dir. create an instance, assert that the log file
// exists.
// - destroy the instance
// - create another instance, assert that the log has moved to lastlog and that
//   an empty new logfile exists.
// - assert that log message go to the first logfile
// - destroy the instance
// - create a third instance and ensure that the existing lastlog is removed,
//   the file rotation happens and an empty new log is created.

TEST_F(LogfileHelperTestFixture, LastlogHelperTest)
{
    static const QString &logname = "testlog.log";
    static const QString &lastlogname = "last_" + logname;

    QDir logDir(logDirName);
    ASSERT_FALSE(logDir.exists(logname));
    ASSERT_FALSE(logDir.exists(lastlogname));

    {
        LastlogHelper llh(logDir, logname, lastlogname);

        ASSERT_FALSE(logDir.exists(lastlogname));
        ASSERT_TRUE(logDir.exists(logname));

        ASSERT_TRUE(llh.logMessage("foobar"));
        ASSERT_TRUE(llh.flush());
        ASSERT_EQ(read_file(logDir.absoluteFilePath(logname)), QString("foobar"));
    }

    {
        LastlogHelper llh(logDir, logname, lastlogname);

        ASSERT_TRUE(logDir.exists(lastlogname));
        ASSERT_TRUE(logDir.exists(logname));

        ASSERT_EQ(read_file(logDir.absoluteFilePath(logname)), QString());
        ASSERT_EQ(read_file(logDir.absoluteFilePath(lastlogname)), QString("foobar"));

        ASSERT_TRUE(llh.logMessage("EnergyCakeBestCake"));
        ASSERT_TRUE(llh.flush());
        ASSERT_EQ(read_file(logDir.absoluteFilePath(logname)), QString("EnergyCakeBestCake"));
    }

    {
        LastlogHelper llh(logDir, logname, lastlogname);

        ASSERT_TRUE(logDir.exists(lastlogname));
        ASSERT_TRUE(logDir.exists(logname));

        ASSERT_EQ(read_file(logDir.absoluteFilePath(logname)), QString());
        ASSERT_EQ(read_file(logDir.absoluteFilePath(lastlogname)), QString("EnergyCakeBestCake"));
    }
}

/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
//#include "event_server_lib.h"
#include <event_server/common/event_server_lib.h>

#include <chrono>
#include <getopt.h>
#include <signal.h>

using std::cerr;
using std::cout;
using std::endl;
using namespace mvme::event_server;

namespace
{

using ClockType = std::chrono::high_resolution_clock;

struct RunStats
{
    ClockType::time_point tStart;
    ClockType::time_point tEnd;
    size_t messageCount = 0;
    size_t errorCount = 0;
    std::vector<size_t> eventCounts;
    std::vector<size_t> eventDataBytes;
    std::vector<std::vector<size_t>> eventDSBytes;
    size_t totalDataBytes = 0;
    size_t dataMessageCount = 0;
    size_t dataMessageSizeSum = 0;
};

class Context: public mvme::event_server::Client
{
    public:
        bool doQuit() const { return m_quit; }
        void setSingleRun(bool b) { m_singleRun = b; }
        void setPrintData(bool b) { m_printData = b; }

    protected:
        virtual void serverInfo(const Message &msg, const json &info) override;

        virtual void beginRun(const Message &msg, const StreamInfo &streamInfo) override;

        virtual void eventData(const Message &msg, int eventIndex,
                                const std::vector<DataSourceContents> &contents) override;

        virtual void endRun(const Message &msg, const json &info) override;

        virtual void error(const Message &msg, const std::exception &e) override;

    private:
        RunStats m_stats;
        bool m_quit = false;
        bool m_singleRun = false;
        bool m_printData = false;
};

void Context::serverInfo(const Message &/*msg*/, const json &info)
{
    cout << __FUNCTION__ << ": serverInfo=" << endl << info.dump(2) << endl;
}

void Context::beginRun(const Message &/*msg*/, const StreamInfo &streamInfo)
{
    cout << __FUNCTION__ << ": streamInfo JSON data:" << endl
        << streamInfo.infoJson.dump(2) << endl;

    cout << __FUNCTION__ << ": runId=" << streamInfo.runId
        << endl;

    m_stats = {};
    m_stats.messageCount++;

    m_stats.eventCounts.clear();
    m_stats.eventCounts.resize(streamInfo.eventDataDescriptions.size(), 0u);

    m_stats.eventDataBytes.clear();
    m_stats.eventDataBytes.resize(streamInfo.eventDataDescriptions.size(), 0u);

    m_stats.eventDSBytes.clear();

    for (auto &edd: streamInfo.eventDataDescriptions)
    {
        m_stats.eventDSBytes.push_back(std::vector<size_t>(edd.dataSources.size()));
    }

    m_stats.tStart = ClockType::now();
}

void Context::eventData(const Message &msg, int eventIndex,
                        const std::vector<DataSourceContents> &contents)
{
    m_stats.messageCount++;
    m_stats.eventCounts[eventIndex]++;
    m_stats.dataMessageCount++;
    m_stats.dataMessageSizeSum += msg.size();

    if (m_printData)
    {
        cout << "EventData, eventIndex=" << eventIndex << endl;
    }

    for (size_t dsIndex = 0; dsIndex < contents.size(); dsIndex++)
    {
        auto &dsc = contents[dsIndex];
        size_t bytes = get_entry_size(dsc) * dsc.count;
        m_stats.totalDataBytes += bytes;
        m_stats.eventDataBytes[eventIndex] += bytes;
        m_stats.eventDSBytes[eventIndex][dsIndex] += bytes;

        if (m_printData)
        {
            cout << "  dsIndex=" << dsIndex << ": ";
            print(cout, dsc);
        }
    }
}

void Context::endRun(const Message &/*msg*/, const json &info)
{
    m_stats.tEnd = ClockType::now();
    m_stats.messageCount++;

    cout << __FUNCTION__ << ": endRun info JSON:" << endl << info.dump(2) << endl << endl;

    cout << __FUNCTION__ << "client run stats:" << endl;

    std::chrono::duration<float> elapsed = m_stats.tEnd - m_stats.tStart;
    float elapsed_s = elapsed.count();
    float bytesPerSecond = m_stats.totalDataBytes / elapsed_s;
    float MBPerSecond = bytesPerSecond / (1024 * 1024);

    cout << "duration: " << elapsed_s << "s" << endl;
    cout << "data: "
        << m_stats.totalDataBytes << " bytes, "
        << m_stats.totalDataBytes / (1024 * 1024.0) << " MB"
        << endl;
    cout << "rate: "
        << bytesPerSecond << " B/s, "
        << MBPerSecond << " MB/s"
        << endl;

    cout << "dataMessageCount: " << m_stats.dataMessageCount
        << endl
        << "totalMessageSizes: " << m_stats.dataMessageSizeSum
        << endl;

    cout << "eventCounts: ";
    for (size_t ei = 0; ei < m_stats.eventCounts.size(); ei++)
    {
        cout << "ei=" << ei << ", count=" << m_stats.eventCounts[ei] << "; ";
    }
    cout << endl;

    if (m_singleRun) m_quit = true;
}

void Context::error(const Message &, const std::exception &)
{
    m_stats.errorCount++;
}

static bool signal_received = false;

void signal_handler(int signum)
{
#ifndef _WIN32
    cout << "signal " << signum << endl;
    cout.flush();
    signal_received = true;
#endif
}

void setup_signal_handlers()
{
#ifndef _WIN32
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    for (auto signum: { SIGINT, SIGHUP, SIGTERM })
    {
        if (sigaction(signum, &new_action, NULL) != 0)
            throw std::system_error(errno, std::generic_category(), "setup_signal_handlers");
    }
#endif
}

} // end anon namespace

int main(int argc, char *argv[])
{
    // host, port, quit after one run?,
    // output filename? if not specified is taken from the runId
    // send out a reply is response to the EndRun message?
    std::string host = "localhost";
    std::string port = "13801";
    bool showHelp = false;

    setup_signal_handlers();

    int res = mvme::event_server::lib_init();
    if (res != 0)
    {
        cerr << "lib_init() failed with code " << res << endl;
        return 1;
    }

    Context ctx;

    while (true)
    {
        static struct option long_options[] =
        {
            { "single-run",             no_argument, nullptr,    0 },
            { "print-data",             no_argument, nullptr,    0 },
            { "help",                   no_argument, nullptr,    0 },
            { nullptr, 0, nullptr, 0 },
        };

        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c == '?') // Unrecognized option
        {
            return 1;
        }

        if (c != 0)
            break;

        std::string opt_name(long_options[option_index].name);

        if (opt_name == "single-run") ctx.setSingleRun(true);
        if (opt_name == "print-data") ctx.setPrintData(true);
        if (opt_name == "help") showHelp = true;
    }

    if (showHelp)
    {
        cout << "Usage: " << argv[0]
            << " [--single-run] [--print-data] [host=localhost] [port=13801]"
            << endl << endl
            ;

        cout << "  If single-run is set the process will exit after receiving" << endl
             << "  data from one run. Otherwise it will wait for the next run to" << endl
             << "  start." << endl << endl
             ;

        return 0;
    }

    if (optind < argc) { host = argv[optind++]; }
    if (optind < argc) { port = argv[optind++]; }

    Message msg;
    int sockfd = -1;
    int retval = 0;

    while (!ctx.doQuit() && !signal_received)
    {
        if (sockfd < 0)
        {
            cout << "Connecting to " << host << ":" << port << " ..." << endl;
        }

        while (sockfd < 0 && !signal_received)
        {
            try
            {
                sockfd = connect_to(host.c_str(), port.c_str());
            }
            catch (const mvme::event_server::exception &e)
            {
                sockfd = -1;
            }

            if (sockfd >= 0)
            {
                cout << "Connected to " << host << ":" << port << endl;
                break;
            }

            if (usleep(1000 * 1000) != 0 && errno != EINTR)
            {
                throw std::system_error(errno, std::generic_category(), "usleep");
            }
        }

        if (signal_received) break;

        try
        {
            read_message(sockfd, msg);
            ctx.handleMessage(msg);
        }
        catch (const mvme::event_server::connection_closed &)
        {
            cout << "Error: The remote host closed the connection." << endl;
            sockfd = -1;
            // Reset context state as we're going to attempt to reconnect.
            ctx.reset();
        }
        catch (const mvme::event_server::exception &e)
        {
            cout << "An error occured: " << e.what() << endl;
            retval = 1;
            break;
        }
        catch (const std::system_error &e)
        {
            cout << "Disconnected from " << host << ":" << port
                << ", reason: " << e.what() << endl;
            sockfd = -1;
            retval = 1;
            break;
        }
    }

    mvme::event_server::lib_shutdown();
    return retval;
}

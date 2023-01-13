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
#include <fstream>
#include <getopt.h>
#include <signal.h>
#include <string>

// ROOT
#include <TFile.h>
#include <TROOT.h> // gROOT
#include <TSystem.h> // gSystem

// mvme
#include <Mustache/mustache.hpp>
#include "event_server/common/event_server_lib.h"
#include "event_server/client/mvme_root_event_objects.h"

using std::cerr;
using std::cout;
using std::endl;

namespace mu = kainjow::mustache;
using namespace mvme::event_server;


// The c++11 way of including text strings into the binary. Uses the new R"()"
// raw string syntax and the preprocessor to create a static string embedded in
// the binary.

static const char *exportHeaderTemplate =
#include "event_server/client/templates/user_objects.h.mustache"
;

static const char *exportImplTemplate =
#include "event_server/client/templates/user_objects.cxx.mustache"
;

//
// ClientContext
//
class ClientContext: public mvme::event_server::Parser
{
    public:
        ClientContext(const std::string &outputDirectory, bool convertNaNsToZero)
            : m_outputDirectory(outputDirectory)
            , m_convertNansToZero(convertNaNsToZero)
        { }

    protected:
        virtual void serverInfo(const Message &msg, const json &info) override;
        virtual void beginRun(const Message &msg, const StreamInfo &streamInfo) override;

        virtual void eventData(const Message &msg, int eventIndex,
                                const std::vector<DataSourceContents> &contents) override;

        virtual void endRun(const Message &msg, const json &info) override;

        virtual void error(const Message &msg, const std::exception &e) override;

    private:
        std::string m_outputDirectory;
        bool m_convertNansToZero = false;
};

void ClientContext::serverInfo(const Message &msg, const json &info)
{
    cout << __FUNCTION__ << ": serverInfo:" << endl << info.dump(2) << endl;
}

void ClientContext::beginRun(const Message &msg, const StreamInfo &streamInfo)
{
    cout << __FUNCTION__ << ": run information:"
        << endl << streamInfo.infoJson.dump(2) << endl;

    cout << __FUNCTION__ << ": runId=" << streamInfo.runId
        << endl;

    std::string projectName = streamInfo.infoJson["ProjectName"];
    std::string projectTitle = streamInfo.infoJson["ProjectTitle"];

    cout << __FUNCTION__ << ": generating ROOT classes for Experiment " << projectName << endl;

    mu::data mu_vmeEvents = mu::data::type::list;

    for (const auto &event: streamInfo.vmeTree.events)
    {
        mu::data mu_vmeModules = mu::data::type::list;

        for (const auto &module: event.modules)
        {
            mu::data mu_moduleDataMembers = mu::data::type::list;

            for (const auto &edd: streamInfo.eventDataDescriptions)
            {
                if (edd.eventIndex != event.eventIndex) continue;

                for (const auto &ds: edd.dataSources)
                {
                    if (ds.moduleIndex != module.moduleIndex) continue;

                    mu::data mu_dataMember = mu::data::type::object;
                    mu_dataMember["type"] = ds.dataType;
                    mu_dataMember["name"] = ds.name;
                    mu_dataMember["size"] = std::to_string(ds.size);

                    mu_moduleDataMembers.push_back(mu_dataMember);
                }
            }

            mu::data mu_module = mu::data::type::object;
            mu_module["struct_name"] = "Module_" + module.name;
            mu_module["name"] = module.name;
            mu_module["title"] = "Data storage for module " + module.name;
            mu_module["var_name"] = module.name;
            mu_module["data_members"] = mu::data{mu_moduleDataMembers};
            mu_module["event_name"] = event.name;
            mu_vmeModules.push_back(mu_module);
        }

        mu::data mu_event = mu::data::type::object;
        mu_event["struct_name"] = "Event_" + event.name;
        mu_event["title"] = "Storage for event " + event.name;
        mu_event["name"] = event.name;
        mu_event["var_name"] = event.name;
        mu_event["modules"] = mu::data{mu_vmeModules};
        mu_vmeEvents.push_back(mu_event);
    }

    // combine template data into one object
    mu::data mu_data;
    mu_data["vme_events"] = mu::data{mu_vmeEvents};
    mu_data["exp_name"] = projectName;
    std::string experimentStructName = projectName;
    mu_data["exp_struct_name"] = experimentStructName;
    mu_data["exp_title"] = projectTitle;
    mu_data["header_guard"] = projectName;

    std::string headerFilename = projectName + "_mvme.h";
    std::string headerFilepath = m_outputDirectory + "/" + headerFilename;
    std::string implFilename = projectName + "_mvme.cxx";
    std::string implFilepath = m_outputDirectory + "/" + implFilename;

    mu_data["header_filename"] = headerFilename;
    mu_data["impl_filename"] = implFilename;

    // write header file
    {
        mu::mustache tmpl(exportHeaderTemplate);
        std::string rendered = tmpl.render(mu_data);

        cout << "Writing header file " << headerFilepath << endl;
        std::ofstream out(headerFilepath);
        out << rendered;
    }

    // write impl file
    {
        mu::mustache tmpl(exportImplTemplate);
        std::string rendered = tmpl.render(mu_data);

        cout << "Writing impl file " << implFilepath << endl;
        std::ofstream out(implFilename);
        out << rendered;
    }

    // Build the project library. This has dependencies on both
    // mvme_root_event_objects.h and libmvme_root_event.
    // => Need to have the correct include and library paths set for the compile
    // and load step to work.
    // TODO: check return value of the .L command and provide specific error message?
#if 0
    {
        //gSystem->AddIncludePath(" -I$MVME/include ");
        //gSystem->AddLinkedLibs(" -lmvme_root_event ");
        //gSystem->AddIncludePath(" -I/home/florian/src/build-mvme2-debug ");
        //gSystem->AddLinkedLibs(" -L/home/florian/src/build-mvme2-debug -lmvme_root_event ");

        std::string cmd = ".L " + implFilepath + "+v";
        cout << "Running ROOT command: '" << cmd << "'" << endl;
        auto res = gROOT->ProcessLineSync(cmd.c_str());
        cout << endl << "-> result = " << res << endl;

        // Instantiate the project specific Experiment subclass we just
        // generated and built.
        cmd = "new " + experimentStructName + "();";
        cout << "Running ROOT command: " << cmd << endl;
        auto experiment = reinterpret_cast<Experiment *>(
            gROOT->ProcessLineSync(cmd.c_str()));
        assert(experiment);
    }
#endif

#if 1
    {
        std::string cmd = implFilepath + "+";
        cout << "LoadMacro " + cmd << endl;
        int error = 0;
        auto res = gROOT->LoadMacro(cmd.c_str(), &error);
        cout << "res=" << res << ", error=" << error << endl;
        assert(res == 0);
    }
#endif

#if 0
    // Have to figure out the path to the shared object built by mvme!
    // The first lib contains the base class implementations
    // .L libmvme_root_event.so
    // An alternative would be to use gSystem->AddLinkedLibs().
    // Test to see if this makes the snake lib link directly with the mvme_event lib
    // or maybe use gSystem->Load("") to load the lib directly without ProcessLine.

    // compile and load the generated code
    std::string cmd = ".L " + implFilepath + "+";
    cout << "ROOT command: " << cmd << endl;
    cout << gROOT->ProcessLineSync(cmd.c_str()) << endl;

    // instantiate the project specific Experiment subclass
    cmd = "new " + experimentStructName + "();";
    cout << "ROOT command: " << cmd << endl;
    auto experiment = reinterpret_cast<Experiment *>(
        gROOT->ProcessLineSync(cmd.c_str()));

    assert(experiment);
#endif

#if 0
    std::string filename;


    if (streamInfo.runId.empty())
    {
        cout << __FUNCTION__ << ": Warning: got an empty runId!" << endl;
        filename = "unknown_run.root";
    }
    else
    {
        filename = streamInfo.runId + ".root";
    }

    cout << __FUNCTION__ << ": Using output filename '" << filename << "'" << endl;

    m_trees.clear();
    m_outFile = std::make_unique<TFile>(filename.c_str(), "RECREATE",
                                        streamInfo.runId.c_str());

    std::set<std::string> branchNames;
    std::vector<size_t> eventByteSizes;

    // For each incoming event: create a TTree, buffer space and branches
    for (const EventDataDescription &edd: streamInfo.eventDataDescriptions)
    {
        const VMEEvent &event = streamInfo.vmeTree.events[edd.eventIndex];

        EventStorage storage;

        storage.tree = new TTree(event.name.c_str(),  // name
                                 event.name.c_str()); // title

        cout << "  event#" << edd.eventIndex << ", " << event.name << endl;

        size_t eventBytes = 0u;

        for (const DataSourceDescription &dsd: edd.dataSources)
        {
            // The branch will point to this buffer space
            storage.buffers.emplace_back(std::vector<float>(dsd.size));
            eventBytes += dsd.bytes;

            std::string branchName = make_branch_name(dsd.name);

            // branchSpec looks like: "name[Size]/D" for doubles,
            // "name[Size]/F" for floats
            std::ostringstream ss;
            ss << branchName << "[" << dsd.size << "]/F";
            std::string branchSpec = ss.str();

            branchName = make_unique_name(branchName, branchNames);
            branchNames.insert(branchName);

            cout << "    data source: " << dsd.name
                << " -> branchSpec=" << branchSpec
                << ", branchName=" << branchName
                << endl;

            // ROOT default is 32000
            static const int BranchBufferSize = 32000 * 1;

            auto branch = storage.tree->Branch(
                branchName.c_str(),
                storage.buffers.back().data(),
                branchSpec.c_str());

            assert(!branch->IsZombie());
        }

        m_trees.emplace_back(std::move(storage));
        eventByteSizes.push_back(eventBytes);
    }

    cout << "Incoming event sizes in bytes:" << endl;
    for (size_t ei=0; ei < eventByteSizes.size(); ei++)
    {
        cout << "  ei=" << ei << ", sz=" << eventByteSizes[ei] << endl;
    }

    cout << endl;
    cout << "Created output tree structures" << endl;
    cout << "Receiving data..." << endl;

    m_stats = {};
    m_stats.tStart = ClockType::now();
#endif
}

void ClientContext::eventData(const Message &msg, int eventIndex,
                        const std::vector<DataSourceContents> &contents)
{
#if 0
    assert(0 <= eventIndex && static_cast<size_t>(eventIndex) < m_trees.size());

    auto &eventStorage = m_trees[eventIndex];

    assert(eventStorage.tree);
    assert(eventStorage.buffers.size() == contents.size());

    for (size_t dsIndex = 0; dsIndex < contents.size(); dsIndex++)
    {
        const DataSourceContents &dsc = contents[dsIndex];

        std::vector<float> &buffer = eventStorage.buffers[dsIndex];

        assert(dsc.dataEnd - dsc.dataBegin == static_cast<ptrdiff_t>(buffer.size()));

        if (m_convertNaNs)
        {
            // Copy, but transform NaN values to 0.0
            std::transform(dsc.dataBegin, dsc.dataEnd, buffer.begin(),
                           [] (const double value) -> float {
                               return std::isnan(value) ? 0.0 : value;
                           });
        }
        else
        {
            // Copy data, implicitly converting incoming doubles to float
            std::copy(dsc.dataBegin, dsc.dataEnd, buffer.begin());
        }

        size_t bytes = (dsc.dataEnd - dsc.dataBegin) * sizeof(double);
        m_stats.totalDataBytes += bytes;
    }

    eventStorage.tree->Fill();
    eventStorage.hits++;
#endif
}

void ClientContext::endRun(const Message &msg, const json &info)
{
#if 0
    cerr << __FUNCTION__ << endl;

    if (m_outFile)
    {
        cout << "  Closing output file " << m_outFile->GetName() << "..." << endl;
        m_outFile->Write();
        m_outFile->Close();
        m_outFile.release();
    }

    cout << "  HitCounts by event:" << endl;
    for (size_t ei=0; ei < m_trees.size(); ei++)
    {
        const auto &es = m_trees[ei];

        cout << "    ei=" << ei << ", hits=" << es.hits << endl;
    }

    cout << endl;

    m_stats.tEnd = ClockType::now();
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

    if (m_singleRun) m_quit = true;
#endif
}

void ClientContext::error(const Message &msg, const std::exception &e)
{
    cout << "An error occured: " << e.what() << endl;

#if 0
    if (m_outFile)
    {
        cout << "Closing output file " << m_outFile->GetName() << "..." << endl;
        m_outFile->Close();
        m_outFile.release();
    }

    m_quit = true;
#endif
}

static bool signal_received = false;

void signal_handler(int signum)
{
    cout << "signal " << signum << endl;
    cout.flush();
    signal_received = true;
}

void setup_signal_handlers()
{
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
}

//
// main
//
int main(int argc, char *argv[])
{
#if 1
    // host, port, quit after one run?,
    // output filename? if not specified is taken from the runId
    // send out a reply is response to the EndRun message?
    std::string host = "localhost";
    std::string port = "13801";
    std::string outputDirectory = ".";
    bool singleRun = false;
    bool convertNaNsToZero = false;
    bool showHelp = false;

    while (true)
    {
        static struct option long_options[] =
        {
            { "single-run", no_argument, nullptr, 0 },
            { "convert-nans", no_argument, nullptr, 0 },
            { "output-directory", required_argument, nullptr, 0 },
            { "help", no_argument, nullptr, 0 },
            { nullptr, 0, nullptr, 0 },
        };

        int option_index = 0;
        int c = getopt_long(argc, argv, "o:", long_options, &option_index);

        if (c == -1) break;

        switch (c)
        {
            case '?':
                // Unrecognized option
                return 1;
            case 'o':
                outputDirectory = optarg;
                break;

            case 0:
                // long options
                {
                    std::string opt_name(long_options[option_index].name);

                    if (opt_name == "single-run") singleRun = true;
                    if (opt_name == "convert-nans") convertNaNsToZero = true;
                    if (opt_name == "output-directory") outputDirectory = optarg;
                    if (opt_name == "help") showHelp = true;
                }
        }
    }

    if (showHelp)
    {
        cout << "Usage: " << argv[0]
            << " [--single-run] [--convert-nans] [--output-directory <dir>=.]"
               " [host=localhost] [port=13801]"
            << endl << endl
            ;

        cout << "  If single-run is set the process will exit after receiving" << endl
             << "  data from one run. Otherwise it will wait for the next run to" << endl
             << "  start." << endl
             << endl
             << "  If convert-nans is set incoming NaN data values will be" << endl
             << "  converted to 0.0 before they are written to their respective ROOT" << endl
             << "  tree Branch." << endl
             << endl
             ;

        return 0;
    }
#endif

    setup_signal_handlers();

    if (int res = mvme::event_server::lib_init() != 0)
    {
        cerr << "mvme::event_server::lib_init() failed with code " << res << endl;
        return 1;
    }

    ClientContext ctx(outputDirectory, convertNaNsToZero);

    // A single message object, whose buffer is reused for each incoming
    // message.
    Message msg;
    int sockfd = -1;
    int retval = 0;
    bool doQuit = false;

    while (!doQuit && !signal_received)
    {
        if (sockfd < 0)
        {
            cout << "Connecting to " << host << ":" << port << " ..." << endl;
        }

        // auto reconnect loop
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

            if (singleRun && msg.type == MessageType::EndRun)
            {
                doQuit = true;
            }
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













#if 0
    // The impl and the header file have to be generated by mvme.

    TFile f("test1.root", "recreate");

    // produces SnakeMVME_cxx.so and loads it immediately
    cout << gROOT->ProcessLineSync(".L SnakeMVME.cxx+") << endl;

    auto experiment = reinterpret_cast<Experiment *>(
        gROOT->ProcessLineSync("new SnakeExperiment();"));

    if (!experiment) return 1;

    cout << experiment->ClassName() << endl;

    auto trees = experiment->MakeTrees();

    assert(trees.size() == experiment->GetNumberOfEvents());

    f.Write();
#endif



    return 0;
}

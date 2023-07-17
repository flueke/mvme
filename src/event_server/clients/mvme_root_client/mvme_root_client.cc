/* mvme_root_client - A client to MVMEs event_server component producing ROOT
 *                    code and data files.
 *
 * Copyright (C) 2019 mesytec GmbH & Co. KG <info@mesytec.com>
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

/*
 * The program works in two modes:
 * 1) As a client it connects to an mvme instance and receives incoming readout
 *    data to produce ROOT output files.
 * 2) The program replays data from a previously created ROOT output file.
 *
 * In the first case the data description sent by mvme at the beginning of a
 * run is used to generate ROOT classes. These classes are then compiled and
 * loaded into the client. Compilation is done using a simple Makefile. The
 * resulting library is then loaded via TSystem::Load().
 *
 * During the run incoming event data is interpreted and used to fill instances
 * of the generated ROOT classes. At the end of the run the class hierarchy is
 * written to file in the form of TTrees. Additionally raw histograms are
 * created, filled and written to a separate output file.
 *
 * In the 2nd case the given input file is opened and mvme specific information
 * is used to locate the previously built ROOT object library. The library is
 * then loaded like in case 1) and the ROOT objects are filled from the data in
 * the input file.
 *
 * In both cases user-editable analysis code is loaded (via dlopen()/dlsym())
 * and invoked for each complete event received from mvme or read from the
 * input file.
 */

/* TODO:
 * - add return codes to the event handler functions or replace do_exit with a
 *   return code value. This should be done so that main can exit with a
 *   non-zero code in case of errors.
 *   Maybe better: return true/false. In case of false the event handler should
 *   set an error string which can then be retrieved and printed in main()
 *   before exiting.
 * - add or transmit eventNumbers
 * - the analysis code could be reloaded for each run/file.
 */

#include <cctype>
#include <fstream>
#include <regex>
#include <string>
#include <sstream>

#include <dlfcn.h> // dlopen, dlsym, dlclose
#include <getopt.h>
#include <signal.h>
#include <sys/stat.h>

// ROOT
#include <TFile.h>
#include <TH1D.h>
#include <TRandom.h> // gRandom
#include <TROOT.h>   // gROOT
#include <TSystem.h> // gSystem

// mvme
#include <mvme/Mustache/mustache.hpp> // mustache template engine
#include <mvme/event_server/common/event_server_lib.h> // mvme event_server protocol parsing and socket handling
#include "mvme_root_event_objects.h" // base classes for generated experiment ROOT objects

using std::cerr;
using std::cout;
using std::endl;

namespace mu = kainjow::mustache;
using namespace mvme::event_server;

// Directly include the mustache template contents into the binary. This uses
// the raw string syntax introduced with c++11.
static const char *exportHeaderTemplate =
#include "templates/user_objects.h.mustache"
;

static const char *exportImplTemplate =
#include "templates/user_objects.cxx.mustache"
;

static const char *exportLinkDefTemplate =
#include "templates/user_objects_LinkDef.h.mustache"
;

static const char *analysisImplTemplate =
#include "templates/analysis.cxx.mustache"
;

static const char *analysisMkTemplate =
#include "templates/analysis.mk.mustache"
;

static const char *makefileTemplate =
#include "templates/Makefile.mustache"
;

static const char *rootPremakeHookTemplate =
#include "templates/mvme_root_premake_hook.C.mustache"
;

// User analysis callbacks
struct UserAnalysis
{
    using InitFunc      = bool (*)(const std::vector<std::string> &args);
    using ShutdownFunc  = bool (*)();
    using BeginRunFunc  = bool (*)(const std::string &inputSource, const std::string &runId,
                                   bool isReplay);
    using EndRunFunc    = bool (*)();
    using EventFunc     = bool (*)(const MVMEEvent *event);

    InitFunc init;
    ShutdownFunc shutdown;
    BeginRunFunc beginRun;
    EndRunFunc endRun;
    // Per event analysis functions ordered by event index (same order as
    // MVMEExperiment::GetEvents())
    std::vector<EventFunc> eventFunctions;
};

struct MakeRawHistosResult
{
    // Histograms indexed by event, then module data storage
    std::vector<std::vector<TH1D *>> histos;
    // Total number of histograms
    size_t histoCount;
    // Bytes used for histo storage
    size_t histoMem;
};

// Creates raw histograms for the given experiment class. For each event in the
// experiment a directory to hold the histograms is created in the histoOutFile.
// The returned histograms are owned by the respective directory. Do not delete them.
static MakeRawHistosResult make_raw_histograms(const MVMEExperiment *exp, TFile *histoOutFile)
{
    MakeRawHistosResult result = {};

    for (const auto &event: exp->GetEvents())
    {
        auto dir = histoOutFile->mkdir(event->GetName());
        assert(dir);
        dir->cd();
        std::vector<TH1D *> eventHistos;

        for (const auto &module: event->GetModules())
        {
            // Iterates through the concrete data arrays defined for the module
            // and creates a histogram for each array element ("parameter").
            for (const auto &userStorage: module->GetDataStorages())
            {
                // cap the bin count at 16 bits
                unsigned bins = 1u << std::min(userStorage.bits, 16u);

                for (size_t paramIndex = 0; paramIndex < userStorage.size; paramIndex++)
                {
                    std::string histoName;

                    if (paramIndex < userStorage.paramNames.size())
                    {
                        histoName = module->GetName() + std::string("_")
                            + userStorage.paramNames[paramIndex];
                    }
                    else
                    {
                        histoName = module->GetName() + std::string("_") + userStorage.name
                            + "[" + std::to_string(paramIndex) + "]";
                    }

                    auto histo = new TH1D(
                        histoName.c_str(),
                        histoName.c_str(),
                        bins,
                        0.0,
                        std::pow(2.0, userStorage.bits)
                        );

                    eventHistos.emplace_back(histo);

                    result.histoMem += bins * sizeof(double);
                    ++result.histoCount;
                }
            }
        }

        result.histos.emplace_back(std::move(eventHistos));
        histoOutFile->cd();
    }

    return result;
}

//
// ClientContext
//
struct Options
{
    using Opt_t = uint32_t;
    static const Opt_t ShowStreamInfo   = 1u << 1;
    static const Opt_t NoAddedRandom    = 1u << 2;
    static const Opt_t SingleRun        = 1u << 3;
    static const Opt_t ReplayMode       = 1u << 4;
    static const Opt_t DetectSplitRuns  = 1u << 5;
};

class ClientContext: public mvme::event_server::Client
{
    public:
        struct RunStats
        {
            using ClockType = std::chrono::high_resolution_clock;
            ClockType::time_point tStart;
            ClockType::time_point tEnd;
            size_t totalDataBytes = 0;
            std::vector<size_t> eventHits;
        };

        ClientContext(const Options::Opt_t &options);

        RunStats GetRunStats() const { return m_stats; }
        bool ShouldQuit() const { return m_quit; }
        void setHostAndPort(const std::string &host, const std::string &port)
        {
            m_host = host;
            m_port = port;
        }

        void setAnalysisInitArgs(const std::vector<std::string> &args)
        {
            m_analysisInitArgs = args;
        }

        void setRootMaxTreeSize(const ssize_t maxTreeSize)
        {
            m_rootMaxTreeSize = maxTreeSize;
        }

        std::vector<std::string> getAnalysisInitArgs() const
        {
            return m_analysisInitArgs;
        }

    protected:
        virtual void serverInfo(const Message &msg, const json &info) override;
        virtual void beginRun(const Message &msg, const StreamInfo &streamInfo) override;

        virtual void eventData(const Message &msg, int eventIndex,
                                const std::vector<DataSourceContents> &contents) override;

        virtual void endRun(const Message &msg, const json &info) override;

        virtual void error(const Message &msg, const std::exception &e) override;

    private:
        Options::Opt_t m_options;
        std::unique_ptr<MVMEExperiment> m_exp;

        std::vector<TTree *> m_eventTrees;

        std::unique_ptr<TFile> m_histoOutFile;
        MakeRawHistosResult m_rawHistos;

        // Set to true if the output TTree and histogram instances have been
        // created.
        bool m_outputTreesAndHistosCreated = false;

        RunStats m_stats;
        bool m_quit = false;
        bool m_codeGeneratedAndLoaded = false;
        void *m_analysisLibHandle = nullptr;
        UserAnalysis m_analysis = {};
        std::string m_host;
        std::string m_port;
        std::vector<std::string> m_analysisInitArgs;
        ssize_t m_rootMaxTreeSize = 100000000000LL;
};

ClientContext::ClientContext(const Options::Opt_t &options)
    : m_options(options)
{
}

void ClientContext::serverInfo(const Message &msg, const json &info)
{
    cout << "serverInfo:" << endl << info.dump(2) << endl;
}

static mu::data build_event_template_data(const StreamInfo &streamInfo)
{
    mu::data mu_vmeEvents = mu::data::type::list;

    for (const auto &event: streamInfo.vmeTree.events)
    {
        mu::data mu_vmeModules = mu::data::type::list;

        for (const auto &module: event.modules)
        {
            mu::data mu_moduleDataMembers = mu::data::type::list;
            mu::data mu_moduleRefMembers = mu::data::type::list;

            int dsIndex = 0;
            for (const auto &edd: streamInfo.eventDataDescriptions)
            {
                if (edd.eventIndex != event.eventIndex) continue;

                for (const auto &dsd: edd.dataSources)
                {
                    if (dsd.moduleIndex != module.moduleIndex) continue;

                    mu::data mu_dataMember = mu::data::type::object;
                    mu_dataMember["name"] = dsd.name;
                    mu_dataMember["size"] = std::to_string(dsd.size);
                    mu_dataMember["bits"] = std::to_string(dsd.bits);
                    mu::data mu_paramNames = mu::data::type::list;

                    size_t paramCount = std::min(dsd.paramNames.size(),
                                                 static_cast<size_t>(dsd.size));

                    for (size_t paramIndex = 0;
                         paramIndex < paramCount;
                         paramIndex++)
                    {
                        mu::data mu_refMember = mu::data::type::object;
                        mu_refMember["name"] = dsd.paramNames[paramIndex];
                        mu_refMember["index"] = std::to_string(paramIndex);
                        mu_refMember["target"] = dsd.name;

                        mu_moduleRefMembers.push_back(mu_refMember);

                        mu_paramNames.push_back(dsd.paramNames[paramIndex]);
                    }

                    mu_dataMember["param_names"] = mu::data{mu_paramNames};
                    mu_moduleDataMembers.push_back(mu_dataMember);
                }
                dsIndex++;
            }

            mu::data mu_module = mu::data::type::object;
            mu_module["struct_name"] = "Module_" + module.name;
            mu_module["name"] = module.name;
            mu_module["title"] = "Module " + module.name;
            mu_module["var_name"] = module.name;
            mu_module["data_members"] = mu::data{mu_moduleDataMembers};
            mu_module["ref_members"] = mu::data{mu_moduleRefMembers};
            mu_module["event_name"] = event.name;
            mu_vmeModules.push_back(mu_module);
        }

        mu::data mu_event = mu::data::type::object;
        mu_event["struct_name"] = "Event_" + event.name;
        mu_event["title"] = "Storage for event '" + event.name + "'";
        mu_event["name"] = event.name;
        mu_event["var_name"] = event.name;
        mu_event["modules"] = mu::data{mu_vmeModules};
        mu_vmeEvents.push_back(mu_event);
    }

    return mu_vmeEvents;
}

template<typename T>
T load_sym(void *handle, const char *name)
{
    return reinterpret_cast<T>(dlsym(handle, name));
}

enum class OverwriteOption
{
    Never,
    Always,
    IfDifferent,
};

enum class CodeGenResult
{
    Created,
    Exists,
    Overwritten,
    Unchanged,
    WriteError,
};

struct CodeGenArgs
{
    std::string outputFilename;
    const char *templateContents;
    OverwriteOption overwriteOption;
    const char *description;
};

CodeGenResult generate_code_file(const CodeGenArgs &args, const mu::data &templateData)
{
    auto read_file = [](const std::string &filename)
    {
        std::ifstream fin(filename);
        std::stringstream sstr;
        sstr << fin.rdbuf();
        return sstr.str();
    };

    auto write_file = [](const std::string &contents, const std::string &filename)
    {
        std::ofstream out(filename);
        out << contents;
        return static_cast<bool>(out);
    };

    auto render_template = [](const char *contents, const mu::data &data)
    {
        auto no_escape = [](const auto &s) { return s; };
        mu::mustache tmpl(contents);
        tmpl.set_custom_escape(no_escape);
        return tmpl.render(data);
    };

    auto render_to_file = [&](const char *contents, const mu::data &data,
                                             const std::string &filename)
    {
        return write_file(render_template(contents, data), filename);
    };

    auto file_exists = [](const std::string &filename)
    {
        struct stat buffer;
        return (stat(filename.c_str(), &buffer) == 0);
    };

    bool doesExist = file_exists(args.outputFilename);

    if (!doesExist)
    {
        bool writeOk = render_to_file(args.templateContents, templateData, args.outputFilename);
        return (writeOk ? CodeGenResult::Created : CodeGenResult::WriteError);
    }

    // The output file exists. Check OverwriteOption to figure out what to do.

    switch (args.overwriteOption)
    {
        case OverwriteOption::Never:
            return CodeGenResult::Exists;

        case OverwriteOption::Always:
            {
                bool writeOk = render_to_file(args.templateContents, templateData,
                                              args.outputFilename);
                return (writeOk ? CodeGenResult::Overwritten : CodeGenResult::WriteError);
            } break;

        case OverwriteOption::IfDifferent:
            {
                auto existingContents = read_file(args.outputFilename);
                auto newContents = render_template(args.templateContents, templateData);

                if (existingContents == newContents)
                    return CodeGenResult::Unchanged;

                bool writeOk = write_file(newContents, args.outputFilename);
                return (writeOk ? CodeGenResult::Overwritten : CodeGenResult::WriteError);
            } break;
    };

    // Can not reach this line
    return CodeGenResult::WriteError;
}

std::unique_ptr<MVMEExperiment> compile_and_load_experiment_library(
    const std::string &expName)
{
    const std::string expStructName = expName;
    const std::string libName = "lib" + expName + "_mvme.so";

    // Run the ROOT pre-make macro
    {
        cout << ">>> Executing ROOT pre-make macro file mvme_root_premake_hook.C ..." << endl;
        auto res = gROOT->ProcessLineSync(".x mvme_root_premake_hook.C");
        cout << endl << "<<< End of output from ROOT premake hook" << endl << endl;

        if (res)
            return {};
    }

    // Run make
    {
        cout << ">>> Running make ..." << endl;
        int res = gSystem->Exec("make");
        cout << endl << "<<< End of make output" << endl;

        if (res)
            return {};
    }

    // Load experiment library
    {
        cout << ">>> Loading experiment library " << libName << endl;
        int res = gSystem->Load(libName.c_str());

        if (res != 0 && res != 1)
        {
            cout << "Error loading experiment library " << libName << endl;
            return {};
        }
    }

    // Create and return an instance of the generated experiment class cast to
    // the base class type. (Only the base class type is known to this code
    // here, not the concrete subclass from the generated code).
    std::string cmd = "new " + expStructName + "();";

    return std::unique_ptr<MVMEExperiment>(
        reinterpret_cast<MVMEExperiment *>(
            gROOT->ProcessLineSync(cmd.c_str())));
}

std::pair<void *, UserAnalysis> load_user_analysis(const char *filename, const std::vector<std::string> eventNames)
{
    void *handle = dlopen("./analysis.so", RTLD_NOW | RTLD_GLOBAL);
    UserAnalysis ua = {};

    if (handle)
    {
        ua.init = load_sym<UserAnalysis::InitFunc>(handle, "init_analysis");
        ua.shutdown = load_sym<UserAnalysis::ShutdownFunc>(handle, "shutdown_analysis");
        ua.beginRun = load_sym<UserAnalysis::BeginRunFunc>(handle, "begin_run");
        ua.endRun = load_sym<UserAnalysis::EndRunFunc>(handle, "end_run");

        for (auto &eventName: eventNames)
        {
            auto fname = std::string("analyze_") + eventName;
            auto func = load_sym<UserAnalysis::EventFunc>(handle, fname.c_str());
            ua.eventFunctions.push_back(func);
        }
    }

    return std::make_pair(handle, ua);
}

std::pair<void *, UserAnalysis> load_user_analysis(const char *filename, const MVMEExperiment *exp)
{
    std::vector<std::string> eventNames;

    for (const auto &event: exp->GetEvents())
        eventNames.emplace_back(event->GetName());

    return load_user_analysis(filename, eventNames);
}

inline double make_quiet_nan()
{
    static const double result = std::numeric_limits<double>::quiet_NaN();
    return result;
}

using MVMERunInfo = std::map<std::string, std::string>;

static const std::regex SplitRunIdRegex("^(.+)part[0-9a-fA-F]+$");

inline bool is_split_run_id(const std::string &runId)
{
    return std::regex_match(runId, SplitRunIdRegex);
}

inline std::string get_split_run_name(const std::string &runId)
{
    std::smatch matches;
    std::regex_match(runId, matches, SplitRunIdRegex);

    if (matches.size() < 2)
        return runId;

    auto runName = matches[1].str();
    while (!runName.empty() && !std::isalnum(runName.back()))
        runName.pop_back();
    return runName;
}

void ClientContext::beginRun(const Message &msg, const StreamInfo &streamInfo)
{
    if (m_options & Options::ShowStreamInfo)
    {
        cout << "Incoming BeginRun Stream Information:" << endl
            << streamInfo.infoJson.dump(2) << endl;
    }

    cout << __FUNCTION__ << ": runId=" << streamInfo.runId
        << endl;

    std::string expName = streamInfo.infoJson["ExperimentName"];
    std::string expTitle = streamInfo.infoJson["ExperimentTitle"];

    if (!m_codeGeneratedAndLoaded)
    {
        std::string headerFilename = expName + "_mvme.h";
        std::string implFilename = expName + "_mvme.cxx";

        // Tables with information about which code files to generate.
        auto expROOTCodeFiles =
        {
            CodeGenArgs
            {
                headerFilename,
                exportHeaderTemplate,
                OverwriteOption::IfDifferent,
                "objects header"
            },
            CodeGenArgs
            {
                implFilename,
                exportImplTemplate,
                OverwriteOption::IfDifferent,
                "objects implementation",
            },
            CodeGenArgs
            {
                expName + "_mvme_LinkDef.h",
                exportLinkDefTemplate,
                OverwriteOption::IfDifferent,
                "objects linkdef",
            },
        };

        auto additionalCodeFiles =
        {
            CodeGenArgs
            {
                "Makefile",
                makefileTemplate,
                OverwriteOption::IfDifferent,
                "build",
            },
            CodeGenArgs
            {
                "analysis.cxx",
                analysisImplTemplate,
                OverwriteOption::Never,
                "analysis skeleton implementation"
            },
            CodeGenArgs
            {
                "analysis.mk",
                analysisMkTemplate,
                OverwriteOption::Never,
                "analysis customization make",
            },

            CodeGenArgs
            {
                "mvme_root_premake_hook.C",
                rootPremakeHookTemplate,
                OverwriteOption::Never,
                "ROOT pre-make macro",
            },
        };

        // Build the mustache template data object
        mu::data mu_vmeEvents = build_event_template_data(streamInfo);
        mu::data mu_data;
        mu_data["vme_events"] = mu::data{mu_vmeEvents};
        mu_data["exp_name"] = expName;
        std::string expStructName = expName;
        mu_data["exp_struct_name"] = expStructName;
        mu_data["exp_title"] = expTitle;
        mu_data["header_guard"] = expName;
        mu_data["header_filename"] = headerFilename;
        mu_data["impl_filename"] = implFilename;

        // Generate output files using the mustache template data. The
        // genArgList variable must contain CodeGenArgs instances.
        auto generate_code_files = [](const auto &genArgList, const auto &mu_data) -> bool
        {
            bool retval = true;

            for (const auto &genArgs: genArgList)
            {
                CodeGenResult result = generate_code_file(genArgs, mu_data);
                std::string statusBegin;

                switch (result)
                {
                    case CodeGenResult::Created:
                        statusBegin = "Created";
                        break;
                    case CodeGenResult::Exists:
                        statusBegin = "Not overwriting existing";
                        break;
                    case CodeGenResult::Overwritten:
                        statusBegin = "Updated";
                        break;
                    case CodeGenResult::Unchanged:
                        statusBegin = "Unchanged";
                        break;
                    case CodeGenResult::WriteError:
                        statusBegin = "!!Error writing";
                        retval = false;
                        break;
                }

                cout << "  " << statusBegin << " " << genArgs.description
                    << " file " << genArgs.outputFilename << endl;
            }

            return retval;
        };

        {
            cout << "Generating ROOT code for experiment " << expName << " ..." << endl;
            if (!generate_code_files(expROOTCodeFiles, mu_data))
            {
                m_quit = true;
                return;
            }
        }

        {
            cout << "Generating additional files ..." << endl;
            if (!generate_code_files(additionalCodeFiles, mu_data))
            {
                m_quit = true;
                return;
            }
        }

        cout << endl;

        m_exp = compile_and_load_experiment_library(expName);

        if (!m_exp)
        {
            cout << "Error creating an instance of the experiment class '"
                << expStructName << "'" << endl;
            m_eventTrees = {};
            m_quit = true;
            return;
        }

        if (streamInfo.eventDataDescriptions.size() != m_exp->GetNumberOfEvents())
        {
            cout << "Error: number of Event definitions declared in StreamInfo does not equal "
                "the number of Event classes present in the generated Experiment code."
                << endl
                << "Please restart the client to regenerate the code."
                << endl;
            m_quit = true;
            return;
        }

        for (size_t eventIndex = 0; eventIndex < m_exp->GetNumberOfEvents(); eventIndex++)
        {
            auto &edd = streamInfo.eventDataDescriptions.at(eventIndex);
            auto event = m_exp->GetEvent(eventIndex);

            if (edd.dataSources.size() != event->GetDataSourceStorages().size())
            {
                cout << "Warning: eventIndex=" << eventIndex << ", eventName=" << event->GetName()
                    << ": number of data sources in the StreamInfo and in the generated Event class "
                    " differ (streamInfo:" << edd.dataSources.size()
                    << ", class:" << event->GetDataSourceStorages().size() << ")."
                    << endl
                    << "Please run `make' and start the client again."
                    << endl;
                m_quit = true;
                return;
            }
        }

// TODO: test unloading.
#if 0
        // Unload analysis
        if (m_analysisLibHandle)
        {
            if (m_analysis.shutdown)
                m_analysis.shutdown();
            dlclose(m_analysisLibHandle);
            m_analysisLibHandle = nullptr;
            m_analysis = {};
        }
#endif

        // Load analysis
        if (!m_analysisLibHandle)
        {
            cout << "Loading analysis.so" << endl;

            void *handle = nullptr;
            m_analysis = {};

            std::tie(handle, m_analysis) = load_user_analysis("analysis.so", m_exp.get());

            if (!handle)
            {
                cout << "Error loading analysis.so: " << dlerror() << endl;
                m_quit = true;
                return;
            }

            m_analysisLibHandle = handle;
        }

        m_codeGeneratedAndLoaded = true;

        if (m_analysis.init && !m_analysis.init(m_analysisInitArgs))
        {
            cout << "Analysis init function returned false, aborting" << endl;
            m_quit = true;
            return;
        }
    }
    else
    {
        cout << __FUNCTION__
            << ": Reusing previously loaded experiment and analysis code." << endl;
    }

    //
    // Create output files, TTrees and histograms.
    //

    bool isSplitRun = false;
    static const std::regex SplitRunIdRegex("^(.+)[_]*part[0-9]+$");

    if (m_options & Options::DetectSplitRuns)
        isSplitRun = is_split_run_id(streamInfo.runId);

    std::string treeOutFilename;
    std::string histoOutFilename;

    if (streamInfo.runId.empty())
    {
        cout << __FUNCTION__ << ": Warning: got an empty runId!" << endl;
        treeOutFilename = "unknown_run_raw.root";
        histoOutFilename = "unknown_run_histos.root";
    }
    else if (isSplitRun)
    {
        auto runName = get_split_run_name(streamInfo.runId);

        treeOutFilename  = runName + "_raw.root";
        histoOutFilename = runName + "_histos.root";
    }
    else
    {
        treeOutFilename = streamInfo.runId + "_raw.root";
        histoOutFilename = streamInfo.runId + "_histos.root";
    }

    if (!isSplitRun || !m_outputTreesAndHistosCreated)
    {
        //
        // Per VME event TTree output.
        //
        cout << "Opening output file " << treeOutFilename << endl;
        auto treeOutFile = new TFile(treeOutFilename.c_str(), "recreate");

        if (treeOutFile->IsZombie() || !treeOutFile->IsOpen())
        {
            cout << "Error opening output file " << treeOutFilename << " for writing: "
                << strerror(treeOutFile->GetErrno()) << endl;
            m_quit = true;
            return;
        }

        cout << "Creating output trees" << endl;
        m_eventTrees = m_exp->MakeTrees();

        for (auto &tree: m_eventTrees)
        {
            assert(tree);
            cout << "  " << tree << " " << tree->GetName() << "\t" << tree->GetTitle() << endl;
            tree->SetMaxTreeSize(m_rootMaxTreeSize);
        }

        assert(m_eventTrees.size() == m_exp->GetNumberOfEvents());

        // Note: this used to be in ClientContext::endRun() but for replays from
        // automatically split TTrees we need the info in the _first_ file of the
        // TChain.
        if (treeOutFile && !treeOutFile->IsZombie() && treeOutFile->IsOpen())
        {
            cout << "  Writing additional mvme info to output file..." << endl;

            MVMERunInfo info;

            info["ExperimentName"] = m_exp->GetName();
            info["RunID"] = getStreamInfo().runId;
            // TODO: use a TDirectory to hold mvme stuff
            // also try a TMap to hold either TStrings or more TMaps
            // figure out how freeing that memory then works
            // FIXME: this does have the title "object title" in rootls
            treeOutFile->WriteObject(&info, "MVMERunInfo");
            treeOutFile->Write();

            // Once writing tree data starts ROOT can close the file at any time due to
            // TTree::SetMaxTreeSize(). Just nulling it here to catch bugs...
            treeOutFile = nullptr;
        }

        //
        // Raw histogram output
        //
        {
            cout << "Opening histo output file " << histoOutFilename << endl;
            m_histoOutFile = std::make_unique<TFile>(histoOutFilename.c_str(), "recreate");

            if (m_histoOutFile->IsZombie() || !m_histoOutFile->IsOpen())
            {
                cout << "Error opening histo output file " << histoOutFilename
                    << " for writing: " << strerror(m_histoOutFile->GetErrno()) << endl;
                m_quit = true;
                return;
            }

            cout << "Creating raw histograms... ";
            m_rawHistos = make_raw_histograms(m_exp.get(), m_histoOutFile.get());

            cout << "created " << m_rawHistos.histoCount << " histograms."
                << " Total raw histo memory: " << (m_rawHistos.histoMem / (1024.0 * 1024.0))
                << " MB" << endl;
        }

        m_outputTreesAndHistosCreated = true;
    }

    // call custom user analysis code
    if (m_analysis.beginRun)
    {
        m_analysis.beginRun("mvme://" + m_host + ":" + m_port,
                            streamInfo.runId, streamInfo.isReplay);
    }

    m_stats = {};
    m_stats.eventHits = std::vector<size_t>(streamInfo.eventDataDescriptions.size());
    m_stats.tStart = RunStats::ClockType::now();

    cout << "BeginRun procedure done, receiving data..." << endl;
}

void ClientContext::eventData(const Message &msg, int eventIndex,
                        const std::vector<DataSourceContents> &contents)
{
    // Streaminfo received with the BeginRun message
    auto &streamInfo = getStreamInfo();

    assert(0 <= eventIndex && static_cast<size_t>(eventIndex) < m_eventTrees.size());
    assert(streamInfo.eventDataDescriptions.size() == m_eventTrees.size());

    if (!m_exp)
    {
        cout << "Error in " << __FUNCTION__
            << ": no MVMEExperiment instance was created" << endl;
        m_quit = true;
        return;
    }

    auto event = m_exp->GetEvent(eventIndex);

    if (!event)
    {
        cout << "Error in " << __FUNCTION__ << ": eventIndex "
            << eventIndex << " out of range" << endl;
        m_quit = true;
        return;
    }

    auto &edd = streamInfo.eventDataDescriptions.at(eventIndex);

#if 0
    assert(edd.eventIndex == eventIndex);
    cout << eventIndex << endl;
    cout << edd.dataSources.size() << endl;
    cout << event->GetDataSourceStorages().size() << endl;

    assert(edd.dataSources.size() == event->GetDataSourceStorages().size());
    assert(contents.size() == edd.dataSources.size());
#endif

    m_stats.eventHits[eventIndex]++;

    // Copy data from the incoming data sources to the data members of the
    // generated classes.
    for (size_t dsIndex = 0; dsIndex < contents.size(); dsIndex++)
    {
        const DataSourceContents &dsc = contents.at(dsIndex);
        const uint8_t *dscEnd = get_end_pointer(dsc);

        // Pointer into the generated array member of the module class. This is
        // where the incoming data will be written to.
        auto userStorage = event->GetDataSourceStorage(dsIndex);
        assert(userStorage.ptr);
        assert(userStorage.size == edd.dataSources.at(dsIndex).size);

        // Fill the data array with NaN values. This way when replaying we know
        // if a hit was recorded or not.
        std::fill(userStorage.ptr, userStorage.ptr + userStorage.size, make_quiet_nan());

        const size_t entrySize = get_entry_size(dsc);
        const size_t indexSize = get_storage_type_size(dsc.indexType);

        // Walk the incoming packed, indexed array and copy the data.
        // Note: The code inside the loop is hit very frequently and is thus
        // critical for performance!
        for (auto entryIndex = 0; entryIndex < dsc.count; entryIndex++)
        {
            const uint8_t *indexPtr = dsc.firstIndex + entryIndex * entrySize;
            const uint8_t *valuePtr = indexPtr + indexSize;

            if (indexPtr >= dscEnd || valuePtr >= dscEnd)
            {
                cout << "Error: incoming data source contents are inconsistent: buffer size exceeded."
                    << " eventIndex=" << eventIndex
                    << ", dataSourceIndex=" << dsIndex
                    << ", entryIndex=" << entryIndex
                    << endl;
                m_quit = true;
                return;
            }

            uint32_t index = read_storage<uint32_t>(dsc.indexType, indexPtr);
            double value = read_storage<double>(dsc.valueType, valuePtr);

            // Perform the copy into the generated raw array
            if (index < userStorage.size)
            {
                // Add a random in [0, 1) to avoid binning issues.
                if (!(m_options & Options::NoAddedRandom))
                {
                    value += gRandom->Uniform();
                }

                userStorage.ptr[index] = value;
            }
            else
            {
                cout << "Error: param index value " << index << " out of range."
                    << " eventIndex=" << eventIndex
                    << ", dataSourceIndex=" << dsIndex
                    << ", entryIndex=" << entryIndex
                    << ", userStorage.size=" << userStorage.size
                    << endl;
                m_quit = true;
                return;
            }
        }

        size_t bytes = get_entry_size(dsc) * dsc.count;
        m_stats.totalDataBytes += bytes;
    }

    // All the data arrays of the event class have been filled at this point.

    // Fill the raw histograms.
    size_t histoIndex = 0;

    for (const auto &userStorage: event->GetDataSourceStorages())
    {
        for (size_t paramIndex = 0; paramIndex < userStorage.size; paramIndex++)
        {
            double paramValue = userStorage.ptr[paramIndex];

            if (!std::isnan(paramValue))
                m_rawHistos.histos[eventIndex][histoIndex + paramIndex]->Fill(paramValue);
        }
        histoIndex += userStorage.size;
    }

    // Fill the ROOT tree for this event and run the user analysis code.
    m_eventTrees.at(eventIndex)->Fill();

    auto eventFunc = m_analysis.eventFunctions.at(eventIndex);

    if (eventFunc)
    {
        eventFunc(event);
    }
}

void ClientContext::endRun(const Message &msg, const json &info)
{
    cout << __FUNCTION__ << ": endRun info:" << endl << info.dump(2) << endl;

    if (m_analysis.endRun)
        m_analysis.endRun();

    auto treeOutFile = m_eventTrees.front()->GetCurrentFile();

    if (treeOutFile && !treeOutFile->IsZombie() && treeOutFile->IsOpen())
    {
        treeOutFile->Write();
    }

    if (m_histoOutFile)
    {
        //cout << "  Closing histo output file " << m_histoOutFile->GetName() << "..." << endl;
        cout << "  Flushing histo output file " << m_histoOutFile->GetName() << "..." << endl;

        m_histoOutFile->Write();
        //m_histoOutFile = {};
    }
    m_rawHistos = {};

    cout << "  HitCounts by event:" << endl;
    for (size_t ei=0; ei < m_stats.eventHits.size(); ei++)
    {
        cout << "    ei=" << ei << ", hits=" << m_stats.eventHits[ei] << endl;
    }

    cout << endl;

    m_stats.tEnd = RunStats::ClockType::now();
    std::chrono::duration<float> elapsed = m_stats.tEnd - m_stats.tStart;
    float elapsed_s = elapsed.count();
    float bytesPerSecond = m_stats.totalDataBytes / elapsed_s;
    float MBPerSecond = bytesPerSecond / (1024 * 1024);

    cout << " duration: " << elapsed_s << "s" << endl;

    cout << " data: "
        << m_stats.totalDataBytes << " bytes, "
        << m_stats.totalDataBytes / (1024 * 1024.0) << " MB"
        << endl;
    cout << " rate: "
        << bytesPerSecond << " B/s, "
        << MBPerSecond << " MB/s"
        << endl;
}

void ClientContext::error(const Message &msg, const std::exception &e)
{
    cout << "A protocol error occured: " << e.what() << endl;

    auto treeOutFile = m_eventTrees.front()->GetCurrentFile();

    if (treeOutFile && !treeOutFile->IsZombie() && treeOutFile->IsOpen())
    {
        cout << "Closing output file " << treeOutFile->GetName() << "..." << endl;
        treeOutFile->Write();
        m_rawHistos = {};
    }

    m_quit = true;
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
// client functionality
//
int client_main(
    const std::string &host,
    const std::string &port,
    const Options::Opt_t &clientOpts,
    const std::vector<std::string> &additionalArgs,
    const ssize_t rootMaxTreeSize)
{
    // Act as a client to the mvme event_server. This code tries to connect
    // to mvme, receive run data and produce ROOT files until canceled via
    // Ctrl-C.

    int retval = 0;

    ClientContext ctx(clientOpts);
    ctx.setAnalysisInitArgs(additionalArgs);
    ctx.setRootMaxTreeSize(rootMaxTreeSize);

    // A single message object, whose buffer is reused for each incoming
    // message.
    Message msg;
    int sockfd = -1;
    bool doQuit = false;

    while (!doQuit && !signal_received)
    {
        if (sockfd < 0)
        {
            cout << "Connecting to " << host << ":" << port << " ..." << endl;
        }

        // auto reconnect loop until connected or a signal arrived
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
                ctx.setHostAndPort(host, port);
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
            // Read messages from the network and let the context object
            // process them.
            read_message(sockfd, msg);
            ctx.handleMessage(msg);

            if ((clientOpts & Options::SingleRun) && msg.type == MessageType::EndRun)
            {
                doQuit = true;
            }
            else
            {
                doQuit = ctx.ShouldQuit();
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

    return retval;
}

//
// replay from ROOT file
//
int replay_main(
    const std::vector<std::string> &replayFilenames,
    const std::vector<std::string> &additionalArgs)
{
    if (replayFilenames.empty())
    {
        cerr << "Replay error: no input file given.\n";
        return 1;
    }

    const auto &firstFilename = replayFilenames.front();

    // Replay from ROOT file.
    TFile f(firstFilename.c_str(), "read");

    cout << ">>> Reading MVMERunInfo from " << firstFilename << endl;

    auto runInfoPtr = reinterpret_cast<MVMERunInfo *>(f.Get("MVMERunInfo"));

    if (!runInfoPtr)
    {
        cout << "Error: input file " << firstFilename << " does not contain an MVMERunInfo object"
            << endl;
        return 1;
    }

    auto runInfo = *runInfoPtr;

    std::string expName = runInfo["ExperimentName"];
    std::string expStructName = expName;
    std::string runId = runInfo["RunID"];
    std::string libName = "lib" + expName + "_mvme.so";

    cout << ">>> Run info: ExperimentName = " << expName << ", RunID=" << runId << endl;

    auto exp = compile_and_load_experiment_library(expName);

    if (!exp)
    {
        cout << "Error creating an instance of the experiment class '"
            << expStructName << "'" << endl;
        return 1;
    }

#if 0
    // Setup tree branch addresses to point to the experiment subobjects.
    auto eventTrees = exp->InitTrees(&f);

    if (eventTrees.size() != exp->GetNumberOfEvents())
    {
        cout << "Error: could not read experiment eventTrees from input file "
            << inputFilename << endl;
        return 1;
    }
#else
    auto eventTrees = exp->InitChains(replayFilenames);

    if (eventTrees.size() != exp->GetNumberOfEvents())
    {
        cout << "Error: could not read experiment eventTrees from input file "
            << firstFilename << endl;
        return 1;
    }
#endif

    // Load analysis
    void *handle = nullptr;
    UserAnalysis analysis = {};

    std::tie(handle, analysis) = load_user_analysis("analysis.so", exp.get());

    if (!handle)
    {
        cout << "Error loading analysis.so: " << dlerror() << endl;
        return 1;
    }

    if (analysis.init && !analysis.init(additionalArgs))
    {
        cout << "Analysis init function returned false, aborting" << endl;
        return 1;
    }

    // Create raw histograms in a separate output file

    std::string histoOutFilename = runId + "_histos.root";

    cout << "Opening histo output file " << histoOutFilename << endl;
    auto histoOutFile = std::make_unique<TFile>(histoOutFilename.c_str(), "recreate");

    if (histoOutFile->IsZombie() || !histoOutFile->IsOpen())
    {
        cout << "Error opening histo output file " << histoOutFilename
            << " for writing: " << strerror(histoOutFile->GetErrno()) << endl;
        return 1;
    }

    cout << "Creating raw histograms... ";
    auto rawHistos = make_raw_histograms(exp.get(), histoOutFile.get());

    cout << "created " << rawHistos.histoCount << " histograms."
        << " Total raw histo memory: " << (rawHistos.histoMem / (1024.0 * 1024.0))
        << " MB" << endl;

    // call custom user analysis code
    if (analysis.beginRun)
    {
        bool isReplay = true;
        analysis.beginRun(firstFilename, runId, isReplay);
    }

    // Replay tree data
    for (size_t eventIndex = 0; eventIndex < eventTrees.size(); eventIndex++)
    {
        auto tree = eventTrees[eventIndex];
        auto event = exp->GetEvent(eventIndex);
        auto analyzeFunc = analysis.eventFunctions[eventIndex];

        cout << "Replaying data from tree '" << tree->GetName() << "'..." << std::flush;

        const auto entryCount = tree->GetEntries();

        for (int64_t entryIndex = 0; entryIndex < entryCount; entryIndex++)
        {
            size_t histoIndex = 0;

            // Fills the event and its submodules with data read from the ROOT tree.
            tree->GetEntry(entryIndex);

            // Read values from the generated array members of the events
            // module classes and fill the raw histograms.
            for (const auto &userStorage: event->GetDataSourceStorages())
            {
                for (size_t paramIndex = 0; paramIndex < userStorage.size; paramIndex++)
                {
                    double paramValue = userStorage.ptr[paramIndex];

                    if (!std::isnan(paramValue))
                        rawHistos.histos[eventIndex][histoIndex + paramIndex]->Fill(paramValue);
                }
                histoIndex += userStorage.size;
            }

            if (analyzeFunc)
                analyzeFunc(event);
        }

        cout << " read " << entryCount << " entries." << endl;
    }

    if (analysis.endRun)
        analysis.endRun();

    cout << "Closing histo output file " << histoOutFile->GetName() << "..." << endl;

    histoOutFile->Write();

    return 0;
}

//
// main
//
int main(int argc, char *argv[])
{
    std::string host = "localhost";
    std::string port = "13801";
    std::string analysisArg;
    ssize_t rootMaxTreeSize = 100000000000LL;
    bool showHelp = false;

    using Opts = Options;
    Opts::Opt_t clientOpts = {};

    while (true)
    {
        static struct option long_options[] =
        {
            { "single-run", no_argument, nullptr, 0 },
            { "show-stream-info", no_argument, nullptr, 0 },
            { "detect-split-runs", no_argument, nullptr, 0 },
            { "help", no_argument, nullptr, 0 },
            { "host", required_argument, nullptr, 0 },
            { "port", required_argument, nullptr, 0 },
            { "root-max-tree-size", required_argument, nullptr, 0 },
            { "replay", no_argument, nullptr, 0 },
            { "analysis-args", required_argument, nullptr, 0 },
            { nullptr, 0, nullptr, 0 },
        };

        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c == -1) break;

        switch (c)
        {
            case '?':
                // Unrecognized option
                return 1;

            case 0:
                // long options
                {
                    std::string opt_name(long_options[option_index].name);

                    if (opt_name == "single-run") clientOpts |= Opts::SingleRun;
                    else if (opt_name == "show-stream-info") clientOpts |= Opts::ShowStreamInfo;
                    else if (opt_name == "detect-split-runs") clientOpts |= Opts::DetectSplitRuns;
                    else if (opt_name == "host") host = optarg;
                    else if (opt_name == "port") port = optarg;
                    else if (opt_name == "help") showHelp = true;
                    else if (opt_name == "replay") clientOpts |= Opts::ReplayMode;
                    else if (opt_name == "analysis-args") analysisArg = optarg;
                    else if (opt_name == "root-max-tree-size")
                    {
                        rootMaxTreeSize = std::atoll(optarg);

                        if (rootMaxTreeSize <= 0)
                        {
                            cerr << "Error: invalid value given to --root-max-tree-size\n";
                            return 1;
                        }
                    }
                    else
                    {
                        assert(!"unhandled long option");
                    }
                }
        }
    }

    if (showHelp)
    {
        cout << "* Usage as a mvme client:\n"
                "  mvme_root_client [--single-run] [--root-max-tree-size=<bytes>] [--host=localhost] [--port=13801]\n"
                "                   [--analysis-args=<args>] [--show-stream-info] [--detect-split-runs]\n"
                "\n"
            ;

        cout << "  In this mode the program connects to and receives data from a mvme process." << endl
             << endl
             << "  If 'single-run' is set the program will exit after receiving\n"
             << "  data from one run. Otherwise it will wait for the next run to\n"
             << "  start.\n"
             << "\n"
             << "  --root-max-tree-size Can be used to specify the maximum number of bytes per output ROOT file.\n"
             << "                       The argument is passed to TTree::SetMaxTreeSize(). Default is 100000000000LL (100GB).\n"
             << "\n"
             << "  --show-stream-info   If present detailed information about the structure of the incoming data stream\n"
                "                       is shown at the start of each run.\n"
                "\n"
                "  --detect-split-runs  If enabled the client will try to detect if data from multiple listfiles belongs to\n"
                "                       the same DAQ run. If true the data will be accumulated into the same output ROOT files.\n"
                "\n"
            ;

        cout << "Usage when replaying from ROOT file: " << argv[0] << " --replay <input ROOT file...> [--analysis-args=<args>]"<< endl << endl;

        cout << "  In this mode data is read from previously recorded 'raw' ROOT files.\n"
             << "  Example usage to replay from a list of files: mvme_root_client --replay $(ls -tr *raw*.root)\n\n"
             << "  Using 'ls' to sort the files by modification time ensures that the first file in the list\n"
             << "  contains the MVMERunInfo data required for the replay.\n"
             << endl;

        cout << "The optional '--analysis-args' can be used to pass arguments to the user analysis code." << endl
             << "It is interpreted as a space-separated list of arguments." << endl
             << endl;

        return 0;
    }

    // Split the --analysis-args string into whitespace separated parts and
    // collect them.

    std::vector<std::string> analysisArgs;

    {
        std::istringstream ss(analysisArg);
        std::string arg;
        while (ss >> arg)
            analysisArgs.emplace_back(arg);
    }

    std::vector<std::string> replayFilenames;

    // Collect files to replay from.
    // Note: optind is set by getopt_long().
    if (clientOpts & Opts::ReplayMode)
    {
        for (int i = optind; i < argc; i++)
            replayFilenames.emplace_back(argv[i]);

        if (replayFilenames.empty())
        {
            cerr << "Error: replay mode requested but no input files specified.\n";
            return 1;
        }
    }

    //
    // More setup and client lib init
    //
    setup_signal_handlers();

    if (int res = mvme::event_server::lib_init() != 0)
    {
        cerr << "mvme::event_server::lib_init() failed with code " << res << endl;
        return 1;
    }

    int retval = 0;

    if (!(clientOpts & Opts::ReplayMode))
    {
        retval = client_main(host, port, clientOpts, analysisArgs, rootMaxTreeSize);
    }
    else
    {
        retval = replay_main(replayFilenames, analysisArgs);
    }

    mvme::event_server::lib_shutdown();
    return retval;
}

#include <fstream>
#include <iostream>
#include <regex>
#include <set>

#include <lyra/lyra.hpp>
#include <QFile>
#include <QJsonDocument>
#include <spdlog/spdlog.h>

#include "analysis/analysis.h"

using namespace analysis;

void analysis_to_dot(std::ostream &out, const analysis::Analysis &ana)
{
    const auto FontName = "Bitstream Vera Sans";

    out << "strict digraph {" << std::endl;
    out << "rankdir=LR" << std::endl;
    out << "id=OuterGraph" << std::endl;
    out << fmt::format("fontname=\"{}\"", FontName) << std::endl;

    auto allObjects = ana.getAllObjects();

    std::set<QUuid> allObjectIds;

    // Set of all registered objects. Used to check if ids referened by
    // directories still exist.
    for (const auto &obj: allObjects)
        allObjectIds.insert(obj->getId());

    // Non-directory  objects nodes.
    for (const auto &obj: allObjects)
    {
        // skip directories
        if (dynamic_cast<const Directory *>(obj.get()))
            continue;

        auto label = obj->objectName().toStdString();
        label = std::regex_replace(label, std::regex("&"), "&amp;");
        label = std::regex_replace(label, std::regex("\""), "&quot;");
        label = std::regex_replace(label, std::regex(">"), "&gt;");
        label = std::regex_replace(label, std::regex("<"), "&lt;");

        if (auto pipeSource = dynamic_cast<const PipeSourceInterface *>(obj.get()))
        {
            label = fmt::format("<b>{}</b><br/>{}", pipeSource->getDisplayName().toStdString(), label);
        }

        out << fmt::format("\"{}\" [id=\"{}\" label=<{}>, fontname=\"{}\"]",
                           obj->getId().toString().toStdString(),
                           obj->getId().toString().toStdString(),
                           label,
                           FontName
                          ) << std::endl;
    }

    // Directory clusters
    for (const auto &obj: allObjects)
    {
        if (auto dir = dynamic_cast<const Directory *>(obj.get()))
        {
            const auto &memberIds = dir->getMembers();

            if (memberIds.empty())
                continue;


            //out << fmt::format("subgraph \"cluster{}\" {{", dir->getId().toString().toStdString()) << std::endl;
            out << fmt::format("subgraph \"cluster{}\" {{", dir->objectName().toStdString()) << std::endl;
            out << fmt::format("label=\"{}\"", dir->objectName().toStdString()) << std::endl;
            out << fmt::format("id=\"{}\"", dir->getId().toString().toStdString()) << std::endl;

            for (const auto &id: memberIds)
            {
                if (allObjectIds.count(id))
                    out << fmt::format("\"{}\";", id.toString().toStdString()) << std::endl;
            }

            out << "};" << std::endl;
        }
    }

    // Pipe edges between objects
    for (const auto &obj: allObjects)
    {
        if (auto pipeSource = dynamic_cast<const PipeSourceInterface *>(obj.get()))
        {
            for (s32 oi = 0; oi < pipeSource->getNumberOfOutputs(); ++oi)
            {
                auto outPipe = pipeSource->getOutput(oi);
                auto destSlots = outPipe->getDestinations();

                for (const auto &destSlot: destSlots)
                {
                    const auto &destOp = destSlot->parentOperator;
                    out << fmt::format("\"{}\" -> \"{}\"",
                                       pipeSource->getId().toString().toStdString(),
                                       destOp->getId().toString().toStdString())
                        << std::endl;
                }
            }
        }
    }

    // Condition links
    auto condLinks = ana.getConditionLinks();
    for (const auto &op: condLinks.keys())
    {
        if (!allObjects.contains(op))
            continue;

        for (const auto &cond: condLinks[op])
        {
            if (!allObjects.contains(cond))
                continue;

            out << fmt::format("\"{}\" -> \"{}\" [arrowhead=diamond, color=blue]",
                               op->getId().toString().toStdString(),
                               cond->getId().toString().toStdString())
                << std::endl;
        }
    }

    out << "}" << std::endl;
}

int main(int argc, char *argv[])
{
    bool opt_showHelp = false;
    std::string opt_analysisInputFile;
    std::string opt_dotOutputFile;

    auto cli
        = lyra::help(opt_showHelp)
        | lyra::arg(opt_analysisInputFile, "analysisInputFile")
            ("mvme analysis input file").required()
        | lyra::arg(opt_dotOutputFile, "dotOutputFile")
            ("graphviz DOT output file").required()
        ;

    auto cliParseResult = cli.parse({ argc, argv });

    if (!cliParseResult)
    {
        spdlog::error("Error parsing command line arguments: {}", cliParseResult.errorMessage());
        return 1;
    }

    if (opt_showHelp)
    {
        std::cout << cli << std::endl;
        return 0;
    }

    auto readResult = read_analysis_config_from_file(QString::fromStdString(opt_analysisInputFile));
    auto analysis = std::move(readResult.first);

    if (!analysis)
        return 1;

    std::ofstream os(opt_dotOutputFile);
    analysis_to_dot(os, *analysis);

    return 0;
}

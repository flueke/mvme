#include <fstream>
#include <iostream>
#include <regex>
#include <set>

#include <lyra/lyra.hpp>
#include <QFile>
#include <QJsonDocument>
#include <spdlog/spdlog.h>

#include "analysis/analysis.h"
#include "analysis/object_visitor.h"

std::shared_ptr<analysis::Analysis> read_analysis_from_file(const std::string &filename)
{
    QFile inFile(QString::fromStdString(filename));

    if (!inFile.open(QIODevice::ReadOnly))
    {
        spdlog::error("Error opening input file {} for reading: {}", filename, inFile.errorString().toStdString());
        return {};
    }

    auto doc = QJsonDocument::fromJson(inFile.readAll());
    auto json = doc.object()["AnalysisNG"].toObject();
    auto result = std::make_shared<analysis::Analysis>();
    VMEConfig emptyVmeConfig{};

    if (auto ec = result->read(json, &emptyVmeConfig))
    {
        spdlog::error("Error loading analysis from {}: {}", filename, ec.message());
        return {};
    }

    return result;
}

using namespace analysis;

class DotVisitor: public analysis::ObjectVisitor
{
    public:
        void visit(SourceInterface *source) override
        {
        }

        void visit(OperatorInterface *op) override
        {
        }

        void visit(SinkInterface *sink) override
        {
        }

        void visit(ConditionInterface *cond) override
        {
        }

        void visit(Directory *dir) override
        {
        }
};

void analysis_to_dot(std::ostream &out, const analysis::Analysis &ana)
{
    const auto FontName = "Bitstream Vera Sans";

    out << "strict digraph {" << std::endl;
    out << "rankdir=LR" << std::endl;
    out << fmt::format("fontname=\"{}\"", FontName) << std::endl;

    auto allObjects = ana.getAllObjects();

    std::set<QUuid> allObjectIds;

    for (const auto &obj: allObjects)
        allObjectIds.insert(obj->getId());

    for (const auto &obj: allObjects)
    {
        if (auto dir = dynamic_cast<const Directory *>(obj.get()))
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

        out << fmt::format("\"{}\" [label=<{}>, fontname=\"{}\"]",
                           obj->getId().toString().toStdString(),
                           label,
                           FontName
                          ) << std::endl;
    }

    for (const auto &obj: allObjects)
    {
        if (auto dir = dynamic_cast<const Directory *>(obj.get()))
        {
            const auto &memberIds = dir->getMembers();

            if (memberIds.empty())
                continue;


            out << fmt::format("subgraph \"cluster{}\" {{", dir->getId().toString().toStdString()) << std::endl;
            out << fmt::format("label=\"{}\"", dir->objectName().toStdString()) << std::endl;

            for (const auto &id: memberIds)
            {
                if (allObjectIds.count(id))
                    out << fmt::format("\"{}\";", id.toString().toStdString()) << std::endl;
            }

            out << "};" << std::endl;
        }
    }

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

#if 0
    for (const auto &source: ana.getSources())
    {
        out << fmt::format("\"{}\" [label=\"{}\"]",
                           source->getId().toString().toStdString(),
                           source->objectName().toStdString()
                          ) << std::endl;
    }

    for (const auto &op: ana.getOperators())
    {
        out << fmt::format("\"{}\" [label=\"{}\"]",
                           op->getId().toString().toStdString(),
                           op->objectName().toStdString()
                          ) << std::endl;
    }
#endif




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

    auto analysis = read_analysis_from_file(opt_analysisInputFile);

    if (!analysis)
        return 1;

    std::ofstream os(opt_dotOutputFile);
    analysis_to_dot(os, *analysis);

    return 0;
}

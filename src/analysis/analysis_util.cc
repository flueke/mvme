#include "analysis_util.h"

#include <QDir>
#include <QJsonDocument>
#include <QMessageBox>
#include <QRegularExpression>

#include "../template_system.h"

namespace analysis
{

QVector<std::shared_ptr<Extractor>> get_default_data_extractors(const QString &moduleTypeName)
{
    QVector<std::shared_ptr<Extractor>> result;

    QDir moduleDir(vats::get_module_path(moduleTypeName));
    QFile filtersFile(moduleDir.filePath("analysis/default_filters.analysis"));

    if (filtersFile.open(QIODevice::ReadOnly))
    {
        auto doc = QJsonDocument::fromJson(filtersFile.readAll());
        Analysis filterAnalysis;
        /* Note: This does not do proper config conversion as no VMEConfig is
         * passed to Analysis::read().  It is assumed that the default filters
         * shipped with mvme are in the latest format (or a format that does
         * not need a VMEConfig to be upconverted). */
        auto readResult = filterAnalysis.read(doc.object()[QSL("AnalysisNG")].toObject());

        if (readResult)
        {
            for (auto source: filterAnalysis.getSources())
            {
                auto extractor = std::dynamic_pointer_cast<Extractor>(source);
                if (extractor)
                {
                    result.push_back(extractor);
                }
            }

            qSort(result.begin(), result.end(), [](const auto &a, const auto &b) {
                return a->objectName() < b->objectName();
            });
        }
        else
        {
            readResult.errorData["Source file"] = filtersFile.fileName();
            QMessageBox::critical(nullptr,
                                  QSL("Error loading default filters"),
                                  readResult.toRichText());
        }
    }

    return result;
}

//
// Dependencies returned as OperatorInterface*
//

QSet<OperatorInterface *> collect_dependent_operators(PipeSourceInterface *startObject,
                                                      CollectFlags::Flag flags)
{
    QSet<OperatorInterface *> result;

    collect_dependent_operators(startObject, result, flags);

    return result;
}

QSet<OperatorInterface *> collect_dependent_operators(const PipeSourcePtr &startObject,
                                                      CollectFlags::Flag flags)
{
    return collect_dependent_operators(startObject.get(), flags);
}

void collect_dependent_operators(PipeSourceInterface *startObject,
                                 QSet<OperatorInterface *> &result,
                                 CollectFlags::Flag flags)
{
    for (s32 oi = 0; oi < startObject->getNumberOfOutputs(); oi++)
    {
        auto outPipe = startObject->getOutput(oi);

        for (auto destSlot: outPipe->getDestinations())
        {
            if (auto op = destSlot->parentOperator)
            {
                auto test = flags & CollectFlags::All;

                if (test == CollectFlags::All
                    || (test == CollectFlags::Operators && !is_sink(op))
                    || (test == CollectFlags::Sinks && is_sink(op)))
                {
                    result.insert(op);
                    collect_dependent_operators(op, result, flags);
                }
            }
        }
    }
}

void collect_dependent_operators(const PipeSourcePtr &startObject,
                                 QSet<OperatorInterface *> &result,
                                 CollectFlags::Flag flags)
{
    return collect_dependent_operators(startObject.get(), result, flags);
}

//
// Dependencies returned as PipeSourceInterface*
//

QSet<PipeSourceInterface *> collect_dependent_objects(PipeSourceInterface *startObject,
                                                      CollectFlags::Flag flags)
{
    QSet<PipeSourceInterface *> result;

    for (auto &op: collect_dependent_operators(startObject, flags))
    {
        result.insert(qobject_cast<PipeSourceInterface *>(op));
    }

    return result;
}

QSet<PipeSourceInterface *> collect_dependent_objects(const PipeSourcePtr &startObject,
                                                      CollectFlags::Flag flags)
{
    return collect_dependent_objects(startObject.get(), flags);
}

void generate_new_object_ids(const AnalysisObjectVector &objects)
{
    for (auto &obj: objects)
    {
        obj->setId(QUuid::createUuid());
    }
}

void generate_new_object_ids(Analysis *analysis)
{
    generate_new_object_ids(analysis->getAllObjects());
}

QSet<QString> get_object_names(const AnalysisObjectVector &objects)
{
    QSet<QString> result;

    for (const auto &obj: objects)
    {
        result.insert(obj->objectName());
    }

    return result;
}

static const char *CloneNameRegexp = "^.+(?<suffix>\\ Copy(?<counter>\\d+)?)$";

QString make_clone_name(const QString &currentName, const QSet<QString> &allNames)
{
    if (currentName.isEmpty()) return currentName;

    QString result = currentName;
    u32 counter = 0;

    while (allNames.contains(result))
    {
        static const QRegularExpression re(CloneNameRegexp);
        auto match = re.match(result);
        assert(match.hasMatch());

        if (match.capturedRef(QSL("suffix")).isNull())
        {
            result += QSL(" Copy");
        }
        else if (match.capturedRef(QSL("counter")).isNull())
        {
            result += QString::number(counter);
        }
        else
        {
            auto sref = match.capturedRef(QSL("counter"));
            counter = sref.toULong() + 1;

            result.replace(match.capturedStart(QSL("counter")),
                           match.capturedLength(QSL("counter")),
                           QString::number(counter));

            if (counter == std::numeric_limits<u32>::max())
                break;
        }
    }

    return result;
}

} // namespace analysis

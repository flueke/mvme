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
    QHash<QUuid, QUuid> oldToNewIds;

    for (auto &obj: objects)
    {
        auto oldId = obj->getId();
        auto newId = QUuid::createUuid();
        obj->setId(newId);
        oldToNewIds.insert(oldId, newId);
    }

    /* Rewrite directory member lists. */
    for (auto &obj: objects)
    {
        if (auto dir = std::dynamic_pointer_cast<Directory>(obj))
        {
            auto oldMembers = dir->getMembers();
            Directory::MemberContainer newMembers;
            newMembers.reserve(oldMembers.size());

            for (const auto &oldId: oldMembers)
            {
                if (!oldToNewIds.value(oldId).isNull())
                {
                    newMembers.push_back(oldToNewIds.value(oldId));
                }
            }

            dir->setMembers(newMembers);
        }
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

NamesByMetaObject group_object_names_by_metatype(const AnalysisObjectVector &objects)
{
    NamesByMetaObject result;

    for (auto &obj: objects)
    {
        result[obj->metaObject()].insert(obj->objectName());
    }

    return result;
}

// Note: the first '+?' is the ungreedy version of '+'
// A great regex debugging helper can be found here: https://regex101.com/
static const char *CloneNameRegexp = "^.+?(?<suffix>\\ Copy(?<counter>\\d+)?)$";

QString make_clone_name(const QString &currentName, const StringSet &allNames)
{
    if (currentName.isEmpty()) return currentName;

    QString result = currentName;

    while (allNames.contains(result))
    {
        static const QRegularExpression re(CloneNameRegexp);
        auto match = re.match(result);

        if (!match.hasMatch() || match.capturedRef(QSL("suffix")).isNull())
        {
            result += QSL(" Copy");
        }
        else if (match.capturedRef(QSL("counter")).isNull())
        {
            result += QString::number(1);
        }
        else
        {
            auto sref = match.capturedRef(QSL("counter"));
            ulong counter = sref.toULong() + 1;

            result.replace(match.capturedStart(QSL("counter")),
                           match.capturedLength(QSL("counter")),
                           QString::number(counter));

            if (counter == std::numeric_limits<u32>::max())
                break;
        }
    }

    return result;
}

//
// AnalysisSignalWrapper
//

AnalysisSignalWrapper::AnalysisSignalWrapper(QObject *parent)
    : QObject(parent)
{ }

AnalysisSignalWrapper::AnalysisSignalWrapper(Analysis *analysis,
                                                 QObject *parent)
    : QObject(parent)
{
    setAnalysis(analysis);
}

void AnalysisSignalWrapper::setAnalysis(Analysis *analysis)
{
    QObject::connect(analysis, &Analysis::modified,
                     this, &AnalysisSignalWrapper::modified);

    QObject::connect(analysis, &Analysis::modifiedChanged,
                     this, &AnalysisSignalWrapper::modifiedChanged);

    QObject::connect(analysis, &Analysis::dataSourceAdded,
                     this, &AnalysisSignalWrapper::dataSourceAdded);

    QObject::connect(analysis, &Analysis::dataSourceRemoved,
                     this, &AnalysisSignalWrapper::dataSourceRemoved);

    QObject::connect(analysis, &Analysis::operatorAdded,
                     this, &AnalysisSignalWrapper::operatorAdded);

    QObject::connect(analysis, &Analysis::operatorRemoved,
                     this, &AnalysisSignalWrapper::operatorRemoved);

    QObject::connect(analysis, &Analysis::directoryAdded,
                     this, &AnalysisSignalWrapper::directoryAdded);

    QObject::connect(analysis, &Analysis::directoryRemoved,
                     this, &AnalysisSignalWrapper::directoryRemoved);

    QObject::connect(analysis, &Analysis::conditionLinkApplied,
                     this, &AnalysisSignalWrapper::conditionLinkApplied);

    QObject::connect(analysis, &Analysis::conditionLinkCleared,
                     this, &AnalysisSignalWrapper::conditionLinkCleared);
}

} // namespace analysis

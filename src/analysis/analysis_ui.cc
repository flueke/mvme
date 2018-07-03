/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "analysis_ui.h"
#include "analysis_ui_p.h"
#include "analysis_serialization.h"
#include "analysis_util.h"
#include "data_extraction_widget.h"
#include "analysis_info_widget.h"
#include "a2_adapter.h"
#ifdef MVME_ENABLE_HDF5
#include "analysis_session.h"
#endif
#include "listfilter_extractor_dialog.h"
#include "expression_operator_dialog.h"

#include "histo1d_widget.h"
#include "histo2d_widget.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"
#include "mvme_stream_worker.h"
#include "rate_monitor_widget.h"
#include "treewidget_utils.h"
#include "util/counters.h"
#include "util/strings.h"
#include "vme_analysis_common.h"
#include "vme_config_ui.h"

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDesktopServices>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QtConcurrent>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>


#include <QJsonObject>

namespace analysis
{

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
    DataRole_ParameterIndex,
    DataRole_HistoAddress,
};

enum NodeType
{
    NodeType_Module = QTreeWidgetItem::UserType,
    NodeType_Source,
    NodeType_Operator,
    NodeType_OutputPipe,
    NodeType_OutputPipeParameter,

    NodeType_Histo1DSink,
    NodeType_Histo2DSink,
    NodeType_Sink,          // Sinks that are not handled specifically

    NodeType_Histo1D,

    NodeType_Directory,

    NodeType_MaxNodeType
};

template<typename T>
T *get_pointer(QTreeWidgetItem *node, s32 dataRole = DataRole_Pointer)
{
    return node ? reinterpret_cast<T *>(node->data(0, dataRole).value<void *>()) : nullptr;
}

inline QObject *get_qobject(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
{
    return get_pointer<QObject>(node, dataRole);
}

AnalysisObjectPtr get_analysis_object(QTreeWidgetItem *node, s32 dataRole = DataRole_Pointer)
{
    auto qo = get_qobject(node, dataRole);
    qDebug() << qo;
    auto ao = qobject_cast<AnalysisObject *>(qo);

    return ao ? ao->shared_from_this() : AnalysisObjectPtr();
}

template<typename T>
std::shared_ptr<T> get_shared_analysis_object(QTreeWidgetItem *node,
                                              s32 dataRole = DataRole_Pointer)
{
    auto objPtr = get_analysis_object(node, dataRole);
    return std::dynamic_pointer_cast<T>(objPtr);
}

/* QTreeWidgetItem does not support setting separate values for Qt::DisplayRole and
 * Qt::EditRole. This subclass removes this limitation.
 *
 * The implementation keeps track of whether DisplayRole and EditRole data have been set.
 * If specific data for the requested role is available it will be returned, otherwise the
 * other roles data is returned.
 *
 * This subclass also implements custom (numeric) sorting behavior for output pipe
 * parameter and histogram index values in operator<().
 *
 * NOTE: Do not use for the headerview as that requires special handling which needs
 * access to the private QTreeModel class.
 * Link to the Qt code: https://code.woboq.org/qt5/qtbase/src/widgets/itemviews/qtreewidget.cpp.html#_ZN15QTreeWidgetItem7setDataEiiRK8QVariant
 */
class TreeNode: public QTreeWidgetItem
{
    public:
        TreeNode(int type = QTreeWidgetItem::Type)
            : QTreeWidgetItem(type)
        { }

        TreeNode(const QStringList &strings, int type = QTreeWidgetItem::Type)
            : QTreeWidgetItem(type)
        {
            for (int i = 0; i < strings.size(); i++)
            {
                setText(i, strings.at(i));
            }
        }

        virtual void setData(int column, int role, const QVariant &value) override
        {
            if (column < 0)
                return;

            if (role != Qt::DisplayRole && role != Qt::EditRole)
            {
                QTreeWidgetItem::setData(column, role, value);
                return;
            }

            if (column >= m_columnData.size())
            {
                m_columnData.resize(column + 1);
            }

            auto &entry = m_columnData[column];

            switch (role)
            {
                case Qt::DisplayRole:
                    if (entry.displayData != value)
                    {
                        entry.displayData = value;
                        entry.flags |= Data::HasDisplayData;
                        emitDataChanged();
                    }
                    break;

                case Qt::EditRole:
                    if (entry.editData != value)
                    {
                        entry.editData = value;
                        entry.flags |= Data::HasEditData;
                        emitDataChanged();
                    }
                    break;

                InvalidDefaultCase;
            }
        }

        virtual QVariant data(int column, int role) const override
        {
            if (role != Qt::DisplayRole && role != Qt::EditRole)
            {
                return QTreeWidgetItem::data(column, role);
            }

            if (0 <= column && column < m_columnData.size())
            {
                const auto &entry = m_columnData[column];

                switch (role)
                {
                    case Qt::DisplayRole:
                        if (entry.flags & Data::HasDisplayData)
                            return entry.displayData;
                        return entry.editData;

                    case Qt::EditRole:
                        if (entry.flags & Data::HasEditData)
                            return entry.editData;
                        return entry.displayData;

                    InvalidDefaultCase;
                }
            }

            return QVariant();
        }

        // Custom sorting for numeric columns
        virtual bool operator<(const QTreeWidgetItem &other) const override
        {
            if (type() == other.type() && treeWidget() && treeWidget()->sortColumn() == 0)
            {
                if (type() == NodeType_OutputPipeParameter)
                {
                    s32 thisAddress  = data(0, DataRole_ParameterIndex).toInt();
                    s32 otherAddress = other.data(0, DataRole_ParameterIndex).toInt();
                    return thisAddress < otherAddress;
                }
                else if (type() == NodeType_Histo1D)
                {
                    s32 thisAddress  = data(0, DataRole_HistoAddress).toInt();
                    s32 otherAddress = other.data(0, DataRole_HistoAddress).toInt();
                    return thisAddress < otherAddress;
                }
            }
            return QTreeWidgetItem::operator<(other);
        }

    private:
        struct Data
        {
            static const u8 HasDisplayData = 1u << 0;
            static const u8 HasEditData    = 1u << 1;
            QVariant displayData;
            QVariant editData;
            u8 flags = 0u;
        };

        QVector<Data> m_columnData;
};

template<typename T>
TreeNode *make_node(T *data, int type = QTreeWidgetItem::Type)
{
    auto ret = new TreeNode(type);
    ret->setData(0, DataRole_Pointer, QVariant::fromValue(static_cast<void *>(data)));
    ret->setFlags(ret->flags() & ~(Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled));
    return ret;
}

inline TreeNode *make_module_node(ModuleConfig *mod)
{
    auto node = make_node(mod, NodeType_Module);
    node->setText(0, mod->objectName());
    node->setIcon(0, QIcon(":/vme_module.png"));
    node->setFlags(node->flags() | Qt::ItemIsDropEnabled);
    return node;
};

inline TreeNode *make_datasource_node(SourceInterface *source)
{
    auto sourceNode = make_node(source, NodeType_Source);
    sourceNode->setData(0, Qt::DisplayRole, source->objectName());
    sourceNode->setData(0, Qt::EditRole, source->objectName());
    sourceNode->setFlags(sourceNode->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);

    auto icon = QIcon(":/data_filter.png");

    if (qobject_cast<ListFilterExtractor *>(source))
    {
        icon = QIcon(":/listfilter.png");
    }

    sourceNode->setIcon(0, icon);

    Q_ASSERT_X(source->getNumberOfOutputs() == 1,
               "make_datasource_node",
               "data sources with multiple output pipes not supported");

    if (source->getNumberOfOutputs() == 1)
    {
        Pipe *outputPipe = source->getOutput(0);
        s32 addressCount = outputPipe->parameters.size();

        for (s32 address = 0; address < addressCount; ++address)
        {
            auto addressNode = make_node(outputPipe, NodeType_OutputPipeParameter);
            addressNode->setData(0, DataRole_ParameterIndex, address);
            addressNode->setText(0, QString::number(address));
            sourceNode->addChild(addressNode);
        }
    }

    return sourceNode;
}

static QIcon make_operator_icon(OperatorInterface *op)
{
    // operators
    if (qobject_cast<CalibrationMinMax *>(op))
        return QIcon(":/operator_calibration.png");

    if (qobject_cast<Difference *>(op))
        return QIcon(":/operator_difference.png");

    if (qobject_cast<PreviousValue *>(op))
        return QIcon(":/operator_previous.png");

    if (qobject_cast<Sum *>(op))
        return QIcon(":/operator_sum.png");

    if (qobject_cast<ExpressionOperator *>(op))
        return QIcon(":/function.png");

    // sinks
    if (qobject_cast<Histo1DSink *>(op))
        return QIcon(":/hist1d.png");

    if (qobject_cast<Histo2DSink *>(op))
        return QIcon(":/hist2d.png");

    if (qobject_cast<RateMonitorSink *>(op))
        return QIcon(":/rate_monitor_sink.png");

    // catchall for sinks
    if (qobject_cast<SinkInterface *>(op))
        return QIcon(":/sink.png");

    return QIcon(":/operator_generic.png");
}

inline TreeNode *make_histo1d_node(Histo1DSink *sink)
{
    auto node = make_node(sink, NodeType_Histo1DSink);

    node->setData(0, Qt::EditRole, sink->objectName());
    node->setData(0, Qt::DisplayRole, QString("<b>%1</b> %2").arg(
            sink->getShortName(),
            sink->objectName()));


    node->setIcon(0, make_operator_icon(sink));
    node->setFlags(node->flags() | Qt::ItemIsEditable);

    if (sink->m_histos.size() > 0)
    {
        for (s32 addr = 0; addr < sink->m_histos.size(); ++addr)
        {
            auto histo = sink->m_histos[addr].get();
            auto histoNode = make_node(histo, NodeType_Histo1D);
            histoNode->setData(0, DataRole_HistoAddress, addr);
            histoNode->setText(0, QString::number(addr));
            node->setIcon(0, make_operator_icon(sink));

            node->addChild(histoNode);
        }
    }

    return node;
};

inline TreeNode *make_histo2d_node(Histo2DSink *sink)
{
    auto node = make_node(sink, NodeType_Histo2DSink);
    node->setData(0, Qt::EditRole, sink->objectName());
    node->setData(0, Qt::DisplayRole, QString("<b>%1</b> %2").arg(
            sink->getShortName(),
            sink->objectName()));
    node->setIcon(0, make_operator_icon(sink));
    node->setFlags(node->flags() | Qt::ItemIsEditable);

    return node;
}

inline TreeNode *make_sink_node(SinkInterface *sink)
{
    auto node = make_node(sink, NodeType_Sink);
    node->setData(0, Qt::EditRole, sink->objectName());
    node->setData(0, Qt::DisplayRole, QString("<b>%1</b> %2").arg(
            sink->getShortName(),
            sink->objectName()));
    node->setIcon(0, make_operator_icon(sink));
    node->setFlags(node->flags() | Qt::ItemIsEditable);

    return node;
}

inline TreeNode *make_operator_node(OperatorInterface *op)
{
    auto result = make_node(op, NodeType_Operator);

    result->setData(0, Qt::EditRole, op->objectName());
    result->setData(0, Qt::DisplayRole, QString("<b>%1</b> %2").arg(
            op->getShortName(),
            op->objectName()));

    result->setIcon(0, make_operator_icon(op));
    result->setFlags(result->flags() | Qt::ItemIsEditable);

    // outputs
    for (s32 outputIndex = 0;
         outputIndex < op->getNumberOfOutputs();
         ++outputIndex)
    {
        Pipe *outputPipe = op->getOutput(outputIndex);
        s32 outputParamSize = outputPipe->parameters.size();

        auto pipeNode = make_node(outputPipe, NodeType_OutputPipe);
        pipeNode->setText(0, QString("#%1 \"%2\" (%3 elements)")
                          .arg(outputIndex)
                          .arg(op->getOutputName(outputIndex))
                          .arg(outputParamSize)
                         );
        result->addChild(pipeNode);

        for (s32 paramIndex = 0; paramIndex < outputParamSize; ++paramIndex)
        {
            auto paramNode = make_node(outputPipe, NodeType_OutputPipeParameter);
            paramNode->setData(0, DataRole_ParameterIndex, paramIndex);
            paramNode->setText(0, QString("[%1]").arg(paramIndex));

            pipeNode->addChild(paramNode);
        }
    }

    return result;
};

inline TreeNode *make_directory_node(const DirectoryPtr &dir)
{
    auto result = make_node(dir.get(), NodeType_Directory);

    result->setText(0, dir->objectName());
    result->setIcon(0, QIcon(QSL(":/folder_orange.png")));
    result->setFlags(result->flags()
                     | Qt::ItemIsDropEnabled
                     | Qt::ItemIsDragEnabled
                     | Qt::ItemIsEditable
                     );

    return result;
}

struct Histo1DWidgetInfo
{
    QVector<std::shared_ptr<Histo1D>> histos;
    s32 histoAddress;
    std::shared_ptr<CalibrationMinMax> calib;
    std::shared_ptr<Histo1DSink> sink;
};

Histo1DWidgetInfo getHisto1DWidgetInfoFromNode(QTreeWidgetItem *node)
{
    QTreeWidgetItem *sinkNode = nullptr;
    Histo1DWidgetInfo result;

    switch (node->type())
    {
        case NodeType_Histo1D:
            {
                Q_ASSERT(node->parent() && node->parent()->type() == NodeType_Histo1DSink);
                sinkNode = node->parent();
                result.histoAddress = node->data(0, DataRole_HistoAddress).toInt();
            } break;

        case NodeType_Histo1DSink:
            {
                sinkNode = node;
                result.histoAddress = 0;
            } break;

        InvalidDefaultCase;
    }

    auto histoSink = get_pointer<Histo1DSink>(sinkNode);
    result.histos = histoSink->m_histos;
    result.sink = std::dynamic_pointer_cast<Histo1DSink>(histoSink->shared_from_this());

    // Check if the histosinks input is a CalibrationMinMax
    if (Pipe *sinkInputPipe = histoSink->getSlot(0)->inputPipe)
    {
        if (auto calibRaw = qobject_cast<CalibrationMinMax *>(sinkInputPipe->getSource()))
        {
            result.calib = std::dynamic_pointer_cast<CalibrationMinMax>(calibRaw->shared_from_this());
        }
    }

    return result;
}

//
// ObjectTree and subclasses
//

// MIME types for drag and drop operations

// SourceInterface objects only
static const QString DataSourceIdListMIMEType = QSL("application/x-mvme-analysis-datasource-id-list");

// Non datasource operators and directories
static const QString OperatorIdListMIMEType = QSL("application/x-mvme-analysis-operator-id-list");

// Sink-type operators and directories
static const QString SinkIdListMIMEType = QSL("application/x-mvme-analysis-sink-id-list");

namespace
{

QVector<QUuid> decode_id_list(QByteArray data)
{
    QDataStream stream(&data, QIODevice::ReadOnly);
    QVector<QByteArray> sourceIds;
    stream >> sourceIds;

    QVector<QUuid> result;
    result.reserve(sourceIds.size());

    for (const auto &idData: sourceIds)
    {
        result.push_back(QUuid(idData));
    }

    return result;
}

} // end anon namespace

MVMEContext *ObjectTree::getContext() const
{
    assert(getEventWidget());
    return getEventWidget()->getContext();
}

Analysis *ObjectTree::getAnalysis() const
{
    assert(getContext());
    return getContext()->getAnalysis();
}

void ObjectTree::dropEvent(QDropEvent *event)
{
    /* Avoid calling the QTreeWidget reimplementation which handles internal moves
     * specially. Instead pass through to the QAbstractItemView base. */
    QAbstractItemView::dropEvent(event);
}

Qt::DropActions ObjectTree::supportedDropActions() const
{
    return Qt::MoveAction; // TODO: allow copying of objects
}

// DataSourceTree
QStringList DataSourceTree::mimeTypes() const
{
    return { DataSourceIdListMIMEType };
}

QMimeData *DataSourceTree::mimeData(const QList<QTreeWidgetItem *> items) const
{
    QVector<QByteArray> idData;

    for (auto item: items)
    {
        switch (item->type())
        {
            case NodeType_Source:
                if (auto source = get_pointer<SourceInterface>(item))
                {
                    idData.push_back(source->getId().toByteArray());
                }
                break;

            default:
                break;
        }
    }

    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream << idData;

    auto result = new QMimeData;
    result->setData(DataSourceIdListMIMEType, buffer);

    return result;
}

bool DataSourceTree::dropMimeData(QTreeWidgetItem *parentItem,
                                  int parentIndex,
                                  const QMimeData *data,
                                  Qt::DropAction action)
{
    if (action != Qt::MoveAction)
        return false;

    if (!data->hasFormat(DataSourceIdListMIMEType))
        return false;

    /* Drag and drop of datasources:
     * If dropped onto the tree or onto unassignedDataSourcesRoot the sources are removed
     * from their module and end up being unassigned.
     * If dropped onto a module the selected sources are (re)assigned to that module.
     */

    bool result = false;
    auto analysis = getEventWidget()->getContext()->getAnalysis();

    if (!parentItem || parentItem == unassignedDataSourcesRoot)
    {
        auto ids = decode_id_list(data->data(DataSourceIdListMIMEType));

        if (ids.isEmpty())
            return false;

        AnalysisPauser pauser(getContext());

        for (auto &id: ids)
        {
            if (auto source = analysis->getSource(id))
            {
                source->setModuleId(QUuid());
                analysis->sourceEdited(source);
            }
        }

        result = true;
    }
    else if (parentItem && parentItem->type() == NodeType_Module)
    {
        auto module = qobject_cast<ModuleConfig *>(get_qobject(parentItem));
        assert(module);

        auto ids = decode_id_list(data->data(DataSourceIdListMIMEType));

        if (ids.isEmpty())
            return false;

        AnalysisPauser pauser(getContext());

        for (auto &id: ids)
        {
            if (auto source = getAnalysis()->getSource(id))
            {
                source->setModuleId(module->getId());
                analysis->sourceEdited(source);
            }
        }

        result = true;
    }

    if (result)
    {
        getEventWidget()->repopulate();
    }

    return result;
}

// OperatorTree
QStringList OperatorTree::mimeTypes() const
{
    return { OperatorIdListMIMEType };
}

QMimeData *OperatorTree::mimeData(const QList<QTreeWidgetItem *> items) const
{
    QVector<QByteArray> idData;

    for (auto item: items)
    {
        switch (item->type())
        {
            case NodeType_Operator:
                {
                    if (auto op = get_pointer<OperatorInterface>(item))
                    {
                        idData.push_back(op->getId().toByteArray());
                    }
                } break;

            case NodeType_Directory:
                {
                    if (auto dir = get_pointer<Directory>(item))
                    {
                        idData.push_back(dir->getId().toByteArray());
                    }
                } break;

            default:
                break;
        }
    }

    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream << idData;

    auto result = new QMimeData;
    result->setData(OperatorIdListMIMEType, buffer);

    return result;
}

bool OperatorTree::dropMimeData(QTreeWidgetItem *parentItem,
                                int parentIndex,
                                const QMimeData *data,
                                Qt::DropAction action)
{
    if (action != Qt::MoveAction)
        return false;

    if (!data->hasFormat(OperatorIdListMIMEType))
        return false;

    auto ids = decode_id_list(data->data(DataSourceIdListMIMEType));

    if (ids.isEmpty())
        return false;

    DirectoryPtr destDir;

    if (parentItem && parentItem->type() == NodeType_Directory)
    {
        destDir = std::dynamic_pointer_cast<Directory>(
            get_pointer<Directory>(parentItem)->shared_from_this());
    }

    bool result = false;
    auto analysis = getEventWidget()->getContext()->getAnalysis();

    AnalysisPauser pauser(getContext());

    for (auto &id: ids)
    {
        auto obj = analysis->getObject(id);

        const s32 levelDelta = getUserLevel() - obj->getUserLevel();

        if (destDir) // drop onto a directory
        {
            // XXX: leftoff
            if (auto sourceDir = analysis->getParentDirectory(obj))
            {
                qDebug() << __PRETTY_FUNCTION__
                    << "removing" << obj.get() << "from dir" << sourceDir.get();

                sourceDir->remove(obj);
            }

            qDebug() << __PRETTY_FUNCTION__ <<
                "adding object" << obj.get() << "to directory" << destDir.get() <<
                "new userlevel =" << getUserLevel();

            // Move objects into destDir. This flattens any source hierarchy.
            destDir->push_back(obj);
            // TODO: adjust non sink operators by level delta
            if (auto op = analysis->getOperator(id))
            {
            }
            else
            {
                obj->setUserLevel(getUserLevel());
            }
            result = true;
        }
        else // drop onto a tree
        {
            if (auto sourceDir = analysis->getParentDirectory(obj))
            {
                qDebug() << __PRETTY_FUNCTION__
                    << "removing" << obj.get() << "from dir" << sourceDir.get();

                sourceDir->remove(obj);
            }
            // TODO: adjust non sink operators by level delta
        }

    }

    return result;
}

// SinkTree
QStringList SinkTree::mimeTypes() const
{
    return { SinkIdListMIMEType };
}

QMimeData *SinkTree::mimeData(const QList<QTreeWidgetItem *> items) const
{
    QVector<QByteArray> encodedIds;

    for (auto item: items)
    {
        switch (item->type())
        {
            case NodeType_Histo1DSink:
            case NodeType_Histo2DSink:
            case NodeType_Sink:
                {
                    if (auto op = get_pointer<OperatorInterface>(item))
                    {
                        encodedIds.push_back(op->getId().toByteArray());
                    }
                } break;

            case NodeType_Directory:
                {
                    if (auto dir = get_pointer<Directory>(item))
                    {
                        encodedIds.push_back(dir->getId().toByteArray());
                    }
                } break;

            default:
                break;
        }
    }

    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream << encodedIds;

    auto result = new QMimeData;
    result->setData(SinkIdListMIMEType, encoded);

    return result;
}

bool SinkTree::dropMimeData(QTreeWidgetItem *parentItem,
                            int parentIndex,
                            const QMimeData *data,
                            Qt::DropAction action)
{
    return false;
}

#if 0
bool ObjectTree::dropMimeData(QTreeWidgetItem *parentItem,
                                   int parentIndex,
                                   const QMimeData *data,
                                   Qt::DropAction action)
{
    qDebug() << __PRETTY_FUNCTION__
        << "parentItem =" << parentItem
        << ", parentIndex =" << parentIndex
        << ", action =" << action;

    /* Cases to handle when dropping objects:
     * - Always test for operator and display tree matches. Do not allow dropping objects
     *   onto the wrong trees.
     * - Move objects from tree to tree
     *   => Adjust the userlevel of the objects.
     * - Move objects from dir to tree
     *   => Adjust userlevel of objects and remove them from the source dir.
     * - Move objects from dir to dir
     *   => Remove from source dir, add to destDir and adjust the objects userlevel
     *
     * The source objects can contain directories too. Should recusrive directories be
     * allowed? Yes!
     *
     * Breakdown:
     * - list of source ids -> list of source objects
     * - dest tree -> dest userLevel
     * - [dest parentItem] -> destDir
     * - for each source object -> parent dir
     *   Objects have to be removed from parent dir when being moved!
     *
     * The case where a dir including its objects are selected and moved will leave move
     * the dir and the objects to the target. The source dir will be empty afterwards and
     * the objects will be at the same level as the source dir.
     *
     * To move a hierarchy only the top level dir must be selected. This will move the dir
     * and keep the objects inside it.
     *
     */

    if (action != Qt::MoveAction)
        return false;

    if (!data->hasFormat(OperatorIdListMIMEType))
        return false;

    DirectoryPtr destDir;

    if (parentItem && parentItem->type() == NodeType_Directory)
    {
        destDir = std::dynamic_pointer_cast<Directory>(
            get_pointer<Directory>(parentItem)->shared_from_this());
    }

    const bool isSinkTree = (qobject_cast<SinkTree *>(this) != nullptr);

    if (destDir)
    {
        auto loc = destDir->getDisplayLocation();

        if ((loc == DisplayLocation::Operator && isSinkTree)
            || (loc == DisplayLocation::Sink && !isSinkTree))
        {
            return false;
        }
    }

    auto encoded = data->data(OperatorIdListMIMEType);
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    QVector<QByteArray> sourceIds;
    stream >> sourceIds;

    AnalysisPauser pauser(m_eventWidget->getContext());
    auto analysis = m_eventWidget->getContext()->getAnalysis();
    bool didMove  = false;

    for (const auto &idData: sourceIds)
    {
        QUuid sourceId(idData);

        auto obj = analysis->getObject(sourceId);
        bool isSink = (qobject_cast<SinkInterface *>(obj.get()) != nullptr);

        if (!obj) continue;

        if (destDir) // drop onto a directory
        {
            assert(destDir->getUserLevel() == m_userLevel);

            if (auto sourceDir = analysis->getParentDirectory(obj))
            {
                qDebug() << __PRETTY_FUNCTION__
                    << "removing" << obj.get() << "from dir" << sourceDir.get();

                sourceDir->remove(obj);
            }

            qDebug() << __PRETTY_FUNCTION__ <<
                "adding object" << obj.get() << "to directory" << destDir.get() <<
                "new userlevel =" << m_userLevel;

            // Move objects into destDir. This flattens any source hierarchy.
            destDir->push_back(obj);

            if (auto op = std::dynamic_pointer_cast<OperatorInterface>(obj))
            {
                s32 levelDelta = m_userLevel - op->getUserLevel();

                qDebug() << __PRETTY_FUNCTION__ <<
                    "adjusting userlevel of" << op.get() << "and dependees by" << levelDelta;

                // Move all source operators by the same amount of userlevels.
                adjust_userlevel_forward(analysis->getOperators(), op.get(), levelDelta);
            }
            else
            {
                obj->setUserLevel(m_userLevel);
            }

            didMove = true;
        }
        else // drop onto a tree
        {
            auto op = std::dynamic_pointer_cast<OperatorInterface>(obj);
            auto sink = std::dynamic_pointer_cast<SinkInterface>(obj);

            if (sink && !isSinkTree) continue;
            if (!sink && isSinkTree) continue;

            if (auto sourceDir = analysis->getParentDirectory(obj))
            {
                qDebug() << __PRETTY_FUNCTION__
                    << "removing" << obj.get() << "from dir" << sourceDir.get();

                sourceDir->remove(obj);
            }

            if (op)
            {
                // drop operator onto tree

                s32 levelDelta = m_userLevel - op->getUserLevel();

                qDebug() << __PRETTY_FUNCTION__ <<
                    "adjusting userlevel of" << op.get() << "and dependees by" << levelDelta;

                // Move all source operators by the same amount of userlevels.
                adjust_userlevel_forward(analysis->getOperators(), op.get(), levelDelta);
                didMove = true;
            }
            else if (auto dir = std::dynamic_pointer_cast<Directory>(obj))
            {
                // drop directory onto tree

                s32 levelDelta = m_userLevel - dir->getUserLevel();

                dir->setUserLevel(m_userLevel);

                for (auto member: analysis->getDirectoryContents(dir))
                {
                    if (auto memberOp = std::dynamic_pointer_cast<OperatorInterface>(member))
                    {
                        adjust_userlevel_forward(analysis->getOperators(), memberOp.get(), levelDelta);
                    }
                    else
                    {
                        member->setUserLevel(m_userLevel);
                    }
                }

                didMove = true;
            }
        }
    }

    if (didMove)
    {
        analysis->setModified(true);
        m_eventWidget->repopulate();
        m_eventWidget->getAnalysisWidget()->updateAddRemoveUserLevelButtons();
        // TODO: find node for dest dir and expand it. this has to happen after repopulate
        // which will create new nodes for everything
    }

    qDebug() << __PRETTY_FUNCTION__ << "done, didMove =" << didMove;

    return didMove;
}

QMimeData *ObjectTree::mimeData(const QList<QTreeWidgetItem *> items) const
{
    QVector<QByteArray> encodedIds;

    for (auto item: items)
    {
        switch (item->type())
        {
            case NodeType_Operator:
            case NodeType_Histo1DSink:
            case NodeType_Histo2DSink:
            case NodeType_Sink:
                {
                    if (auto op = get_pointer<OperatorInterface>(item))
                    {
                        encodedIds.push_back(op->getId().toByteArray());
                    }
                } break;

            case NodeType_Directory:
                {
                    if (auto dir = get_pointer<Directory>(item))
                    {
                        encodedIds.push_back(dir->getId().toByteArray());
                    }
                } break;

            default:
                break;
        }
    }

    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream << encodedIds;

    auto result = new QMimeData;
    result->setData(OperatorIdListMIMEType, encoded);

    return result;
}
#endif

/* Operator (top) and Sink (bottom) trees showing objects for one userlevel. */
struct UserLevelTrees
{
    ObjectTree *operatorTree;
    SinkTree *sinkTree;
    s32 userLevel;

    std::array<ObjectTree *, 2> getObjectTrees() const
    {
        return
        {
            {
                reinterpret_cast<ObjectTree *>(operatorTree),
                reinterpret_cast<ObjectTree *>(sinkTree)
            }
        };
    }
};

static const QString AnalysisFileFilter = QSL("MVME Analysis Files (*.analysis);; All Files (*.*)");

using SetOfVoidStar = QSet<void *>;

static const u32 PeriodicUpdateTimerInterval_ms = 1000;

struct EventWidgetPrivate
{
    enum Mode
    {
        Default,
        SelectInput
    };

    EventWidget *m_q;
    MVMEContext *m_context;
    QUuid m_eventId;
    int m_eventIndex;
    AnalysisWidget *m_analysisWidget;

    QVector<UserLevelTrees> m_levelTrees;

    Mode m_mode = Default;
    QWidget *m_uniqueWidget = nullptr;

    struct InputSelectInfo
    {
        Slot *slot = nullptr;
        s32 userLevel;
        EventWidget::SelectInputCallback callback;
        // Set of additional pipe sources to be considered invalid as valid
        // inputs for the slot.
        QSet<PipeSourceInterface *> additionalInvalidSources;
    };

    InputSelectInfo m_inputSelectInfo;

    QSplitter *m_operatorFrameSplitter;
    QSplitter *m_displayFrameSplitter;

    enum TreeType
    {
        TreeType_Operator,
        TreeType_Sink,
        TreeType_Count
    };
    // Keeps track of the expansion state of those tree nodes that are storing objects in DataRole_Pointer.
    // There's two sets, one for the operator trees and one for the display
    // trees, because objects may have nodes in both trees.
    std::array<SetOfVoidStar, TreeType_Count> m_expandedObjects;
    QTimer *m_displayRefreshTimer;

    // If set the trees for that user level will not be shown
    QVector<bool> m_hiddenUserLevels;

    // The user level that was manually added via addUserLevel()
    s32 m_manualUserLevel = 0;

    // Actions used in makeToolBar()
    std::unique_ptr<QAction> m_actionImportForModuleFromTemplate;
    std::unique_ptr<QAction> m_actionImportForModuleFromFile;
    QWidgetAction *m_actionModuleImport;

    // Actions and widgets used in makeEventSelectAreaToolBar()
    QAction *m_actionExport;
    QAction *m_actionImport;
    QAction *m_actionSelectVisibleLevels;
    QLabel *m_eventRateLabel;

    QToolBar* m_upperToolBar;
    QToolBar* m_eventSelectAreaToolBar;

    // Periodically updated extractor hit counts and histo sink entry counts.
    struct ObjectCounters
    {
        QVector<double> hitCounts;
    };

    QHash<SourceInterface *, ObjectCounters> m_extractorCounters;
    QHash<Histo1DSink *, ObjectCounters> m_histo1DSinkCounters;
    QHash<Histo2DSink *, ObjectCounters> m_histo2DSinkCounters;
    MVMEStreamProcessorCounters m_prevStreamProcessorCounters;

    double m_prevAnalysisTimeticks = 0.0;;

    void createView(const QUuid &eventId);
    UserLevelTrees createTrees(const QUuid &eventId, s32 level);
    UserLevelTrees createSourceTrees(const QUuid &eventId);
    void appendTreesToView(UserLevelTrees trees);
    void repopulate();

    void addUserLevel();
    void removeUserLevel();
    s32 getUserLevelForTree(QTreeWidget *tree);

    void doOperatorTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel);
    void doSinkTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel);

    void modeChanged();
    void highlightValidInputNodes(QTreeWidgetItem *node);
    void highlightInputNodes(OperatorInterface *op);
    void highlightOutputNodes(PipeSourceInterface *ps);
    void clearToDefaultNodeHighlights(QTreeWidgetItem *node);
    void clearAllToDefaultNodeHighlights();
    void onNodeClicked(TreeNode *node, int column, s32 userLevel);
    void onNodeDoubleClicked(TreeNode *node, int column, s32 userLevel);
    void onNodeChanged(TreeNode *node, int column, s32 userLevel);
    void clearAllTreeSelections();
    void clearTreeSelectionsExcept(QTreeWidget *tree);
    void generateDefaultFilters(ModuleConfig *module);
    PipeDisplay *makeAndShowPipeDisplay(Pipe *pipe);
    void doPeriodicUpdate();
    void periodicUpdateExtractorCounters(double dt_s);
    void periodicUpdateHistoCounters(double dt_s);
    void periodicUpdateEventRate(double dt_s);
    void updateActions();

    // Returns the currentItem() of the tree widget that has focus.
    QTreeWidgetItem *getCurrentNode() const;
    QList<QTreeWidgetItem *> getAllSelectedNodes() const;
    AnalysisObjectVector getAllSelectedObjects() const;

    // import / export
    void importForModuleFromTemplate();
    void importForModuleFromFile();
    void importForModule(ModuleConfig *module, const QString &startPath);
    bool canExport() const;
    void actionExport();
    void actionImport();

    void setSinksEnabled(const QVector<SinkInterface *> sinks, bool enabled);
    void removeSinks(const QVector<SinkInterface *> sinks);
    void removeDirectoryRecursively(const DirectoryPtr &dir);
};

void EventWidgetPrivate::createView(const QUuid &eventId)
{
    auto analysis = m_context->getAnalysis();
    s32 maxUserLevel = 0;

    for (const auto &op: analysis->getOperators(eventId))
    {
        maxUserLevel = std::max(maxUserLevel, op->getUserLevel());
    }

    for (const auto &dir: analysis->getDirectories(eventId))
    {
        maxUserLevel = std::max(maxUserLevel, dir->getUserLevel());
    }

    // Level 0: special case for data sources
    m_levelTrees.push_back(createSourceTrees(eventId));

    // Level >= 1: standard trees
    for (s32 userLevel = 1; userLevel <= maxUserLevel; ++userLevel)
    {
        auto trees = createTrees(eventId, userLevel);
        m_levelTrees.push_back(trees);
    }
}

UserLevelTrees make_displaylevel_trees(const QString &opTitle, const QString &dispTitle, s32 level)
{
    const auto editTriggers = QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed;

    UserLevelTrees result = {};
    result.operatorTree = (level == 0 ? new DataSourceTree : new OperatorTree);
    result.sinkTree = new SinkTree;
    result.userLevel = level;

    result.operatorTree->setObjectName(opTitle);
    result.operatorTree->headerItem()->setText(0, opTitle);
    result.operatorTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    result.operatorTree->setEditTriggers(editTriggers);

    result.sinkTree->setObjectName(dispTitle);
    result.sinkTree->headerItem()->setText(0, dispTitle);
    result.sinkTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    result.sinkTree->setEditTriggers(editTriggers);

    for (auto tree: result.getObjectTrees())
    {
        tree->setExpandsOnDoubleClick(false);
        tree->setItemDelegate(new HtmlDelegate(tree));
        tree->setDragEnabled(true);
        tree->viewport()->setAcceptDrops(true);
        tree->setDropIndicatorShown(true);
        tree->setDragDropMode(QAbstractItemView::DragDrop);
    }

    return result;
}

UserLevelTrees EventWidgetPrivate::createSourceTrees(const QUuid &eventId)
{
    auto analysis = m_context->getAnalysis();
    auto vmeConfig = m_context->getVMEConfig();

    auto eventConfig = vmeConfig->getEventConfig(eventId);
    auto modules = eventConfig->getModuleConfigs();

    UserLevelTrees result = make_displaylevel_trees(
        QSL("L0 Parameter Extraction"),
        QSL("L0 Raw Data Display"),
        0);

    // Populate the OperatorTree
    for (const auto &mod: modules)
    {
        QObject::disconnect(mod, &ConfigObject::modified, m_q, &EventWidget::repopulate);
        QObject::connect(mod, &ConfigObject::modified, m_q, &EventWidget::repopulate);
        auto moduleNode = make_module_node(mod);
        result.operatorTree->addTopLevelItem(moduleNode);
        moduleNode->setExpanded(true);

        auto sources = analysis->getSources(eventId, mod->getId());

#ifndef QT_NO_DEBUG
        qDebug() << __PRETTY_FUNCTION__ << ">>>>> sources in order:";
        for (auto source: sources)
        {
            qDebug() << source.get();
        }
        qDebug() << __PRETTY_FUNCTION__ << " <<<< end sources";
#endif

        for (auto source: sources)
        {
            auto sourceNode = make_datasource_node(source.get());
            moduleNode->addChild(sourceNode);
        }
    }

    auto dataSourceTree = qobject_cast<DataSourceTree *>(result.operatorTree);
    assert(dataSourceTree);

    // Add unassigned data sources below a special root node
    for (const auto &source: analysis->getSourcesByEvent(eventId))
    {
        if (source->getModuleId().isNull())
        {
            if (!dataSourceTree->unassignedDataSourcesRoot)
            {
                auto node = new TreeNode({QSL("Unassigned")});
                node->setFlags(node->flags() &
                               (~Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
                node->setIcon(0, QIcon(QSL(":/exclamation-circle.png")));

                dataSourceTree->unassignedDataSourcesRoot = node;
                result.operatorTree->addTopLevelItem(node);
                node->setExpanded(true);
            }

            assert(dataSourceTree->unassignedDataSourcesRoot);

            auto sourceNode = make_datasource_node(source.get());
            dataSourceTree->unassignedDataSourcesRoot->addChild(sourceNode);
        }
    }

    // Populate the SinkTree
    // Create module nodes and nodes for the raw histograms for each data source for the module.
    QSet<QObject *> sinksAddedBelowModules;
    auto operators = analysis->getOperators(eventId, 0);

    for (const auto &mod: modules)
    {
        auto moduleNode = make_module_node(mod);
        result.sinkTree->addTopLevelItem(moduleNode);
        moduleNode->setExpanded(true);

        for (const auto &source: analysis->getSources(eventId, mod->getId()))
        {
            for (const auto &op: operators)
            {
                auto sink = qobject_cast<SinkInterface *>(op.get());

                if (sink && (sink->getSlot(0)->inputPipe == source->getOutput(0)))
                {
                    TreeNode *node = nullptr;

                    if (auto histoSink = qobject_cast<Histo1DSink *>(op.get()))
                    {
                        node = make_histo1d_node(histoSink);
                    }
                    else
                    {
                        node = make_sink_node(sink);
                    }

                    if (node)
                    {
                        moduleNode->addChild(node);
                        sinksAddedBelowModules.insert(sink);
                    }
                }
            }
        }
    }

    // This handles any "lost" display elements. E.g. raw histograms whose data
    // source has been deleted.
    for (auto &op: operators)
    {
        if (auto histoSink = qobject_cast<Histo1DSink *>(op.get()))
        {
            if (!sinksAddedBelowModules.contains(histoSink))
            {
                auto histoNode = make_histo1d_node(histoSink);
                result.sinkTree->addTopLevelItem(histoNode);
            }
        }
        else if (auto histoSink = qobject_cast<Histo2DSink *>(op.get()))
        {
            if (!sinksAddedBelowModules.contains(histoSink))
            {
                auto histoNode = make_histo2d_node(histoSink);
                result.sinkTree->addTopLevelItem(histoNode);
            }
        }
        else if (auto sink = qobject_cast<SinkInterface *>(op.get()))
        {
            if (!sinksAddedBelowModules.contains(sink))
            {
                auto sinkNode = make_sink_node(sink);
                result.sinkTree->addTopLevelItem(sinkNode);
            }
        }
    }

    result.sinkTree->sortItems(0, Qt::AscendingOrder);

    return result;
}

void add_directory_nodes(ObjectTree *tree, const DirectoryPtr &dir,
                         QHash<DirectoryPtr, TreeNode *> &nodes,
                         Analysis *analysis)
{
    if (nodes.contains(dir)) return;

    auto node = make_directory_node(dir);

    if (auto parent = analysis->getParentDirectory(dir))
    {
        add_directory_nodes(tree, parent, nodes, analysis);
        auto parentNode = nodes.value(parent);
        assert(parentNode);
        parentNode->addChild(node);
    }
    else
    {
        tree->addTopLevelItem(node);
    }

    nodes.insert(dir, node);
}

void add_directory_nodes(ObjectTree *tree, const DirectoryVector &dirs,
                         QHash<DirectoryPtr, TreeNode *> &nodes,
                         Analysis *analysis)
{
    for (const auto &dir: dirs)
    {
        add_directory_nodes(tree, dir, nodes, analysis);
    }
}

UserLevelTrees EventWidgetPrivate::createTrees(const QUuid &eventId, s32 level)
{
    UserLevelTrees result = make_displaylevel_trees(
        QString(QSL("L%1 Processing")).arg(level),
        QString(QSL("L%1 Data Display")).arg(level),
        level);

    auto analysis = m_context->getAnalysis();

    // create directory entries for both trees
    auto opDirs = analysis->getDirectories(eventId, level, DisplayLocation::Operator);
    auto sinkDirs = analysis->getDirectories(eventId, level, DisplayLocation::Sink);

    QHash<DirectoryPtr, TreeNode *> dirNodes;

    add_directory_nodes(result.operatorTree, opDirs, dirNodes, analysis);
    add_directory_nodes(result.sinkTree, sinkDirs, dirNodes, analysis);

    // Populate the OperatorTree
    auto operators = analysis->getOperators(eventId, level);

    for (auto op: operators)
    {
        if (qobject_cast<SinkInterface *>(op.get()))
            continue;

        std::unique_ptr<TreeNode> opNode(make_operator_node(op.get()));

        if (level > 0)
        {
            opNode->setFlags(opNode->flags() | Qt::ItemIsDragEnabled);
        }

        if (auto dir = analysis->getParentDirectory(op))
        {
            if (auto dirNode = dirNodes.value(dir))
            {
                dirNode->addChild(opNode.release());
            }
        }
        else
        {
            result.operatorTree->addTopLevelItem(opNode.release());
        }
    }
    result.operatorTree->sortItems(0, Qt::AscendingOrder);

    // Populate the SinkTree
    {
        auto histo1DRoot = new TreeNode({QSL("1D")});
        auto histo2DRoot = new TreeNode({QSL("2D")});
        auto rateRoot    = new TreeNode({QSL("Rates")});
        auto exportRoot  = new TreeNode({QSL("Exports")});

        for (auto node: { histo1DRoot, histo2DRoot, rateRoot, exportRoot })
        {
            result.sinkTree->addTopLevelItem(node);
            node->setExpanded(true);
        }

        result.sinkTree->histo1DRoot = histo1DRoot;
        result.sinkTree->histo2DRoot = histo2DRoot;
        result.sinkTree->rateRoot    = rateRoot;
        result.sinkTree->exportRoot  = exportRoot;

        for (const auto &op: operators)
        {
            TreeNode *theNode = nullptr;

            if (auto histoSink = qobject_cast<Histo1DSink *>(op.get()))
            {
                auto histoNode = make_histo1d_node(histoSink);
                histo1DRoot->addChild(histoNode);
                theNode = histoNode;
            }
            else if (auto histoSink = qobject_cast<Histo2DSink *>(op.get()))
            {
                auto histoNode = make_histo2d_node(histoSink);
                histo2DRoot->addChild(histoNode);
                theNode = histoNode;
            }
            else if (auto rms = qobject_cast<RateMonitorSink *>(op.get()))
            {
                theNode = make_sink_node(rms);
                rateRoot->addChild(theNode);
            }
            else if (auto ex = qobject_cast<ExportSink *>(op.get()))
            {
                theNode = make_sink_node(ex);
                exportRoot->addChild(theNode);
            }
            else if (auto sink = qobject_cast<SinkInterface *>(op.get()))
            {
                auto sinkNode = make_sink_node(sink);
                result.sinkTree->addTopLevelItem(sinkNode);
                theNode = sinkNode;
            }

            if (theNode && level > 0)
            {
                theNode->setFlags(theNode->flags() | Qt::ItemIsDragEnabled);
            }
        }
    }
    result.sinkTree->sortItems(0, Qt::AscendingOrder);


    return result;
}

static const s32 minTreeWidth = 200;
static const s32 minTreeHeight = 150;

void EventWidgetPrivate::appendTreesToView(UserLevelTrees trees)
{
    auto opTree   = trees.operatorTree;
    auto dispTree = trees.sinkTree;
    s32 levelIndex = trees.userLevel;

    opTree->setMinimumWidth(minTreeWidth);
    opTree->setMinimumHeight(minTreeHeight);
    opTree->setContextMenuPolicy(Qt::CustomContextMenu);

    dispTree->setMinimumWidth(minTreeWidth);
    dispTree->setMinimumHeight(minTreeHeight);
    dispTree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_operatorFrameSplitter->addWidget(opTree);
    m_displayFrameSplitter->addWidget(dispTree);

    QObject::connect(opTree, &QWidget::customContextMenuRequested,
                     m_q, [this, opTree, levelIndex] (QPoint pos) {
        doOperatorTreeContextMenu(opTree, pos, levelIndex);
    });

    QObject::connect(dispTree, &QWidget::customContextMenuRequested,
                     m_q, [this, dispTree, levelIndex] (QPoint pos) {
        doSinkTreeContextMenu(dispTree, pos, levelIndex);
    });

    for (auto tree: trees.getObjectTrees())
    {
        tree->setEventWidget(m_q);
        tree->setUserLevel(levelIndex);

        // mouse interaction
        QObject::connect(tree, &QTreeWidget::itemClicked,
                         m_q, [this, levelIndex] (QTreeWidgetItem *node, int column) {
            onNodeClicked(reinterpret_cast<TreeNode *>(node), column, levelIndex);
            updateActions();
        });

        QObject::connect(tree, &QTreeWidget::itemDoubleClicked,
                         m_q, [this, levelIndex] (QTreeWidgetItem *node, int column) {
            onNodeDoubleClicked(reinterpret_cast<TreeNode *>(node), column, levelIndex);
        });

        // keyboard interaction changes the treewidgets current item
        QObject::connect(tree, &QTreeWidget::currentItemChanged,
                         m_q, [this, tree](QTreeWidgetItem *current, QTreeWidgetItem *previous) {
            qDebug() << "currentItemChanged on" << tree;
#if 0
            if (current)
            {
                clearTreeSelectionsExcept(tree);
            }
            updateActions();
#endif
        });

        // inline editing via F2
        QObject::connect(tree, &QTreeWidget::itemChanged,
                         m_q, [this, levelIndex] (QTreeWidgetItem *item, int column) {
            onNodeChanged(reinterpret_cast<TreeNode *>(item), column, levelIndex);
        });

        TreeType treeType = (tree == opTree ? TreeType_Operator : TreeType_Sink);

        QObject::connect(tree, &QTreeWidget::itemExpanded,
                         m_q, [this, treeType] (QTreeWidgetItem *node) {
            if (void *voidObj = get_pointer<void>(node))
            {
                m_expandedObjects[treeType].insert(voidObj);
            }
        });

        QObject::connect(tree, &QTreeWidget::itemCollapsed,
                         m_q, [this, treeType] (QTreeWidgetItem *node) {
            if (void *voidObj = get_pointer<void>(node))
            {
                m_expandedObjects[treeType].remove(voidObj);
            }
        });

        QObject::connect(tree, &QTreeWidget::itemSelectionChanged,
                         m_q, [this, tree] () {
            //qDebug() << "itemSelectionChanged on" << tree
            //    << ", new selected item count =" << tree->selectedItems().size();
            updateActions();
        });
    }
}

static void expandObjectNodes(QTreeWidgetItem *node, const SetOfVoidStar &objectsToExpand)
{
    s32 childCount = node->childCount();

    for (s32 childIndex = 0;
         childIndex < childCount;
         ++childIndex)
    {
        auto childNode = node->child(childIndex);
        expandObjectNodes(childNode, objectsToExpand);
    }

    void *voidObj = get_pointer<void>(node);

    if (voidObj && objectsToExpand.contains(voidObj))
    {
        node->setExpanded(true);
    }
}

template<typename T>
static void expandObjectNodes(const QVector<UserLevelTrees> &treeVector, const T &objectsToExpand)
{
    for (auto trees: treeVector)
    {
        expandObjectNodes(trees.operatorTree->invisibleRootItem(),
                          objectsToExpand[EventWidgetPrivate::TreeType_Operator]);
        expandObjectNodes(trees.sinkTree->invisibleRootItem(),
                          objectsToExpand[EventWidgetPrivate::TreeType_Sink]);
    }
}

void EventWidgetPrivate::repopulate()
{
    qDebug() << __PRETTY_FUNCTION__ << m_q;

    auto splitterSizes = m_operatorFrameSplitter->sizes();
    // clear
#if 0
    for (auto trees: m_levelTrees)
    {
        // FIXME: this is done because setParent(nullptr) below will cause a
        // focus-in event on one of the other trees and that will call
        // EventWidget::eventFilter() which will call setCurrentItem() on
        // whatever tree gained focus which will emit currentItemChanged()
        // which will invoke a lambda which will call onNodeClicked() which
        // will call clearAllToDefaultNodeHighlights() which will try to figure
        // out if any operator is missing any inputs which will dereference the
        // operator which might have just been deleted via the context menu.
        // This is complicated and not great. Is it generally dangerous to have
        // currentItemChanged() call onNodeClicked()? Is there some other way
        // to stop the call chain earlier?
        trees.operatorTree->removeEventFilter(m_q);
        trees.sinkTree->removeEventFilter(m_q);
    }
#endif

    for (auto trees: m_levelTrees)
    {
        trees.operatorTree->setParent(nullptr);
        trees.operatorTree->deleteLater();

        trees.sinkTree->setParent(nullptr);
        trees.sinkTree->deleteLater();
    }
    m_levelTrees.clear();
    Q_ASSERT(m_operatorFrameSplitter->count() == 0);
    Q_ASSERT(m_displayFrameSplitter->count() == 0);

    // populate
    if (!m_eventId.isNull())
    {
        // This populates m_d->m_levelTrees
        createView(m_eventId);
    }

    for (auto trees: m_levelTrees)
    {
        // This populates the operator and display splitters
        appendTreesToView(trees);
    }

    s32 levelsToAdd = m_manualUserLevel - m_levelTrees.size();

    for (s32 i = 0; i < levelsToAdd; ++i)
    {
        s32 levelIndex = m_levelTrees.size();
        auto trees = createTrees(m_eventId, levelIndex);
        m_levelTrees.push_back(trees);
        appendTreesToView(trees);
    }

    if (splitterSizes.size() == m_operatorFrameSplitter->count())
    {
        // Restore the splitter sizes. As the splitters are synced via
        // splitterMoved() they both had the same sizes before.
        m_operatorFrameSplitter->setSizes(splitterSizes);
        m_displayFrameSplitter->setSizes(splitterSizes);
    }

    m_hiddenUserLevels.resize(m_levelTrees.size());

    for (s32 idx = 0; idx < m_hiddenUserLevels.size(); ++idx)
    {
        m_levelTrees[idx].operatorTree->setVisible(!m_hiddenUserLevels[idx]);
        m_levelTrees[idx].sinkTree->setVisible(!m_hiddenUserLevels[idx]);
    }

    expandObjectNodes(m_levelTrees, m_expandedObjects);
    clearAllToDefaultNodeHighlights();
    updateActions();
}

void EventWidgetPrivate::addUserLevel()
{
    s32 levelIndex = m_levelTrees.size();
    auto trees = createTrees(m_eventId, levelIndex);
    m_levelTrees.push_back(trees);
    appendTreesToView(trees);
    m_manualUserLevel = levelIndex + 1;
}

void EventWidgetPrivate::removeUserLevel()
{
    Q_ASSERT(m_levelTrees.size() > 1);
    auto trees = m_levelTrees.last();
    m_levelTrees.pop_back();
    delete trees.operatorTree;
    delete trees.sinkTree;
    m_manualUserLevel = m_levelTrees.size();
}

s32 EventWidgetPrivate::getUserLevelForTree(QTreeWidget *tree)
{
    for (s32 userLevel = 0;
         userLevel < m_levelTrees.size();
         ++userLevel)
    {
        auto trees = m_levelTrees[userLevel];
        if (tree == trees.operatorTree || tree == trees.sinkTree)
        {
            return userLevel;
        }
    }

    return -1;
}

namespace
{

QDialog *operator_editor_factory(const std::shared_ptr<OperatorInterface> &op,
                                 s32 userLevel, OperatorEditorMode mode, EventWidget *eventWidget)
{
    QDialog *result = nullptr;

    if (auto expr = std::dynamic_pointer_cast<ExpressionOperator>(op))
    {
        result = new ExpressionOperatorDialog(expr, userLevel, mode, eventWidget);
    }
    else
    {
        result = new AddEditOperatorDialog(op, userLevel, mode, eventWidget);
    }

    return result;
}

} // end anon namespace

/* Context menu for the operator tree views (top). */
void EventWidgetPrivate::doOperatorTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel)
{
    auto node = tree->itemAt(pos);
    auto obj  = get_qobject(node);

    QMenu menu;
    auto menuNew = new QMenu(&menu);
    bool actionNewIsFirst = false;

    if (node)
    {
        /* The level 0 tree. This is where the VME side modules show up and
         * data extractors can be created. */
        if (userLevel == 0 && node->type() == NodeType_Module)
        {
            if (!m_uniqueWidget)
            {
                auto moduleConfig = get_pointer<ModuleConfig>(node);

                // new data sources / filters
                auto add_newDataSourceAction =
                    [this, &menu, menuNew, moduleConfig](const QString &title, auto srcPtr)
                {
                    menuNew->addAction(title, &menu, [this, moduleConfig, srcPtr]() {
                        QDialog *dialog = nullptr;

                        if (auto ex = std::dynamic_pointer_cast<Extractor>(srcPtr))
                        {
                            dialog = new AddEditExtractorDialog(
                                ex, moduleConfig, AddEditExtractorDialog::AddExtractor, m_q);

                            QObject::connect(dialog, &QDialog::accepted,
                                             m_q, &EventWidget::addExtractorDialogAccepted);

                            QObject::connect(dialog, &QDialog::rejected,
                                             m_q, &EventWidget::addEditExtractorDialogRejected);
                        }
                        else if (qobject_cast<ListFilterExtractor *>(srcPtr.get()))
                        {
                            auto lfe_dialog = new ListFilterExtractorDialog(
                                moduleConfig, m_context->getAnalysis(), m_context, m_q);

                            if (!m_context->getAnalysis()->getListFilterExtractors(
                                    moduleConfig->getEventId(), moduleConfig->getId()).isEmpty())
                            {
                                lfe_dialog->newFilter();
                            }

                            QObject::connect(lfe_dialog, &QDialog::accepted,
                                             m_q, &EventWidget::listFilterExtractorDialogAccepted);

                            QObject::connect(lfe_dialog, &ListFilterExtractorDialog::applied,
                                             m_q, &EventWidget::listFilterExtractorDialogApplied);

                            QObject::connect(lfe_dialog, &QDialog::rejected,
                                             m_q, &EventWidget::listFilterExtractorDialogRejected);

                            dialog = lfe_dialog;
                        }
                        else
                        {
                            InvalidCodePath;
                        }

                        //POS dialog->move(QCursor::pos());
                        dialog->setAttribute(Qt::WA_DeleteOnClose);
                        dialog->show();
                        m_uniqueWidget = dialog;
                        clearAllTreeSelections();
                        clearAllToDefaultNodeHighlights();
                    });
                };

                auto analysis = m_context->getAnalysis();
                auto &registry(analysis->getObjectFactory());

                QVector<SourcePtr> sourceInstances;

                for (auto sourceName: registry.getSourceNames())
                {
                    SourcePtr src(registry.makeSource(sourceName));
                    sourceInstances.push_back(src);
                }

                // Sort sources by displayname
                qSort(sourceInstances.begin(), sourceInstances.end(), [](const SourcePtr &a, const SourcePtr &b) {
                    return a->getDisplayName() < b->getDisplayName();
                });

                for (auto src: sourceInstances)
                {
                    add_newDataSourceAction(src->getDisplayName(), src);
                }

                // default data filters and "raw display" creation
                if (moduleConfig)
                {
                    auto defaultExtractors = get_default_data_extractors(
                        moduleConfig->getModuleMeta().typeName);

                    if (!defaultExtractors.isEmpty())
                    {
                        menu.addAction(QSL("Generate default filters"), [this, moduleConfig] () {

                            QMessageBox box(
                                QMessageBox::Question,
                                QSL("Generate default filters"),
                                QSL("This action will generate extraction filters,"
                                    ", calibrations and histograms for the selected module."
                                    " Do you want to continue?"),
                                QMessageBox::Ok | QMessageBox::No,
                                m_q);

                            box.button(QMessageBox::Ok)->setText("Yes, generate filters");

                            if (box.exec() == QMessageBox::Ok)
                            {
                                generateDefaultFilters(moduleConfig);
                            }
                        });
                    }

                    auto menuImport = new QMenu(&menu);
                    menuImport->setTitle(QSL("Import"));
                    //menuImport->setIcon(QIcon(QSL(":/analysis_module_import.png")));
                    menuImport->addAction(m_actionImportForModuleFromTemplate.get());
                    menuImport->addAction(m_actionImportForModuleFromFile.get());
                    menu.addMenu(menuImport);

                    // Module Settings
                    menu.addAction(QIcon(QSL(":/gear.png")), QSL("Module Settings"),
                                         &menu, [this, moduleConfig]() {

                        auto analysis = m_context->getAnalysis();
                        auto moduleSettings = analysis->getVMEObjectSettings(moduleConfig->getId());

                        ModuleSettingsDialog dialog(moduleConfig, moduleSettings, m_q);

                        if (dialog.exec() == QDialog::Accepted)
                        {
                            analysis->setVMEObjectSettings(moduleConfig->getId(),
                                                           dialog.getSettings());
                        }
                    });
                }

                actionNewIsFirst = true;
            }
        }

        /* Context menu for an existing data source. */
        if (userLevel == 0 && node->type() == NodeType_Source)
        {
            auto sourceInterface = get_pointer<SourceInterface>(node);

            if (sourceInterface)
            {
                Q_ASSERT_X(sourceInterface->getNumberOfOutputs() == 1,
                           "doOperatorTreeContextMenu",
                           "data sources with multiple outputs are not supported");

                auto moduleNode = node->parent();
                ModuleConfig *moduleConfig = nullptr;

                if (moduleNode && moduleNode->type() == NodeType_Module)
                    moduleConfig = get_pointer<ModuleConfig>(moduleNode);

                const bool isAttachedToModule = moduleConfig != nullptr;

                auto pipe = sourceInterface->getOutput(0);

                if (isAttachedToModule)
                {
                    menu.addAction(QSL("Show Parameters"), [this, pipe]() {
                        makeAndShowPipeDisplay(pipe);
                    });
                }

                if (!m_uniqueWidget)
                {
                    if (moduleConfig)
                    {
                        menu.addAction(QSL("Edit"), [this, sourceInterface, moduleConfig]() {
                            QDialog *dialog = nullptr;

                            auto srcPtr = sourceInterface->shared_from_this();

                            if (auto ex = std::dynamic_pointer_cast<Extractor>(srcPtr))
                            {
                                dialog = new AddEditExtractorDialog(
                                    ex, moduleConfig, AddEditExtractorDialog::EditExtractor, m_q);

                                QObject::connect(dialog, &QDialog::accepted,
                                                 m_q, &EventWidget::editExtractorDialogAccepted);

                                QObject::connect(dialog, &QDialog::rejected,
                                                 m_q, &EventWidget::addEditExtractorDialogRejected);
                            }
                            else if (auto lfe = std::dynamic_pointer_cast<ListFilterExtractor>(srcPtr))
                            {
                                auto lfe_dialog = new ListFilterExtractorDialog(
                                    moduleConfig, m_context->getAnalysis(), m_context, m_q);

                                lfe_dialog->editListFilterExtractor(lfe);

                                QObject::connect(lfe_dialog, &QDialog::accepted, m_q,
                                                 &EventWidget::listFilterExtractorDialogAccepted);

                                QObject::connect(lfe_dialog, &ListFilterExtractorDialog::applied, m_q,
                                                 &EventWidget::listFilterExtractorDialogApplied);

                                QObject::connect(lfe_dialog, &QDialog::rejected, m_q,
                                                 &EventWidget::listFilterExtractorDialogRejected);

                                dialog = lfe_dialog;
                            }
                            else
                            {
                                InvalidCodePath;
                            }

                            //POS dialog->move(QCursor::pos());
                            dialog->setAttribute(Qt::WA_DeleteOnClose);
                            dialog->show();
                            m_uniqueWidget = dialog;
                            clearAllTreeSelections();
                            clearAllToDefaultNodeHighlights();
                        });
                    }

                    menu.addAction(QSL("Remove"), [this, sourceInterface]() {
                        // TODO: QMessageBox::question or similar or undo functionality
                        m_q->removeSource(sourceInterface);
                    });
                }
            }
        }

        if (userLevel > 0 && node->type() == NodeType_OutputPipe)
        {
            auto pipe = get_pointer<Pipe>(node);

            menu.addAction(QSL("Show Parameters"), [this, pipe]() {
                makeAndShowPipeDisplay(pipe);
            });
        }

        if (userLevel > 0 && node->type() == NodeType_Operator)
        {
            if (!m_uniqueWidget)
            {
                auto rawOpPtr = get_pointer<OperatorInterface>(node);
                Q_ASSERT(rawOpPtr);

                auto op = std::dynamic_pointer_cast<OperatorInterface>(rawOpPtr->shared_from_this());

                if (op->getNumberOfOutputs() == 1)
                {
                    Pipe *pipe = op->getOutput(0);

                    menu.addAction(QSL("Show Parameters"), [this, pipe]() {
                        makeAndShowPipeDisplay(pipe);
                    });
                }

                // Edit Operator
                menu.addAction(QSL("Edit"), [this, userLevel, op]() {
                    auto dialog = operator_editor_factory(op, userLevel, OperatorEditorMode::Edit, m_q);
                    //POS dialog->move(QCursor::pos());
                    dialog->setAttribute(Qt::WA_DeleteOnClose);
                    dialog->show();
                    m_uniqueWidget = dialog;
                    clearAllTreeSelections();
                    clearAllToDefaultNodeHighlights();

                    QObject::connect(dialog, &QDialog::accepted,
                                     m_q, &EventWidget::editOperatorDialogAccepted);

                    QObject::connect(dialog, &QDialog::rejected,
                                     m_q, &EventWidget::addEditOperatorDialogRejected);
                });

                menu.addAction("Rename", [node] () {
                    if (auto tw = node->treeWidget())
                    {
                        tw->editItem(node);
                    }
                });

                menu.addSeparator();
                menu.addAction(QIcon::fromTheme("edit-delete"), QSL("Remove"), [this, op]() {
                    // TODO: QMessageBox::question or similar or undo functionality
                    m_q->removeOperator(op.get());
                });
            }
        }

        if (node->type() == NodeType_Directory && !m_uniqueWidget)
        {
            menu.addAction("Rename", [node] () {
                if (auto tw = node->treeWidget())
                {
                    tw->editItem(node);
                }
            });

            if (auto dir = get_shared_analysis_object<Directory>(node))
            {

                menu.addSeparator();
                menu.addAction(QIcon::fromTheme("edit-delete"), "Remove", [this, dir] () {
                    // TODO: QMessageBox::question or similar or undo functionality
                    removeDirectoryRecursively(dir);
                });
            }
        }
    }
    else // No node selected
    {
        if (m_mode == EventWidgetPrivate::Default && !m_uniqueWidget)
        {
            if (userLevel > 0)
            {
                auto add_newOperatorAction = [this, &menu, menuNew, userLevel](const QString &title, auto op)
                {
                    // New Operator
                    menuNew->addAction(title, &menu, [this, userLevel, op]() {
                        auto dialog = operator_editor_factory(op, userLevel, OperatorEditorMode::New, m_q);
                        //POS dialog->move(QCursor::pos());
                        dialog->setAttribute(Qt::WA_DeleteOnClose);
                        dialog->show();
                        m_uniqueWidget = dialog;
                        clearAllTreeSelections();
                        clearAllToDefaultNodeHighlights();

                        QObject::connect(dialog, &QDialog::accepted,
                                         m_q, &EventWidget::addOperatorDialogAccepted);

                        QObject::connect(dialog, &QDialog::rejected,
                                         m_q, &EventWidget::addEditOperatorDialogRejected);
                    });
                };

                auto analysis = m_context->getAnalysis();
                auto &registry(analysis->getObjectFactory());
                QVector<OperatorPtr> operatorInstances;

                for (auto operatorName: registry.getOperatorNames())
                {
                    OperatorPtr op(registry.makeOperator(operatorName));
                    operatorInstances.push_back(op);
                }

                // Sort operators by displayname
                qSort(operatorInstances.begin(), operatorInstances.end(),
                      [](const OperatorPtr &a, const OperatorPtr &b) {
                    return a->getDisplayName() < b->getDisplayName();
                });

                for (auto op: operatorInstances)
                {
                    add_newOperatorAction(op->getDisplayName(), op);
                }

                menuNew->addSeparator();
                menuNew->addAction(QIcon(QSL(":/folder_orange.png")), QSL("Directory"),
                                   &menu, [this, userLevel]() {

                    auto dir = std::make_shared<Directory>();
                    dir->setObjectName("New Directory");
                    dir->setUserLevel(userLevel);
                    dir->setEventId(m_eventId);
                    dir->setDisplayLocation(DisplayLocation::Operator);
                    m_context->getAnalysis()->addDirectory(dir);
                    repopulate();

                });
            }
        }
    }

    if (menuNew->isEmpty())
    {
        delete menuNew;
    }
    else
    {
        auto actionNew = menu.addAction(QSL("New"));
        actionNew->setMenu(menuNew);
        QAction *before = nullptr;
        if (actionNewIsFirst)
        {
            before = menu.actions().value(0);
        }
        menu.insertAction(before, actionNew);
    }

    if (!menu.isEmpty())
    {
        menu.exec(tree->mapToGlobal(pos));
    }
}

/* Context menu for the display/sink trees (bottom). */
void EventWidgetPrivate::doSinkTreeContextMenu(QTreeWidget *tree, QPoint pos, s32 userLevel)
{
    Q_ASSERT(userLevel >= 0 && userLevel < m_levelTrees.size());

    auto selectedNodes = tree->selectedItems();
    auto node          = tree->itemAt(pos);
    auto obj           = get_qobject(node);

    QMenu menu;
    auto menuNew = new QMenu;

    auto add_newOperatorAction = [this, &menu, menuNew, userLevel](const QString &title, auto op)
    {
        // New Display Operator
        menuNew->addAction(title, &menu, [this, userLevel, op]() {
            auto dialog = operator_editor_factory(op, userLevel, OperatorEditorMode::New, m_q);

            //POS dialog->move(QCursor::pos());
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
            m_uniqueWidget = dialog;
            clearAllTreeSelections();
            clearAllToDefaultNodeHighlights();

            QObject::connect(dialog, &QDialog::accepted,
                             m_q, &EventWidget::addOperatorDialogAccepted);

            QObject::connect(dialog, &QDialog::rejected,
                             m_q, &EventWidget::addEditOperatorDialogRejected);
        });
    };

    if (selectedNodes.size() > 1)
    {
        QVector<SinkInterface *> selectedSinks;
        selectedSinks.reserve(selectedNodes.size());

        for (auto node: selectedNodes)
        {
            switch (node->type())
            {
                case NodeType_Histo1DSink:
                case NodeType_Histo2DSink:
                case NodeType_Sink:
                    selectedSinks.push_back(get_pointer<SinkInterface>(node));
                    break;
            }
        }

        if (selectedSinks.size())
        {
            menu.addAction("E&nable selected", [this, selectedSinks] {
                setSinksEnabled(selectedSinks, true);
            });

            menu.addAction("&Disable selected", [this, selectedSinks] {
                setSinksEnabled(selectedSinks, false);
            });

            menu.addSeparator();
            menu.addAction("Remove selected", [this, selectedSinks] {
                removeSinks(selectedSinks);
            });
        }
    }
    else if (node)
    {
        switch (node->type())
        {
            case NodeType_Histo1D:
                {
                    Histo1DWidgetInfo widgetInfo = getHisto1DWidgetInfoFromNode(node);
                    Q_ASSERT(widgetInfo.sink);

                    if (widgetInfo.histoAddress < widgetInfo.histos.size())
                    {
                        menu.addAction(QSL("Open Histogram"), m_q, [this, widgetInfo]() {

                            if (!m_context->hasObjectWidget(widgetInfo.sink.get()) || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                            {
                                auto widget = new Histo1DListWidget(widgetInfo.histos);
                                widget->setContext(m_context);

                                if (widgetInfo.calib)
                                {
                                    widget->setCalibration(widgetInfo.calib);
                                }

                                {
                                    auto context = m_context;
                                    widget->setSink(widgetInfo.sink, [context] (const std::shared_ptr<Histo1DSink> &sink) {
                                        context->analysisOperatorEdited(sink);
                                    });
                                }

                                widget->selectHistogram(widgetInfo.histoAddress);

                                m_context->addObjectWidget(widget, widgetInfo.sink.get(), widgetInfo.sink->getId().toString());
                            }
                            else if (auto widget = qobject_cast<Histo1DListWidget *>(m_context->getObjectWidget(widgetInfo.sink.get())))
                            {
                                widget->selectHistogram(widgetInfo.histoAddress);
                                show_and_activate(widget);
                            }
                        });

                        menu.addAction(QSL("Open Histogram in new window"), m_q, [this, widgetInfo]() {

                            auto widget = new Histo1DListWidget(widgetInfo.histos);
                            widget->setContext(m_context);

                            if (widgetInfo.calib)
                            {
                                widget->setCalibration(widgetInfo.calib);
                            }

                            {
                                auto context = m_context;
                                widget->setSink(widgetInfo.sink, [context] (const std::shared_ptr<Histo1DSink> &sink) {
                                    context->analysisOperatorEdited(sink);
                                });
                            }

                            widget->selectHistogram(widgetInfo.histoAddress);

                            m_context->addObjectWidget(widget, widgetInfo.sink.get(), widgetInfo.sink->getId().toString());
                        });
                    }
                } break;

            case NodeType_Histo1DSink:
                {
                    Histo1DWidgetInfo widgetInfo = getHisto1DWidgetInfoFromNode(node);
                    Q_ASSERT(widgetInfo.sink);

                    if (widgetInfo.histoAddress < widgetInfo.histos.size())
                    {

                        menu.addAction(QSL("Open 1D List View"), m_q, [this, widgetInfo]() {
                            // always creates a new window
                            auto widget = new Histo1DListWidget(widgetInfo.histos);
                            widget->setContext(m_context);

                            if (widgetInfo.calib)
                            {
                                widget->setCalibration(widgetInfo.calib);
                            }

                            {
                                auto context = m_context;
                                widget->setSink(widgetInfo.sink, [context] (const std::shared_ptr<Histo1DSink> &sink) {
                                    context->analysisOperatorEdited(sink);
                                });
                            }

                            m_context->addObjectWidget(widget, widgetInfo.sink.get(), widgetInfo.sink->getId().toString());

                        });
                    }


                    if (widgetInfo.histos.size())
                    {
                        menu.addAction(QSL("Open 2D Combined View"), m_q, [this, widgetInfo]() {
                            auto widget = new Histo2DWidget(widgetInfo.sink, m_context);
                            widget->setContext(m_context);
                            m_context->addWidget(widget, widgetInfo.sink->getId().toString() + QSL("_2dCombined"));
                        });
                    }
                } break;

            case NodeType_Histo2DSink:
                {
                    if (auto histoSink = qobject_cast<Histo2DSink *>(obj))
                    {
                        auto histo = histoSink->m_histo;
                        if (histo)
                        {
                            auto sinkPtr = std::dynamic_pointer_cast<Histo2DSink>(histoSink->shared_from_this());

                            menu.addAction(QSL("Open Histogram"), m_q, [this, histo, sinkPtr, userLevel]() {

                                if (!m_context->hasObjectWidget(sinkPtr.get()) || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                                {
                                    auto histoPtr = sinkPtr->m_histo;
                                    auto widget = new Histo2DWidget(histoPtr);

                                    auto context = m_context;
                                    auto eventId = m_eventId;

                                    widget->setSink(sinkPtr,
                                                    // addSinkCallback
                                                    [context, eventId, userLevel] (const std::shared_ptr<Histo2DSink> &sink) {
                                                        context->addAnalysisOperator(eventId, sink, userLevel);
                                                    },
                                                    // sinkModifiedCallback
                                                    [context] (const std::shared_ptr<Histo2DSink> &sink) {
                                                        context->analysisOperatorEdited(sink);
                                                    },
                                                    // makeUniqueOperatorNameFunction
                                                    [context] (const QString &name) {
                                                        return make_unique_operator_name(context->getAnalysis(), name);
                                                    });

                                    widget->setContext(m_context);

                                    m_context->addObjectWidget(widget, sinkPtr.get(), sinkPtr->getId().toString());
                                }
                                else
                                {
                                    m_context->activateObjectWidget(sinkPtr.get());
                                }
                            });

                            menu.addAction(QSL("Open Histogram in new window"), m_q, [this, histo, sinkPtr, userLevel]() {

                                auto histoPtr = sinkPtr->m_histo;
                                auto widget = new Histo2DWidget(histoPtr);

                                auto context = m_context;
                                auto eventId = m_eventId;

                                widget->setSink(sinkPtr,
                                                // addSinkCallback
                                                [context, eventId, userLevel] (const std::shared_ptr<Histo2DSink> &sink) {
                                                    context->addAnalysisOperator(eventId, sink, userLevel);
                                                },
                                                // sinkModifiedCallback
                                                [context] (const std::shared_ptr<Histo2DSink> &sink) {
                                                    context->analysisOperatorEdited(sink);
                                                },
                                                // makeUniqueOperatorNameFunction
                                                [context] (const QString &name) {
                                                    return make_unique_operator_name(context->getAnalysis(), name);
                                                });

                                widget->setContext(m_context);

                                m_context->addObjectWidget(widget, sinkPtr.get(), sinkPtr->getId().toString());
                            });
                        }
                    }
                } break;

            case NodeType_Sink:
                if (auto ex = qobject_cast<ExportSink *>(obj))
                {
                    auto sinkPtr = std::dynamic_pointer_cast<ExportSink>(ex->shared_from_this());
                    menu.addAction("Open Status Monitor", m_q, [this, sinkPtr]() {
                        if (!m_context->hasObjectWidget(sinkPtr.get()) || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                        {
                            auto widget = new ExportSinkStatusMonitor(sinkPtr, m_context);
                            m_context->addObjectWidget(widget, sinkPtr.get(), sinkPtr->getId().toString());
                        }
                        else
                        {
                            m_context->activateObjectWidget(sinkPtr.get());
                        }
                    });
                }
                break;

            case NodeType_Module:
                {
                    auto sink = std::make_shared<Histo1DSink>();
                    add_newOperatorAction(sink->getDisplayName(), sink);
                } break;
        }

        if (auto opRawPtr = qobject_cast<OperatorInterface *>(obj))
        {
            auto op = std::dynamic_pointer_cast<OperatorInterface>(opRawPtr->shared_from_this());
            assert(op);


            if (!m_uniqueWidget)
            {
                // Edit Display Operator
                menu.addAction(QSL("&Edit"), [this, userLevel, op]() {
                    auto dialog = operator_editor_factory(op, userLevel, OperatorEditorMode::Edit, m_q);
                    //POS dialog->move(QCursor::pos());
                    dialog->setAttribute(Qt::WA_DeleteOnClose);
                    dialog->show();
                    m_uniqueWidget = dialog;
                    clearAllTreeSelections();
                    clearAllToDefaultNodeHighlights();

                    QObject::connect(dialog, &QDialog::accepted,
                                     m_q, &EventWidget::editOperatorDialogAccepted);

                    QObject::connect(dialog, &QDialog::rejected,
                                     m_q, &EventWidget::addEditOperatorDialogRejected);
                });

                if (auto sink = std::dynamic_pointer_cast<SinkInterface>(op))
                {
                    menu.addSeparator();
                    menu.addAction(sink->isEnabled() ? QSL("&Disable") : QSL("E&nable"),
                                   [this, sink]() {
                        m_q->toggleSinkEnabled(sink.get());
                    });
                }

                menu.addSeparator();
                menu.addAction(QSL("Remove"), [this, op]() {
                    // maybe TODO: QMessageBox::question or similar as there's no way to undo the action
                    m_q->removeOperator(op.get());
                });
            }
        }

        if (userLevel > 0 && !m_uniqueWidget)
        {
            auto sinkTree = m_levelTrees[userLevel].sinkTree;
            Q_ASSERT(sinkTree->topLevelItemCount() >= 2);
            Q_ASSERT(sinkTree->histo1DRoot);
            Q_ASSERT(sinkTree->histo2DRoot);
            Q_ASSERT(sinkTree->rateRoot);
            Q_ASSERT(sinkTree->exportRoot);

            std::shared_ptr<SinkInterface> sink;

            if (node == sinkTree->histo1DRoot)
            {
                sink = std::make_shared<Histo1DSink>();
            }
            else if (node == sinkTree->histo2DRoot)
            {
                sink = std::make_shared<Histo2DSink>();
            }
            else if (node == sinkTree->rateRoot)
            {
                sink = std::make_shared<RateMonitorSink>();
            }
            else if (node == sinkTree->exportRoot)
            {
                sink = std::make_shared<ExportSink>();
            }

            if (sink)
            {
                add_newOperatorAction(sink->getDisplayName(), sink);
            }
        }
    }
    else
    {
        if (m_mode == EventWidgetPrivate::Default && !m_uniqueWidget)
        {
            if (userLevel == 0)
            {
                {
                    auto sink = std::make_shared<Histo1DSink>();
                    add_newOperatorAction(sink->getDisplayName(), sink);
                }
                {
                    auto sink = std::make_shared<RateMonitorSink>();
                    add_newOperatorAction(sink->getDisplayName(), sink);
                }
            }
            else
            {
                auto analysis = m_context->getAnalysis();
                auto &registry(analysis->getObjectFactory());

                for (auto sinkName: registry.getSinkNames())
                {
                    OperatorPtr sink(registry.makeSink(sinkName));
                    add_newOperatorAction(sink->getDisplayName(), sink);
                }
            }
        }
    }

    if (menuNew->isEmpty())
    {
        delete menuNew;
    }
    else
    {
        auto actionNew = menu.addAction(QSL("New"));
        actionNew->setMenu(menuNew);
        menu.addAction(actionNew);
    }

    if (!menu.isEmpty())
    {
        menu.exec(tree->mapToGlobal(pos));
    }
}

void EventWidgetPrivate::modeChanged()
{
    switch (m_mode)
    {
        case Default:
            {
                /* The previous mode was SelectInput so m_inputSelectInfo.userLevel
                 * must still be valid */
                Q_ASSERT(m_inputSelectInfo.userLevel < m_levelTrees.size());

                for (s32 userLevel = 0; userLevel <= m_inputSelectInfo.userLevel; userLevel++)
                {
                    auto opTree = m_levelTrees[userLevel].operatorTree;
                    clearToDefaultNodeHighlights(opTree->invisibleRootItem());
                }
            } break;

        case SelectInput:
            // highlight valid sources
            {
                clearAllTreeSelections();


                Q_ASSERT(m_inputSelectInfo.userLevel < m_levelTrees.size());

                for (s32 userLevel = 0; userLevel <= m_inputSelectInfo.userLevel; ++userLevel)
                {
                    auto opTree = m_levelTrees[userLevel].operatorTree;
                    highlightValidInputNodes(opTree->invisibleRootItem());
                }
            } break;
    }

    updateActions();
}

static bool forward_path_exists(PipeSourceInterface *from, PipeSourceInterface *to)
{
    if (!from) return false;
    if (!to) return false;


    for (s32 oi = 0; oi < from->getNumberOfOutputs(); oi++)
    {
        auto outPipe = from->getOutput(oi);

        for (auto destSlot: outPipe->getDestinations())
        {
            if (destSlot->parentOperator == to)
                return true;
            if (destSlot->parentOperator && forward_path_exists(destSlot->parentOperator, to))
                return true;
        }
    }

    return false;
}

static bool is_valid_input_node(QTreeWidgetItem *node, Slot *slot,
                                QSet<PipeSourceInterface *> additionalInvalidSources)
{
    PipeSourceInterface *dstObject = slot->parentOperator;
    Q_ASSERT(dstObject);

    PipeSourceInterface *srcObject = nullptr;

    switch (node->type())
    {
        case NodeType_Operator:
            {
                srcObject = get_pointer<PipeSourceInterface>(node);
                Q_ASSERT(srcObject);
            } break;
        case NodeType_OutputPipe:
        case NodeType_OutputPipeParameter:
            {
                auto pipe = get_pointer<Pipe>(node);
                srcObject = pipe->source;
                Q_ASSERT(srcObject);
            } break;
    }

    bool result = false;

    if (srcObject == dstObject)
    {
        // do not allow direct self-connections! :)
        result = false;
    }
    else if (additionalInvalidSources.contains(srcObject))
    {
        // manually given pipe sources to ignore
        result = false;
    }
    else if (forward_path_exists(dstObject, srcObject))
    {
        result = false;
    }
    // FIXME: enabling this breaks input selection for Histo2DSink: selecting the first
    // axis input works. this then created a forward path and selecting input for the 2nd
    // axis from the same input pipe is not allowed anymore
    //else if (forward_path_exists(srcObject, dstObject))
    //{
    //    result = false;
    //}
    else if ((slot->acceptedInputTypes & InputType::Array)
        && (node->type() == NodeType_Operator || node->type() == NodeType_Source))
    {
        // Highlight operator and source nodes only if they have exactly a
        // single output.
        PipeSourceInterface *pipeSource = get_pointer<PipeSourceInterface>(node);
        if (pipeSource->getNumberOfOutputs() == 1)
        {
            result = true;
        }
    }
    else if ((slot->acceptedInputTypes & InputType::Array)
             && node->type() == NodeType_OutputPipe)
    {
        result = true;
    }
    else if ((slot->acceptedInputTypes & InputType::Value)
             && node->type() == NodeType_OutputPipeParameter)
    {
        result = true;
    }

    return result;
}

static const QColor ValidInputNodeColor         = QColor("lightgreen");
static const QColor InputNodeOfColor            = QColor(0x90, 0xEE, 0x90, 255.0/3); // lightgreen but with some alpha
static const QColor ChildIsInputNodeOfColor     = QColor(0x90, 0xEE, 0x90, 255.0/6);

static const QColor OutputNodeOfColor           = QColor(0x00, 0x00, 0xCD, 255.0/3); // mediumblue with some alpha
static const QColor ChildIsOutputNodeOfColor    = QColor(0x00, 0x00, 0xCD, 255.0/6);

static const QColor MissingInputColor           = QColor(0xB2, 0x22, 0x22, 255.0/3); // firebrick with some alpha

void EventWidgetPrivate::highlightValidInputNodes(QTreeWidgetItem *node)
{
    if (is_valid_input_node(node, m_inputSelectInfo.slot, m_inputSelectInfo.additionalInvalidSources))
    {
        node->setBackground(0, ValidInputNodeColor);
    }

    for (s32 childIndex = 0; childIndex < node->childCount(); ++childIndex)
    {
        // recurse
        auto child = node->child(childIndex);
        highlightValidInputNodes(child);
    }
}

static bool isSourceNodeOf(QTreeWidgetItem *node, Slot *slot)
{
    PipeSourceInterface *srcObject = nullptr;

    switch (node->type())
    {
        case NodeType_Source:
        case NodeType_Operator:
            {
                srcObject = get_pointer<PipeSourceInterface>(node);
                Q_ASSERT(srcObject);
            } break;

        case NodeType_OutputPipe:
        case NodeType_OutputPipeParameter:
            {
                auto pipe = get_pointer<Pipe>(node);
                srcObject = pipe->source;
                Q_ASSERT(srcObject);
            } break;
    }

    bool result = false;

    if (slot->inputPipe->source == srcObject)
    {
        if (slot->paramIndex == Slot::NoParamIndex && node->type() != NodeType_OutputPipeParameter)
        {
            result = true;
        }
        else if (slot->paramIndex != Slot::NoParamIndex && node->type() == NodeType_OutputPipeParameter)
        {
            s32 nodeParamAddress = node->data(0, DataRole_ParameterIndex).toInt();
            result = (nodeParamAddress == slot->paramIndex);
        }
    }

    return result;
}

static bool isOutputNodeOf(QTreeWidgetItem *node, PipeSourceInterface *ps)
{
    OperatorInterface *dstObject = nullptr;

    switch (node->type())
    {
        case NodeType_Operator:
        case NodeType_Histo1DSink:
        case NodeType_Histo2DSink:
        case NodeType_Sink:
            {
                dstObject = get_pointer<OperatorInterface>(node);
                Q_ASSERT(dstObject);
            } break;
    }

    bool result = false;

    if (dstObject)
    {
        for (s32 slotIndex = 0; slotIndex < dstObject->getNumberOfSlots(); ++slotIndex)
        {
            Slot *slot = dstObject->getSlot(slotIndex);

            if (slot->inputPipe)
            {
                for (s32 outputIndex = 0; outputIndex < ps->getNumberOfOutputs(); ++outputIndex)
                {
                    Pipe *pipe = ps->getOutput(outputIndex);
                    if (slot->inputPipe == pipe)
                    {
                        result = true;
                        break;
                    }
                }
            }
        }
    }

    return result;
}

// Returns true if this node or any of its children represent an input of the
// given operator.
static bool highlightInputNodes(OperatorInterface *op, QTreeWidgetItem *node)
{
    bool result = false;

    for (s32 childIndex = 0; childIndex < node->childCount(); ++childIndex)
    {
        // recurse
        auto child = node->child(childIndex);
        result = highlightInputNodes(op, child) || result;
    }

    if (result)
    {
        node->setBackground(0, ChildIsInputNodeOfColor);
    }

    for (s32 slotIndex = 0; slotIndex < op->getNumberOfSlots(); ++slotIndex)
    {
        Slot *slot = op->getSlot(slotIndex);
        if (slot->inputPipe && isSourceNodeOf(node, slot))
        {
            node->setBackground(0, InputNodeOfColor);
            result = true;
        }
    }

    return result;
}

// Returns true if this node or any of its children are connected to an output of the
// given pipe source.
static bool highlightOutputNodes(PipeSourceInterface *ps, QTreeWidgetItem *node)
{
    bool result = false;

    for (s32 childIndex = 0; childIndex < node->childCount(); ++childIndex)
    {
        // recurse
        auto child = node->child(childIndex);
        result = highlightOutputNodes(ps, child) || result;
    }

    if (result)
    {
        node->setBackground(0, ChildIsOutputNodeOfColor);
    }

    if (isOutputNodeOf(node, ps))
    {
        node->setBackground(0, OutputNodeOfColor);
        result = true;
    }

    return result;
}

void EventWidgetPrivate::highlightInputNodes(OperatorInterface *op)
{
    for (auto trees: m_levelTrees)
    {
        // Without the namespace prefix the compiler can't find
        // highlightInputNodes()...
        analysis::highlightInputNodes(op, trees.operatorTree->invisibleRootItem());
    }
}

void EventWidgetPrivate::highlightOutputNodes(PipeSourceInterface *ps)
{
    for (auto trees: m_levelTrees)
    {
        analysis::highlightOutputNodes(ps, trees.operatorTree->invisibleRootItem());
        analysis::highlightOutputNodes(ps, trees.sinkTree->invisibleRootItem());
    }
}

void EventWidgetPrivate::clearToDefaultNodeHighlights(QTreeWidgetItem *node)
{
    node->setBackground(0, QBrush());

    for (s32 childIndex = 0; childIndex < node->childCount(); ++childIndex)
    {
        // recurse
        auto child = node->child(childIndex);
        clearToDefaultNodeHighlights(child);
    }

    switch (node->type())
    {
        case NodeType_Operator:
        case NodeType_Histo1DSink:
        case NodeType_Histo2DSink:
            {
                auto op = get_pointer<OperatorInterface>(node);
                for (auto slotIndex = 0; slotIndex < op->getNumberOfSlots(); ++slotIndex)
                {
                    Slot *slot = op->getSlot(slotIndex);

                    Q_ASSERT(slot);

                    if (!slot->isParamIndexInRange())
                    {
                        node->setBackground(0, MissingInputColor);
                        break;
                    }
                }
            } break;
    }

    switch (node->type())
    {
        case NodeType_Histo1DSink:
        case NodeType_Histo2DSink:
        case NodeType_Sink:
            {
                auto sink = get_pointer<SinkInterface>(node);

                if (!sink->isEnabled())
                {
                    auto font = node->font(0);
                    font.setStrikeOut(true);
                    node->setFont(0, font);
                }
            } break;
    }
}

void EventWidgetPrivate::clearAllToDefaultNodeHighlights()
{
    for (auto trees: m_levelTrees)
    {
        clearToDefaultNodeHighlights(trees.operatorTree->invisibleRootItem());
        clearToDefaultNodeHighlights(trees.sinkTree->invisibleRootItem());
    }
}

void EventWidgetPrivate::onNodeClicked(TreeNode *node, int column, s32 userLevel)
{
    switch (m_mode)
    {
        case Default:
            {
                auto kmods = QGuiApplication::keyboardModifiers();

                if (!(kmods & (Qt::ControlModifier | Qt::ShiftModifier)))
                {
                    clearTreeSelectionsExcept(node->treeWidget());
                }

                clearAllToDefaultNodeHighlights();

                switch (node->type())
                {
                    case NodeType_Operator:
                    case NodeType_Histo1DSink:
                    case NodeType_Histo2DSink:
                    case NodeType_Sink:
                        {
                            auto op = get_pointer<OperatorInterface>(node);
                            highlightInputNodes(op);

                            qDebug() << "Object Info: id =" << op->getId()
                                << ", class =" << op->metaObject()->className()
                                << ", #slots =" << op->getNumberOfSlots();

                            for (s32 si = 0; si < op->getNumberOfSlots(); si++)
                            {
                                auto slot = op->getSlot(si);
                                QString inputObjectId = QSL("<none>");

                                if (slot->isConnected())
                                {
                                    inputObjectId = slot->inputPipe->getSource()->getId().toString();
                                }

                                qDebug() << " Slot" << si << ": isParamIndexInRange() =" << slot->isParamIndexInRange()
                                    << ", isConnected() =" << slot->isConnected()
                                    << ", sourceId =" << inputObjectId;
                            }


                        } break;
                }

                switch (node->type())
                {
                    case NodeType_Source:
                    case NodeType_Operator:
                        {
                            auto ps = get_pointer<PipeSourceInterface>(node);
                            highlightOutputNodes(ps);
                        } break;
                }
            } break;

        case SelectInput:
            {
                clearTreeSelectionsExcept(node->treeWidget());

                if (is_valid_input_node(node, m_inputSelectInfo.slot,
                                     m_inputSelectInfo.additionalInvalidSources)
                    && getUserLevelForTree(node->treeWidget()) <= m_inputSelectInfo.userLevel)
                {
                    Slot *slot = m_inputSelectInfo.slot;
                    Q_ASSERT(slot);

                    Pipe *selectedPipe = nullptr;
                    s32 selectedParamIndex = Slot::NoParamIndex;

                    switch (node->type())
                    {
                        /* Click on a Source or Operator node: use output[0]
                         * and connect the whole array. */
                        case NodeType_Source:
                        case NodeType_Operator:
                            {
                                Q_ASSERT(slot->acceptedInputTypes & InputType::Array);

                                PipeSourceInterface *source = get_pointer<PipeSourceInterface>(node);

                                selectedPipe       = source->getOutput(0);
                                selectedParamIndex = Slot::NoParamIndex;

                                //slot->connectPipe(source->getOutput(0), Slot::NoParamIndex);
                            } break;

                        /* Click on a specific output of an object. */
                        case NodeType_OutputPipe:
                            {
                                Q_ASSERT(slot->acceptedInputTypes & InputType::Array);
                                Q_ASSERT(slot->parentOperator);

                                selectedPipe       = get_pointer<Pipe>(node);
                                selectedParamIndex = Slot::NoParamIndex;

                                //slot->connectPipe(pipe, Slot::NoParamIndex);
                            } break;

                        /* Click on a specific parameter index. */
                        case NodeType_OutputPipeParameter:
                            {
                                Q_ASSERT(slot->acceptedInputTypes & InputType::Value);

                                selectedPipe       = get_pointer<Pipe>(node);
                                selectedParamIndex = node->data(0, DataRole_ParameterIndex).toInt();
                            } break;

                        InvalidDefaultCase;
                    }

                    Q_ASSERT(selectedPipe);
                    Q_ASSERT(m_inputSelectInfo.callback);

                    // tell the widget that initiated the select that we're done
                    if (m_inputSelectInfo.callback)
                    {
                        qDebug() << __PRETTY_FUNCTION__ << "invoking selectInputCallback:"
                            << slot << selectedPipe << selectedParamIndex;
                        m_inputSelectInfo.callback(slot, selectedPipe, selectedParamIndex);
                    }

                    // leave SelectInput mode
                    m_mode = Default;
                    m_inputSelectInfo.callback = nullptr;
                    modeChanged();
                }
            } break;
    }
}

void EventWidgetPrivate::onNodeDoubleClicked(TreeNode *node, int column, s32 userLevel)
{
    if (m_mode == Default)
    {
        switch (node->type())
        {
            case NodeType_Histo1D:
                {
                    Histo1DWidgetInfo widgetInfo = getHisto1DWidgetInfoFromNode(node);
                    Q_ASSERT(widgetInfo.sink);

                    if (widgetInfo.histoAddress >= widgetInfo.histos.size())
                        break;

                    if (!widgetInfo.histos[widgetInfo.histoAddress])
                        break;


                    if (!m_context->hasObjectWidget(widgetInfo.sink.get())
                        || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                    {
                        auto widget = new Histo1DListWidget(widgetInfo.histos);
                        widget->setContext(m_context);

                        if (widgetInfo.calib)
                        {
                            widget->setCalibration(widgetInfo.calib);
                        }

                        {
                            auto context = m_context;
                            widget->setSink(widgetInfo.sink, [context] (const std::shared_ptr<Histo1DSink> &sink) {
                                context->analysisOperatorEdited(sink);
                            });
                        }

                        widget->selectHistogram(widgetInfo.histoAddress);

                        m_context->addObjectWidget(widget, widgetInfo.sink.get(),
                                                   widgetInfo.sink->getId().toString());
                    }
                    else if (auto widget = qobject_cast<Histo1DListWidget *>(
                            m_context->getObjectWidget(widgetInfo.sink.get())))
                    {
                        widget->selectHistogram(widgetInfo.histoAddress);
                        show_and_activate(widget);
                    }
                } break;

            case NodeType_Histo1DSink:
                {
                    Histo1DWidgetInfo widgetInfo = getHisto1DWidgetInfoFromNode(node);
                    Q_ASSERT(widgetInfo.sink);

                    if (widgetInfo.histos.size())
                    {
                        if (!m_context->hasObjectWidget(widgetInfo.sink.get())
                            || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                        {
                            auto widget = new Histo1DListWidget(widgetInfo.histos);
                            widget->setContext(m_context);

                            if (widgetInfo.calib)
                            {
                                widget->setCalibration(widgetInfo.calib);
                            }

                            {
                                auto context = m_context;
                                widget->setSink(widgetInfo.sink,
                                                [context] (const std::shared_ptr<Histo1DSink> &sink) {
                                    context->analysisOperatorEdited(sink);
                                });
                            }

                            m_context->addObjectWidget(widget, widgetInfo.sink.get(),
                                                       widgetInfo.sink->getId().toString());
                        }
                        else
                        {
                            m_context->activateObjectWidget(widgetInfo.sink.get());
                        }
                    }
                } break;

            case NodeType_Histo2DSink:
                {
                    auto sinkPtr = std::dynamic_pointer_cast<Histo2DSink>(
                        get_pointer<Histo2DSink>(node)->shared_from_this());

                    if (!sinkPtr->m_histo)
                        break;

                    if (!m_context->hasObjectWidget(sinkPtr.get())
                        || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                    {
                        auto histoPtr = sinkPtr->m_histo;
                        auto widget = new Histo2DWidget(histoPtr);

                        auto context = m_context;
                        auto eventId = m_eventId;

                        widget->setSink(
                            sinkPtr,
                            // addSinkCallback
                            [context, eventId, userLevel] (const std::shared_ptr<Histo2DSink> &sink) {
                                context->addAnalysisOperator(eventId, sink, userLevel);
                            },
                            // sinkModifiedCallback
                            [context] (const std::shared_ptr<Histo2DSink> &sink) {
                                context->analysisOperatorEdited(sink);
                            },
                            // makeUniqueOperatorNameFunction
                            [context] (const QString &name) {
                                return make_unique_operator_name(context->getAnalysis(), name);
                        });

                        widget->setContext(m_context);

                        m_context->addObjectWidget(widget, sinkPtr.get(), sinkPtr->getId().toString());
                    }
                    else
                    {
                        m_context->activateObjectWidget(sinkPtr.get());
                    }
                } break;

            case NodeType_Sink:
                if (auto rms = get_shared_analysis_object<RateMonitorSink>(node))
                {
                    if (!m_context->hasObjectWidget(rms.get())
                        || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                    {
                        auto context = m_context;
                        auto widget = new RateMonitorWidget(rms->getRateSamplers());

                        widget->setSink(rms, [context](const std::shared_ptr<RateMonitorSink> &sink) {
                            context->analysisOperatorEdited(sink);
                        });

                        widget->setPlotExportDirectory(
                            m_context->getWorkspacePath(QSL("PlotsDirectory")));

                        m_context->addObjectWidget(widget, rms.get(), rms->getId().toString());
                    }
                    else
                    {
                        m_context->activateObjectWidget(rms.get());
                    }
                }
                else if (auto ex = get_shared_analysis_object<ExportSink>(node))
                {
                    if (!m_context->hasObjectWidget(ex.get())
                        || QGuiApplication::keyboardModifiers() & Qt::ControlModifier)
                    {
                        auto widget = new ExportSinkStatusMonitor(ex, m_context);
                        m_context->addObjectWidget(widget, ex.get(), ex->getId().toString());
                    }
                    else
                    {
                        m_context->activateObjectWidget(ex.get());
                    }
                }
                break;
        }
    }
}

void EventWidgetPrivate::onNodeChanged(TreeNode *node, int column, s32 userLevel)
{
    if (column != 0)
        return;

    switch (node->type())
    {
        case NodeType_Source:
        case NodeType_Operator:
        case NodeType_Histo1DSink:
        case NodeType_Histo2DSink:
        case NodeType_Sink:
        case NodeType_Directory:
            break;

        default:
            return;
    }

    //qDebug() << __PRETTY_FUNCTION__
    //    << "node =" << node << ", col =" << column << ", userLevel =" << userLevel;

    if (auto obj = get_pointer<AnalysisObject>(node))
    {
        auto value    = node->data(0, Qt::EditRole).toString();
        bool modified = value != obj->objectName();

        //qDebug() << __PRETTY_FUNCTION__
        //    << "node =" << node
        //    << ", value =" << value
        //    << ", objName =" << obj->objectName()
        //    << ", modified =" << modified;

        if (modified)
        {
            obj->setObjectName(value);
            m_q->getAnalysis()->setModified(true);

            if (auto op = qobject_cast<OperatorInterface *>(obj))
            {
                node->setData(0, Qt::DisplayRole, QString("<b>%1</b> %2").arg(
                        op->getShortName(),
                        op->objectName()));
            }
            else
            {
                node->setData(0, Qt::DisplayRole, value);
            }
        }
    }
}

void EventWidgetPrivate::clearAllTreeSelections()
{
    for (UserLevelTrees trees: m_levelTrees)
    {
        for (auto tree: trees.getObjectTrees())
        {
            tree->clearSelection();
        }
    }
}

void EventWidgetPrivate::clearTreeSelectionsExcept(QTreeWidget *treeNotToClear)
{
    for (UserLevelTrees trees: m_levelTrees)
    {
        for (auto tree: trees.getObjectTrees())
        {
            if (tree != treeNotToClear)
            {
                tree->clearSelection();
            }
        }
    }
}

void EventWidgetPrivate::generateDefaultFilters(ModuleConfig *module)
{
    AnalysisPauser pauser(m_context);

    auto defaultFilters = get_default_data_extractors(module->getModuleMeta().typeName);

    for (auto &ex: defaultFilters)
    {
        auto dataFilter = ex->getFilter();
        double unitMin = 0.0;
        double unitMax = std::pow(2.0, dataFilter.getDataBits());
        QString name = module->getModuleMeta().typeName + QSL(".") + ex->objectName().section('.', 0, -1);

        RawDataDisplay rawDataDisplay = make_raw_data_display(dataFilter, unitMin, unitMax,
                                                              name,
                                                              ex->objectName().section('.', 0, -1),
                                                              QString());

        add_raw_data_display(m_context->getAnalysis(), m_eventId, module->getId(), rawDataDisplay);
    }

    repopulate();
}

PipeDisplay *EventWidgetPrivate::makeAndShowPipeDisplay(Pipe *pipe)
{
    auto widget = new PipeDisplay(m_context->getAnalysis(), pipe, m_q);
    QObject::connect(m_displayRefreshTimer, &QTimer::timeout, widget, &PipeDisplay::refresh);
    QObject::connect(pipe->source, &QObject::destroyed, widget, &QWidget::close);
    add_widget_close_action(widget);
    widget->move(QCursor::pos());
    widget->setAttribute(Qt::WA_DeleteOnClose);
    widget->show();
    return widget;
}

void EventWidgetPrivate::doPeriodicUpdate()
{
    /* If it's a replay: use timeticks
     * If it's DAQ: use elapsed walltime
     * Reason: if analysis efficiency is < 1.0 timeticks will be lost. Thus
     * using timeticks with a DAQ run may lead to very confusing numbers as
     * sometimes ticks will be lost, at other times they'll appear.
     */

    auto analysis = m_context->getAnalysis();
    bool isReplay = analysis->getRunInfo().isReplay;
    double dt_s = 0.0;
    double currentAnalysisTimeticks = analysis->getTimetickCount();

    if (isReplay)
    {
        dt_s = calc_delta0(currentAnalysisTimeticks, m_prevAnalysisTimeticks);
    }
    else
    {
        dt_s = PeriodicUpdateTimerInterval_ms / 1000.0;
    }

    periodicUpdateExtractorCounters(dt_s);
    periodicUpdateHistoCounters(dt_s);
    periodicUpdateEventRate(dt_s);

    m_prevAnalysisTimeticks = currentAnalysisTimeticks;
}

void EventWidgetPrivate::periodicUpdateExtractorCounters(double dt_s)
{
    auto analysis = m_context->getAnalysis();
    auto a2State = analysis->getA2AdapterState();

    //
    // level 0: operator tree (Extractor hitcounts)
    //
    for (auto iter = QTreeWidgetItemIterator(m_levelTrees[0].operatorTree);
         *iter; ++iter)
    {
        auto node(*iter);

        if (node->type() == NodeType_Source)
        {
            auto source = qobject_cast<SourceInterface *>(get_pointer<PipeSourceInterface>(node));

            if (!source)
                continue;

            auto ds_a2 = a2State->sourceMap.value(source, nullptr);

            if (!ds_a2)
                continue;

            auto hitCounts = to_qvector(ds_a2->hitCounts);

            auto &prevHitCounts = m_extractorCounters[source].hitCounts;

            prevHitCounts.resize(hitCounts.size());

            auto hitCountDeltas = calc_deltas0(hitCounts, prevHitCounts);
            auto hitCountRates = hitCountDeltas;
            std::for_each(hitCountRates.begin(), hitCountRates.end(), [dt_s](double &d) { d /= dt_s; });

            Q_ASSERT(hitCounts.size() == node->childCount());

            for (s32 addr = 0; addr < node->childCount(); ++addr)
            {
                Q_ASSERT(node->child(addr)->type() == NodeType_OutputPipeParameter);

                QString addrString = QString("%1").arg(addr, 2).replace(QSL(" "), QSL("&nbsp;"));

                double hitCount = hitCounts[addr];
                auto childNode = node->child(addr);

                if (hitCount <= 0.0)
                {
                    childNode->setText(0, addrString);
                }
                else
                {
                    double rate = hitCountRates[addr];

                    if (std::isnan(rate)) rate = 0.0;

                    auto rateString = format_number(rate, QSL("cps"), UnitScaling::Decimal,
                                                    0, 'g', 3);

                    childNode->setText(0, QString("%1 (hits=%2, rate=%3, dt=%4 s)")
                                       .arg(addrString)
                                       .arg(hitCount)
                                       .arg(rateString)
                                       .arg(dt_s)
                                      );
                }
            }

            prevHitCounts = hitCounts;
        }
    }
}

void EventWidgetPrivate::periodicUpdateHistoCounters(double dt_s)
{
    auto analysis = m_context->getAnalysis();
    auto a2State = analysis->getA2AdapterState();

    //
    // level > 0: display trees (histo counts)
    //
    for (auto trees: m_levelTrees)
    {
        for (auto iter = QTreeWidgetItemIterator(trees.sinkTree);
             *iter; ++iter)
        {
            auto node(*iter);

            if (node->type() == NodeType_Histo1DSink)
            {
                auto histoSink = qobject_cast<Histo1DSink *>(get_pointer<OperatorInterface>(node));

                if (!histoSink)
                    continue;

                if (histoSink->m_histos.size() != node->childCount())
                    continue;

                QVector<double> entryCounts;

                if (a2State)
                {
                    if (auto a2_sink = a2State->operatorMap.value(histoSink, nullptr))
                    {
                        auto sinkData = reinterpret_cast<a2::H1DSinkData *>(a2_sink->d);

                        entryCounts.reserve(sinkData->histos.size);

                        for (s32 i = 0; i < sinkData->histos.size; i++)
                        {
                            entryCounts.push_back(sinkData->histos[i].entryCount);
                        }
                    }
                }

                auto &prevEntryCounts = m_histo1DSinkCounters[histoSink].hitCounts;

                prevEntryCounts.resize(entryCounts.size());

                auto entryCountDeltas = calc_deltas0(entryCounts, prevEntryCounts);
                auto entryCountRates = entryCountDeltas;
                std::for_each(entryCountRates.begin(), entryCountRates.end(), [dt_s](double &d) { d /= dt_s; });

                auto maxCount = std::min(entryCounts.size(), node->childCount());

                for (s32 addr = 0; addr < maxCount; ++addr)
                {
                    Q_ASSERT(node->child(addr)->type() == NodeType_Histo1D);

                    QString numberString = QString("%1").arg(addr, 2).replace(QSL(" "), QSL("&nbsp;"));

                    double entryCount = entryCounts[addr];
                    auto childNode = node->child(addr);

                    if (entryCount <= 0.0)
                    {
                        childNode->setText(0, numberString);
                    }
                    else
                    {
                        double rate = entryCountRates[addr];
                        if (std::isnan(rate)) rate = 0.0;

                        auto rateString = format_number(rate, QSL("cps"), UnitScaling::Decimal,
                                                        0, 'g', 3);

                        childNode->setText(0, QString("%1 (entries=%2, rate=%3, dt=%4 s)")
                                           .arg(numberString)
                                           .arg(entryCount, 0, 'g', 3)
                                           .arg(rateString)
                                           .arg(dt_s)
                                          );
                    }
                }

                prevEntryCounts = entryCounts;
            }
            else if (node->type() == NodeType_Histo2DSink)
            {
                auto sink = get_pointer<Histo2DSink>(node);
                auto histo = sink->m_histo;

                if (histo)
                {
                    double entryCount = 0.0;

                    if (auto a2_sink = a2State->operatorMap.value(sink, nullptr))
                    {
                        auto sinkData = reinterpret_cast<a2::H2DSinkData *>(a2_sink->d);

                        entryCount = sinkData->histo.entryCount;
                    }
                    auto &prevEntryCounts = m_histo2DSinkCounters[sink].hitCounts;
                    prevEntryCounts.resize(1);

                    double prevEntryCount = prevEntryCounts[0];

                    double countDelta = calc_delta0(entryCount, prevEntryCount);
                    double countRate = countDelta / dt_s;

                    if (entryCount <= 0.0)
                    {
                        node->setText(0, QString("<b>%1</b> %2")
                                      .arg(sink->getShortName())
                                      .arg(sink->objectName())
                                     );
                    }
                    else
                    {
                        if (std::isnan(countRate)) countRate = 0.0;

                        auto rateString = format_number(countRate, QSL("cps"), UnitScaling::Decimal,
                                                        0, 'g', 3);

                        node->setText(0, QString("<b>%1</b> %2 (entries=%3, rate=%4, dt=%5)")
                                      .arg(sink->getShortName())
                                      .arg(sink->objectName())
                                      .arg(entryCount, 0, 'g', 3)
                                      .arg(rateString)
                                      .arg(dt_s)
                                     );
                    }

                    prevEntryCounts[0] = entryCount;
                }
            }
        }
    }

}

void EventWidgetPrivate::periodicUpdateEventRate(double dt_s)
{
    auto &prevCounters(m_prevStreamProcessorCounters);
    const auto &counters(m_context->getMVMEStreamWorker()->getCounters());
    Q_ASSERT(0 <= m_eventIndex && m_eventIndex < (s32)counters.eventCounters.size());

    /* Use the counters of the first module in this event as that represents
     * the event rate after multi-event splitting. */
    double deltaEvents = calc_delta0(
        counters.moduleCounters[m_eventIndex][0],
        prevCounters.moduleCounters[m_eventIndex][0]);

    double eventCount = counters.moduleCounters[m_eventIndex][0];
    double eventRate = deltaEvents / dt_s;
    if (std::isnan(eventRate)) eventRate = 0.0;

    auto labelText = (QString("count=%1\nrate=%2")
                      .arg(format_number(eventCount, QSL(""), UnitScaling::Decimal))
                      .arg(format_number(eventRate, QSL("cps"), UnitScaling::Decimal, 0, 'g', 3))
                     );

    if (m_context->getAnalysis()->getRunInfo().isReplay)
    {
        double walltimeRate = deltaEvents / (PeriodicUpdateTimerInterval_ms / 1000.0);

        labelText += (QString("\nreplayRate=%1")
                      .arg(format_number(walltimeRate, QSL("cps"), UnitScaling::Decimal, 0, 'g', 3))
                      );
    }
    else // not a replay
    {
        auto daqStats = m_context->getDAQStats();
        double efficiency = daqStats.getAnalysisEfficiency();
        efficiency = std::isnan(efficiency) ? 0.0 : efficiency;

        labelText += QSL("\nEfficiency=%1").arg(efficiency, 0, 'f', 2);
    }

    m_eventRateLabel->setText(labelText);

    prevCounters = counters;
}


QTreeWidgetItem *EventWidgetPrivate::getCurrentNode() const
{
    QTreeWidgetItem *result = nullptr;

    if (auto activeTree = qobject_cast<QTreeWidget *>(m_q->focusWidget()))
    {
        result = activeTree->currentItem();
    }

    return result;
}

/* Returns the concatenation of the individual tree selections.
 * Note that the results are not sorted in a specific way but reflect the ordering of the
 * unterlying Qt itemview selection mechanism. */
QList<QTreeWidgetItem *> EventWidgetPrivate::getAllSelectedNodes() const
{
    QList<QTreeWidgetItem *> result;

    for (const auto &trees: m_levelTrees)
    {
        result.append(trees.operatorTree->selectedItems());
        result.append(trees.sinkTree->selectedItems());
    }

    return result;
}

/* Returns the set of selected analysis objects across all userlevel tree widgets.
 * Note that the results are not sorted in a specific way but reflect the ordering of the
 * unterlying Qt itemview selection mechanism. */
AnalysisObjectVector EventWidgetPrivate::getAllSelectedObjects() const
{
    AnalysisObjectVector result;

    for (const auto &node: getAllSelectedNodes())
    {
        switch (node->type())
        {
            case NodeType_Source:
            case NodeType_Operator:
            case NodeType_Histo1DSink:
            case NodeType_Histo2DSink:
            case NodeType_Sink:
            case NodeType_Directory:
                if (auto obj = get_analysis_object(node))
                {
                    result.push_back(obj);
                }
                break;
        }
    }

    return result;
}

void EventWidgetPrivate::updateActions()
{
    auto node = getCurrentNode();

    m_actionModuleImport->setEnabled(false);
    m_actionImportForModuleFromTemplate->setEnabled(false);
    m_actionImportForModuleFromFile->setEnabled(false);
    m_actionExport->setEnabled(false);

    if (m_mode == Default)
    {
        if (node && node->type() == NodeType_Module)
        {
            if (auto module = get_pointer<ModuleConfig>(node))
            {
                m_actionModuleImport->setEnabled(true);
                m_actionImportForModuleFromTemplate->setEnabled(true);
                m_actionImportForModuleFromFile->setEnabled(true);
            }
        }

        m_actionExport->setEnabled(canExport());
    }
}

void EventWidgetPrivate::importForModuleFromTemplate()
{
    auto node = getCurrentNode();

    if (node && node->type() == NodeType_Module)
    {
        if (auto module = get_pointer<ModuleConfig>(node))
        {
            QString path = vats::get_module_path(module->getModuleMeta().typeName) + QSL("/analysis");
            importForModule(module, path);
        }
    }
}

void EventWidgetPrivate::importForModuleFromFile()
{
    auto node = getCurrentNode();

    if (node && node->type() == NodeType_Module)
    {
        if (auto module = get_pointer<ModuleConfig>(node))
        {
            importForModule(module, m_context->getWorkspaceDirectory());
        }
    }
}

/* Importing of module specific analysis objects
 * - Module must be given
 * - Let user pick a file starting in the given startPath
 * - Load analysis from file
 * - Generate new IDs for analysis objects
 * - Try auto assignment but using only the ModuleInfo from the given target module
 * - If auto assignment fails run the assigment gui also using only info for the selected module.
 * - Remove analysis objects not related to the target module.
 * - Add the remaining objects into the existing analysis
 */
void EventWidgetPrivate::importForModule(ModuleConfig *module, const QString &startPath)
{
    auto event = qobject_cast<EventConfig *>(module->parent());

    if (!event)
        return;

    QString fileName = QFileDialog::getOpenFileName(m_q, QSL("Import analysis objects"),
                                                    startPath, AnalysisFileFilter);

    if (fileName.isEmpty())
        return;

    QJsonDocument doc(gui_read_json_file(fileName));
    auto json = doc.object()[QSL("AnalysisNG")].toObject();

    Analysis analysis;
    auto readResult = analysis.read(json, m_context->getVMEConfig());

    if (!readResult)
    {
        readResult.errorData["Source file"] = fileName;
        QMessageBox::critical(m_q,
                              QSL("Error importing analysis"),
                              readResult.toRichText());
        return;
    }

    using namespace vme_analysis_common;

    ModuleInfo moduleInfo;
    moduleInfo.id = module->getId();
    moduleInfo.typeName = module->getModuleMeta().typeName;
    moduleInfo.name = module->objectName();
    moduleInfo.eventId = event->getId();

    generate_new_object_ids(&analysis);

    if (!auto_assign_vme_modules({moduleInfo}, &analysis))
    {
        if (!run_vme_analysis_module_assignment_ui({moduleInfo}, &analysis))
            return;
    }

    remove_analysis_objects_unless_matching(&analysis, moduleInfo);

    m_context->logMessage(QString("Importing %1").arg(info_string(&analysis)));

    auto sources = analysis.getSources();
    auto operators = analysis.getOperators();

    if (sources.isEmpty() && operators.isEmpty())
        return;

    AnalysisPauser pauser(m_context);
    auto targetAnalysis = m_context->getAnalysis();

    s32 baseUserLevel = targetAnalysis->getMaxUserLevel(moduleInfo.eventId);

    for (auto source: sources)
    {
        Q_ASSERT(source->getEventId() == moduleInfo.eventId);
        Q_ASSERT(source->getModuleId() == moduleInfo.id);

        targetAnalysis->addSource(source->getEventId(),
                                  source->getModuleId(),
                                  source);
    }

    for (auto op: operators)
    {
        Q_ASSERT(op->getEventId() == moduleInfo.eventId);

        s32 targetUserLevel = baseUserLevel + op->getUserLevel();

        if (op->getUserLevel() == 0 && qobject_cast<SinkInterface *>(op.get()))
        {
            targetUserLevel = 0;
        }

        targetAnalysis->addOperator(op->getEventId(),
                                    targetUserLevel,
                                    op);
    }

    targetAnalysis->beginRun(Analysis::KeepState);

    // FIXME: directories

    repopulate();
}

bool EventWidgetPrivate::canExport() const
{
    for (const auto &node: getAllSelectedNodes())
    {
        switch (node->type())
        {
            case NodeType_Source:
            case NodeType_Operator:
            case NodeType_Histo1DSink:
            case NodeType_Histo2DSink:
            case NodeType_Sink:
            case NodeType_Directory:
                return true;
        }
    }

    return false;
}

static const char *AnalysisLibraryFileFilter =
    "MVME Analysis Library Files (*.analysislib);; All Files (*.*)";

static const char *AnalysisLibraryFileExtension = ".analysislib";

void EventWidgetPrivate::actionExport()
{
    assert(canExport());

    // Step 0) Let the user pick a file
    auto path = m_context->getWorkspaceDirectory();

    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getSaveFileName(m_q, QSL("Select file to export to"),
                                                    path, AnalysisLibraryFileFilter);

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);

    if (fi.completeSuffix().isEmpty())
        fileName += AnalysisLibraryFileExtension;

    // Step 1) Collect all objects that have to be written out
    auto analysis = m_context->getAnalysis();
    auto selectedObjects = getAllSelectedObjects();
    auto allObjects = order_objects(expand_objects(selectedObjects, analysis), analysis);

    qDebug() << __PRETTY_FUNCTION__
        << "#selected =" << selectedObjects.size()
        << ", #collected =" << allObjects.size();

    // Step 2) Create the JSON structures and the document
    ObjectSerializerVisitor sv;
    visit_objects(allObjects.begin(), allObjects.end(), sv);

    QJsonObject exportRoot;
    exportRoot["MVMEAnalysisExport"] = sv.finalize();

    QJsonDocument doc(exportRoot);

    qDebug() << __PRETTY_FUNCTION__
        << "exporting" << sv.objectCount() << "objects";

    // Step 3) Write to file
    // FIXME: replace with something that can give a specific error message for
    // this concrete file save operation instead of just a generic write error
    gui_write_json_file(fileName, doc);
}

void EventWidgetPrivate::actionImport()
{
    /* Global import without a specific target directory/userlevel or a
     * subselection of objects.
     * The following should happen:
     * Read in the file, check for version errors and create all contained objects.
     * Place them as is without modifying userlevels or directories.
     * Regenerate unique IDs
     * Later: for each imported object check if an object of the same type and
     * name exists. If so append a suffix to the object name to make it unique.
     * Finally select the newly added objects.
     */
    qDebug() << __PRETTY_FUNCTION__;

    QString startPath = m_context->getWorkspaceDirectory();

    QString fileName = QFileDialog::getOpenFileName(m_q, QSL("Import analysis objects"),
                                                    startPath, AnalysisLibraryFileFilter);

    if (fileName.isEmpty())
        return;

    QJsonDocument doc(gui_read_json_file(fileName));
    auto exportRoot = doc.object();

    if (!exportRoot.contains("MVMEAnalysisExport"))
    {
        QMessageBox::critical(m_q, "File format error", "File format error");
        return;
    }

    auto importData = exportRoot["MVMEAnalysisExport"].toObject();

    if (importData["MVMEAnalysisVersion"].toInt() > Analysis::getCurrentAnalysisVersion())
    {
        QMessageBox::critical(m_q, "File version error",
                              "The library file was written by a more recent version of mvme."
                              " Please update to the latest release.");
        return;
    }
}

void EventWidgetPrivate::setSinksEnabled(const QVector<SinkInterface *> sinks, bool enabled)
{
    if (sinks.isEmpty())
        return;

    AnalysisPauser pauser(m_context);

    for (auto sink: sinks)
    {
        sink->setEnabled(enabled);
    }

    m_context->getAnalysis()->setModified(true);
    repopulate();
}

void EventWidgetPrivate::removeSinks(const QVector<SinkInterface *> sinks)
{
    if (sinks.isEmpty())
        return;

    AnalysisPauser pauser(m_context);

    for (auto sink: sinks)
    {
        m_context->getAnalysis()->removeOperator(sink);
    }

    repopulate();
    m_analysisWidget->updateAddRemoveUserLevelButtons();
}

void EventWidgetPrivate::removeDirectoryRecursively(const DirectoryPtr &dir)
{
    auto analysis = m_context->getAnalysis();
    auto objects = analysis->getDirectoryContents(dir);

    if (!objects.isEmpty())
    {
        AnalysisPauser pauser(m_context);
        analysis->removeDirectoryRecursively(dir);
    }
    else
    {
        analysis->removeDirectory(dir);
    }

    repopulate();
}

static const u32 EventWidgetPeriodicRefreshInterval_ms = 1000;

void run_userlevel_visibility_dialog(QVector<bool> &hiddenLevels, QWidget *parent = 0)
{
    auto listWidget = new QListWidget;

    for (s32 idx = 0; idx < hiddenLevels.size(); ++idx)
    {
        auto item = new QListWidgetItem(QString("Level %1").arg(idx));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(hiddenLevels[idx] ? Qt::Unchecked : Qt::Checked);
        listWidget->addItem(item);
    }

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto layout = new QVBoxLayout;
    layout->addWidget(listWidget);
    layout->addWidget(buttonBox);
    layout->setStretch(0, 1);

    QDialog dialog(parent);
    dialog.setWindowTitle(QSL("Select processing levels to show"));
    dialog.setLayout(layout);
    add_widget_close_action(&dialog);

    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted)
    {
        for (s32 idx = 0; idx < listWidget->count(); ++idx)
        {
            auto item = listWidget->item(idx);
            hiddenLevels[idx] = (item->checkState() == Qt::Unchecked);
        }
    }
}

EventWidget::EventWidget(MVMEContext *ctx, const QUuid &eventId, int eventIndex,
                         AnalysisWidget *analysisWidget, QWidget *parent)
    : QWidget(parent)
    , m_d(new EventWidgetPrivate)
{
    qDebug() << __PRETTY_FUNCTION__ << this << "event =" << eventId;
    *m_d = {};
    m_d->m_q = this;
    m_d->m_context = ctx;
    m_d->m_eventId = eventId;
    m_d->m_eventIndex = eventIndex;
    m_d->m_analysisWidget = analysisWidget;
    m_d->m_displayRefreshTimer = new QTimer(this);
    m_d->m_displayRefreshTimer->start(EventWidgetPeriodicRefreshInterval_ms);

    auto outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // Row frames and splitter:
    // Two rows, the top one containing Modules and Operators, the bottom one
    // containing histograms.
    auto rowSplitter = new QSplitter(Qt::Vertical);
    outerLayout->addWidget(rowSplitter);

    auto operatorFrame = new QFrame;
    auto operatorFrameLayout = new QHBoxLayout(operatorFrame);
    operatorFrameLayout->setContentsMargins(2, 2, 2, 2);
    rowSplitter->addWidget(operatorFrame);

    auto displayFrame = new QFrame;
    auto displayFrameLayout = new QHBoxLayout(displayFrame);
    displayFrameLayout->setContentsMargins(2, 2, 2, 2);
    rowSplitter->addWidget(displayFrame);

    // Column frames and splitters:
    // One column for each user level
    m_d->m_operatorFrameSplitter = new QSplitter;
    m_d->m_operatorFrameSplitter->setChildrenCollapsible(false);
    operatorFrameLayout->addWidget(m_d->m_operatorFrameSplitter);

    m_d->m_displayFrameSplitter = new QSplitter;
    m_d->m_displayFrameSplitter->setChildrenCollapsible(false);
    displayFrameLayout->addWidget(m_d->m_displayFrameSplitter);

    auto sync_splitters = [](QSplitter *sa, QSplitter *sb)
    {
        auto sync_one_way = [](QSplitter *src, QSplitter *dst)
        {
            connect(src, &QSplitter::splitterMoved, dst, [src, dst](int, int) {
                dst->setSizes(src->sizes());
            });
        };

        sync_one_way(sa, sb);
        sync_one_way(sb, sa);
    };

    sync_splitters(m_d->m_operatorFrameSplitter, m_d->m_displayFrameSplitter);


    /* ToolBar creation. Note that these toolbars are not directly added to the
     * widget but instead they're handled by AnalysisWidget via getToolBar()
     * and getEventSelectAreaToolBar(). */

    // Upper ToolBar actions

    m_d->m_actionImportForModuleFromTemplate = std::make_unique<QAction>("Import from template");
    m_d->m_actionImportForModuleFromFile     = std::make_unique<QAction>("Import from file");
    m_d->m_actionModuleImport = new QWidgetAction(this);
    {
        auto menu = new QMenu(this);
        menu->addAction(m_d->m_actionImportForModuleFromTemplate.get());
        menu->addAction(m_d->m_actionImportForModuleFromFile.get());

        auto toolButton = new QToolButton;
        toolButton->setMenu(menu);
        toolButton->setPopupMode(QToolButton::InstantPopup);
        toolButton->setIcon(QIcon(QSL(":/analysis_module_import.png")));
        toolButton->setText(QSL("Import module objects"));
        toolButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        auto font = toolButton->font();
        font.setPointSize(7);
        toolButton->setFont(font);

        m_d->m_actionModuleImport->setDefaultWidget(toolButton);
    }

    connect(m_d->m_actionImportForModuleFromTemplate.get(), &QAction::triggered, this, [this] {
        m_d->importForModuleFromTemplate();
    });

    connect(m_d->m_actionImportForModuleFromFile.get(), &QAction::triggered, this, [this] {
        m_d->importForModuleFromFile();
    });

    // create the upper toolbar
    {
        m_d->m_upperToolBar = make_toolbar();
        auto tb = m_d->m_upperToolBar;

        //tb->addWidget(new QLabel(QString("Hello, event! %1").arg((uintptr_t)this)));
    }

    // Lower ToolBar, to the right of the event selection combo
    m_d->m_actionSelectVisibleLevels = new QAction(QIcon(QSL(":/eye_pencil.png")),
                                                   QSL("Level Visiblity"), this);

    connect(m_d->m_actionSelectVisibleLevels, &QAction::triggered, this, [this] {

        m_d->m_hiddenUserLevels.resize(m_d->m_levelTrees.size());

        run_userlevel_visibility_dialog(m_d->m_hiddenUserLevels, this);

        for (s32 idx = 0; idx < m_d->m_hiddenUserLevels.size(); ++idx)
        {
            m_d->m_levelTrees[idx].operatorTree->setVisible(!m_d->m_hiddenUserLevels[idx]);
            m_d->m_levelTrees[idx].sinkTree->setVisible(!m_d->m_hiddenUserLevels[idx]);
        }
    });

    // Export
    {
        auto action = new QAction(QIcon(QSL(":/folder_export.png")), QSL("Export"), this);
        action->setToolTip(QSL("Export selected objects to file."));
        action->setStatusTip(action->toolTip());
        m_d->m_actionExport = action;
        connect(m_d->m_actionExport, &QAction::triggered, this, [this] {
            m_d->actionExport();
        });
    }

    // Import
    {
        auto action = new QAction(QIcon(QSL(":/folder_import.png")), QSL("Import"), this);
        action->setToolTip(QSL("Import objects from file to this event."));
        action->setStatusTip(action->toolTip());
        m_d->m_actionImport = action;
        connect(m_d->m_actionImport, &QAction::triggered, this, [this] {
            m_d->actionImport();
        });
    }

    // Event settings action
    QAction *actionEventSettings = new QAction(
        QIcon(QSL(":/gear.png")), QSL("Event settings"), this);

    connect(actionEventSettings, &QAction::triggered, this, [this] {
        auto analysis = m_d->m_context->getAnalysis();

        EventSettingsDialog dialog(analysis->getVMEObjectSettings(m_d->m_eventId), this);

        if (dialog.exec() == QDialog::Accepted)
        {
            analysis->setVMEObjectSettings(m_d->m_eventId, dialog.getSettings());
        }
    });

    m_d->m_eventRateLabel = new QLabel;

    // create the lower toolbar
    {
        m_d->m_eventSelectAreaToolBar = make_toolbar();
        auto tb = m_d->m_eventSelectAreaToolBar;

        tb->setIconSize(QSize(16, 16));
        tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        tb->addWidget(m_d->m_eventRateLabel);
        tb->addSeparator();
        tb->addAction(m_d->m_actionSelectVisibleLevels);
        tb->addAction(actionEventSettings);
        tb->addSeparator();
        tb->addAction(m_d->m_actionExport);
        tb->addAction(m_d->m_actionImport);

#ifndef QT_NO_DEBUG
        tb->addSeparator();
        tb->addAction(QSL("Repopulate (dev)"), this, [this]() { m_d->repopulate(); });
#endif
    }

    m_d->repopulate();
}

EventWidget::~EventWidget()
{
    qDebug() << __PRETTY_FUNCTION__ << this << "event =" << m_d->m_eventId;

    if (m_d->m_uniqueWidget)
    {
        if (auto dialog = qobject_cast<QDialog *>(m_d->m_uniqueWidget))
        {
            dialog->reject();
        }
    }

    delete m_d;
}

void EventWidget::selectInputFor(Slot *slot, s32 userLevel, SelectInputCallback callback,
                                 QSet<PipeSourceInterface *> additionalInvalidSources)
{
    qDebug() << __PRETTY_FUNCTION__;

    m_d->m_inputSelectInfo.slot = slot;
    m_d->m_inputSelectInfo.userLevel = userLevel;
    m_d->m_inputSelectInfo.callback = callback;
    m_d->m_inputSelectInfo.additionalInvalidSources = additionalInvalidSources;

    //qDebug() << __PRETTY_FUNCTION__ << "additionalInvalidSources ="
    //    << additionalInvalidSources;

    m_d->m_mode = EventWidgetPrivate::SelectInput;
    m_d->modeChanged();
    // The actual input selection is handled in onNodeClicked()
}

void EventWidget::endSelectInput()
{
    if (m_d->m_mode == EventWidgetPrivate::SelectInput)
    {
        m_d->m_mode = EventWidgetPrivate::Default;
        m_d->m_inputSelectInfo = {};
        m_d->modeChanged();
    }
}

void EventWidget::highlightInputOf(Slot *slot, bool doHighlight)
{
    if (!slot || !slot->isParamIndexInRange())
        return;

    QTreeWidgetItem *node = nullptr;

    if (auto source = qobject_cast<SourceInterface *>(slot->inputPipe->getSource()))
    {
        // As the input is a SourceInterface we only need to look in the source tree
        auto tree = m_d->m_levelTrees[0].operatorTree;

        node = findFirstNode(tree->invisibleRootItem(), [source](auto nodeToTest) {
            return (nodeToTest->type() == NodeType_Source
                    && get_pointer<SourceInterface>(nodeToTest) == source);
        });

    }
    else if (qobject_cast<OperatorInterface *>(slot->inputPipe->getSource()))
    {
        // The input is another operator
        for (s32 treeIndex = 1;
             treeIndex < m_d->m_levelTrees.size() && !node;
             ++treeIndex)
        {
            auto tree = m_d->m_levelTrees[treeIndex].operatorTree;

            node = findFirstNode(tree->invisibleRootItem(), [slot](auto nodeToTest) {
                return (nodeToTest->type() == NodeType_OutputPipe
                        && get_pointer<Pipe>(nodeToTest) == slot->inputPipe);
            });
        }
    }
    else
    {
        InvalidCodePath;
    }

    if (node && slot->isParameterConnection() && slot->paramIndex < node->childCount())
    {
        node = node->child(slot->paramIndex);
    }

    if (node)
    {
        auto highlight_node = [doHighlight](QTreeWidgetItem *node, const QColor &color)
        {
            if (doHighlight)
            {
                node->setBackground(0, color);
            }
            else
            {
                node->setBackground(0, QColor(0, 0, 0, 0));
            }
        };

        highlight_node(node, InputNodeOfColor);

        for (node = node->parent();
             node;
             node = node->parent())
        {
            highlight_node(node, ChildIsInputNodeOfColor);
        }
    }
}

//
// Extractor add/edit/cancel
//
void EventWidget::addExtractorDialogAccepted()
{
    qDebug() << __PRETTY_FUNCTION__;
    uniqueWidgetCloses();
    m_d->repopulate();
}

void EventWidget::editExtractorDialogAccepted()
{
    qDebug() << __PRETTY_FUNCTION__;
    uniqueWidgetCloses();
    m_d->repopulate();
}

void EventWidget::addEditExtractorDialogRejected()
{
    endSelectInput();
    uniqueWidgetCloses();
}

//
// ListFilter Extractor
//
void EventWidget::listFilterExtractorDialogAccepted()
{
    qDebug() << __PRETTY_FUNCTION__;
    m_d->repopulate();
    uniqueWidgetCloses();
}

void EventWidget::listFilterExtractorDialogApplied()
{
    qDebug() << __PRETTY_FUNCTION__;
    m_d->repopulate();
}

void EventWidget::listFilterExtractorDialogRejected()
{
    qDebug() << __PRETTY_FUNCTION__;
    uniqueWidgetCloses();
}

//
// Operator add/edit/cancel
//
void EventWidget::addOperatorDialogAccepted()
{
    qDebug() << __PRETTY_FUNCTION__;

    endSelectInput();
    uniqueWidgetCloses();
    m_d->repopulate();
    m_d->m_analysisWidget->updateAddRemoveUserLevelButtons();
}

void EventWidget::editOperatorDialogAccepted()
{
    endSelectInput();
    uniqueWidgetCloses();
    m_d->repopulate();
}

void EventWidget::addEditOperatorDialogRejected()
{
    endSelectInput();
    uniqueWidgetCloses();
    m_d->repopulate();
}

//
//
//

void EventWidget::removeOperator(OperatorInterface *op)
{
    AnalysisPauser pauser(m_d->m_context);
    m_d->m_context->getAnalysis()->removeOperator(op);
    m_d->repopulate();
    m_d->m_analysisWidget->updateAddRemoveUserLevelButtons();
}

void EventWidget::toggleSinkEnabled(SinkInterface *sink)
{
    AnalysisPauser pauser(m_d->m_context);
    sink->setEnabled(!sink->isEnabled());
    m_d->m_context->getAnalysis()->setModified(true);
    m_d->repopulate();
}

void EventWidget::removeSource(SourceInterface *src)
{
    AnalysisPauser pauser(m_d->m_context);
    m_d->m_context->getAnalysis()->removeSource(src);
    m_d->repopulate();
}

void EventWidget::uniqueWidgetCloses()
{
    m_d->m_uniqueWidget = nullptr;
}

void EventWidget::addUserLevel()
{
    m_d->addUserLevel();
}

void EventWidget::removeUserLevel()
{
    m_d->removeUserLevel();
}

void EventWidget::repopulate()
{
    m_d->repopulate();
}

QToolBar *EventWidget::getToolBar()
{
    return m_d->m_upperToolBar;
}

QToolBar *EventWidget::getEventSelectAreaToolBar()
{
    return m_d->m_eventSelectAreaToolBar;
}

MVMEContext *EventWidget::getContext() const
{
    return m_d->m_context;
}

AnalysisWidget *EventWidget::getAnalysisWidget() const
{
    return m_d->m_analysisWidget;
}

Analysis *EventWidget::getAnalysis() const
{
    return m_d->m_context->getAnalysis();
}

RunInfo EventWidget::getRunInfo() const
{
    return getContext()->getRunInfo();
}

VMEConfig *EventWidget::getVMEConfig() const
{
    return getContext()->getVMEConfig();
}

QUuid EventWidget::getEventId() const
{
    return m_d->m_eventId;
}

struct AnalysisWidgetPrivate
{
    AnalysisWidget *m_q;
    MVMEContext *m_context;
    QHash<QUuid, EventWidget *> m_eventWidgetsByEventId;


    QToolBar *m_toolbar;
    QComboBox *m_eventSelectCombo;
    QStackedWidget *m_eventWidgetStack;
    QStackedWidget *m_eventWidgetToolBarStack;
    QStackedWidget *m_eventWidgetEventSelectAreaToolBarStack;
    QToolButton *m_removeUserLevelButton;
    QToolButton *m_addUserLevelButton;
    QStatusBar *m_statusBar;
    QLabel *m_labelSinkStorageSize;
    QLabel *m_labelTimetickCount;
    QLabel *m_statusLabelA2;
    QLabel *m_labelEfficiency;
    QTimer *m_periodicUpdateTimer;
    WidgetGeometrySaver *m_geometrySaver;
    AnalysisInfoWidget *m_infoWidget = nullptr;
    QAction *m_actionPause;
    QAction *m_actionStepNextEvent;

    void repopulate();
    void repopulateEventSelectCombo();
    void doPeriodicUpdate();

    void closeAllUniqueWidgets();
    void closeAllHistogramWidgets();

    void updateActions();

    void actionNew();
    void actionOpen();
    QPair<bool, QString> actionSave();
    QPair<bool, QString> actionSaveAs();
    void actionClearHistograms();
#ifdef MVME_ENABLE_HDF5
    void actionSaveSession();
    void actionLoadSession();
#endif
    void actionExploreWorkspace();
    void actionPause(bool isChecked);
    void actionStepNextEvent();

    void updateWindowTitle();
    void updateAddRemoveUserLevelButtons();
};

// Clears the stacked widget and deletes its child widgets
static void clear_stacked_widget(QStackedWidget *stackedWidget)
{
    while (auto widget = stackedWidget->currentWidget())
    {
        stackedWidget->removeWidget(widget);
        widget->deleteLater();
    }
    Q_ASSERT(stackedWidget->count() == 0);
}

void AnalysisWidgetPrivate::repopulate()
{
    clear_stacked_widget(m_eventWidgetEventSelectAreaToolBarStack);
    clear_stacked_widget(m_eventWidgetToolBarStack);
    clear_stacked_widget(m_eventWidgetStack);
    m_eventWidgetsByEventId.clear();

    // Repopulate combobox and stacked widget
    auto eventConfigs = m_context->getEventConfigs();

    // FIXME: event creation is still entirely based on the DAQ config. events
    //        that do exist in the analysis but not in the DAQ won't show up at all
    for (s32 eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto eventId = eventConfig->getId();
        auto eventWidget = new EventWidget(m_context, eventId, eventIndex, m_q);

        auto scrollArea = new QScrollArea;
        scrollArea->setWidget(eventWidget);
        scrollArea->setWidgetResizable(true);

        m_eventWidgetStack->addWidget(scrollArea);
        m_eventWidgetToolBarStack->addWidget(eventWidget->getToolBar());
        m_eventWidgetEventSelectAreaToolBarStack->addWidget(eventWidget->getEventSelectAreaToolBar());
        m_eventWidgetsByEventId[eventId] = eventWidget;
    }

    repopulateEventSelectCombo();

    updateWindowTitle();
    updateAddRemoveUserLevelButtons();
}

void AnalysisWidgetPrivate::repopulateEventSelectCombo()
{
    const QUuid lastSelectedEventId = m_eventSelectCombo->currentData().toUuid();
    m_eventSelectCombo->clear();

    auto eventConfigs = m_context->getEventConfigs();

    s32 comboIndexToSelect = -1;

    for (s32 eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto eventId = eventConfig->getId();

        QObject::disconnect(eventConfig, &ConfigObject::modified,
                            m_q, &AnalysisWidget::eventConfigModified);

        QObject::connect(eventConfig, &ConfigObject::modified,
                         m_q, &AnalysisWidget::eventConfigModified);

        m_eventSelectCombo->addItem(eventConfig->objectName(), eventId);
        qDebug() << __PRETTY_FUNCTION__ << eventConfig->objectName() << eventId << eventIndex;

        if (eventId == lastSelectedEventId)
            comboIndexToSelect = eventIndex;
    }

    if (!lastSelectedEventId.isNull() && comboIndexToSelect < m_eventSelectCombo->count())
    {
        m_eventSelectCombo->setCurrentIndex(comboIndexToSelect);
    }
}

void AnalysisWidgetPrivate::doPeriodicUpdate()
{
    for (auto eventWidget: m_eventWidgetsByEventId.values())
    {
        eventWidget->m_d->doPeriodicUpdate();
    }
}

void AnalysisWidgetPrivate::closeAllUniqueWidgets()
{
    for (auto eventWidget: m_eventWidgetsByEventId.values())
    {
        if (eventWidget->m_d->m_uniqueWidget)
        {
                eventWidget->m_d->m_uniqueWidget->close();
                eventWidget->uniqueWidgetCloses();
        }
    }
}

/* Close any open histograms belonging to the current analysis. */
void AnalysisWidgetPrivate::closeAllHistogramWidgets()
{
    auto close_if_not_null = [](QWidget *widget)
    {
        if (widget)
            widget->close();
    };

    for (const auto &op: m_context->getAnalysis()->getOperators())
    {
        if (auto sink = qobject_cast<Histo1DSink *>(op.get()))
        {
            close_if_not_null(m_context->getObjectWidget(sink));

            for (const auto &histoPtr: sink->m_histos)
            {
                close_if_not_null(m_context->getObjectWidget(histoPtr.get()));
            }
        }
        else if (auto sink = qobject_cast<Histo2DSink *>(op.get()))
        {
            close_if_not_null(m_context->getObjectWidget(sink));
        }
    }
}

void AnalysisWidgetPrivate::actionNew()
{
    if (m_context->getAnalysis()->isModified())
    {
        QMessageBox msgBox(
            QMessageBox::Question, QSL("Save analysis configuration?"),
            QSL("The current analysis configuration has modifications. Do you want to save it?"),
            QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);

        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!actionSave().first)
                return;
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
        // else discard
    }

    /* Close any active unique widgets _before_ replacing the analysis as the
     * unique widgets might perform actions on the analysis in their reject()
     * code. */
    closeAllUniqueWidgets();
    closeAllHistogramWidgets();

    AnalysisPauser pauser(m_context);
    m_context->getAnalysis()->clear();
    m_context->getAnalysis()->setModified(false);
    m_context->setAnalysisConfigFileName(QString());
    m_context->analysisWasCleared();
    repopulate();
}

void AnalysisWidgetPrivate::actionOpen()
{
    auto path = m_context->getWorkspaceDirectory();
    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getOpenFileName(m_q, QSL("Load analysis config"), path, AnalysisFileFilter);

    if (fileName.isEmpty())
        return;

    if (m_context->getAnalysis()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save analysis configuration?"),
                           QSL("The current analysis configuration has modifications."
                               " Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!actionSave().first)
                return;
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
        // else discard
    }

    closeAllUniqueWidgets();
    closeAllHistogramWidgets();

    m_context->loadAnalysisConfig(fileName);
}

QPair<bool, QString> AnalysisWidgetPrivate::actionSave()
{
    QString fileName = m_context->getAnalysisConfigFileName();

    if (fileName.isEmpty())
    {
        return actionSaveAs();
    }
    else
    {
        auto result = saveAnalysisConfig(m_context->getAnalysis(), fileName,
                                         m_context->getWorkspaceDirectory(),
                                         AnalysisFileFilter,
                                         m_context);
        if (result.first)
        {
            m_context->setAnalysisConfigFileName(result.second);
            m_context->getAnalysis()->setModified(false);
            m_context->analysisWasSaved();
        }

        return result;
    }
}

QPair<bool, QString> AnalysisWidgetPrivate::actionSaveAs()
{
    auto result = saveAnalysisConfigAs(m_context->getAnalysis(),
                                       m_context->getWorkspaceDirectory(),
                                       AnalysisFileFilter,
                                       m_context);

    if (result.first)
    {
        m_context->setAnalysisConfigFileName(result.second);
        m_context->getAnalysis()->setModified(false);
    }

    return result;
}

#if 0
void AnalysisWidgetPrivate::actionImport()
{
    // Step 0) Let the user pick a file
    // Step 1) Create Analysis from file contents
    // Step 2) Generate new IDs for analysis objects
    // Step 3) Try auto-assignment of modules
    // Step 4) If auto assignment fails run the assignment gui
    // Step 5) Add the remaining objects into the existing analysis

    // Step 0) Let the user pick a file
    auto path = m_context->getWorkspaceDirectory();
    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getOpenFileName(m_q, QSL("Import analysis"),
                                                    path, AnalysisFileFilter);

    if (fileName.isEmpty())
        return;

    // Step 1) Create Analysis from file contents
    QJsonDocument doc(gui_read_json_file(fileName));
    auto json = doc.object()[QSL("AnalysisNG")].toObject();

    Analysis analysis;
    auto readResult = analysis.read(json, m_context->getVMEConfig());

    if (!readResult)
    {
        readResult.errorData["Source file"] = fileName;
        QMessageBox::critical(m_q,
                              QSL("Error importing analysis"),
                              readResult.toRichText());
        return;
    }

    // Step 2) Generate new IDs for analysis objects
    generate_new_object_ids(&analysis);

    // Step 3) Try auto-assignment of modules
    using namespace vme_analysis_common;

    if (!auto_assign_vme_modules(m_context->getVMEConfig(), &analysis))
    {
    // Step 4) If auto assignment fails run the assignment gui
        if (!run_vme_analysis_module_assignment_ui(m_context->getVMEConfig(), &analysis))
            return;
    }

    remove_analysis_objects_unless_matching(&analysis, m_context->getVMEConfig());

    m_context->logMessage(QString("Importing %1").arg(info_string(&analysis)));

    if (analysis.isEmpty())
        return;

    auto sources = analysis.getSources();
    auto operators = analysis.getOperators();
    // FIXME: directories

#ifndef QT_NO_DEBUG
    {
        QSet<QUuid> vmeEventIds;
        QSet<QUuid> vmeModuleIds;
        for (auto ec: m_context->getVMEConfig()->getEventConfigs())
        {
            vmeEventIds.insert(ec->getId());

            for (auto mc: ec->getModuleConfigs())
            {
                vmeModuleIds.insert(mc->getId());
            }
        }

        for (auto source: sources)
        {
            Q_ASSERT(vmeEventIds.contains(source->getEventId()));
            Q_ASSERT(vmeModuleIds.contains(source->getModuleId()));
        }

        for (auto op: operators)
        {
            Q_ASSERT(vmeEventIds.contains(op->getEventId()));
        }
    }
#endif

    // Step 5) Add the remaining objects into the existing analysis
    AnalysisPauser pauser(m_context);
    auto targetAnalysis = m_context->getAnalysis();

    QHash<QUuid, s32> eventMaxUserLevels;
    for (auto op: targetAnalysis->getOperators())
    {
        auto eventId = op->getEventId();

        if (!eventMaxUserLevels.contains(eventId))
        {
            eventMaxUserLevels.insert(eventId, targetAnalysis->getMaxUserLevel(eventId));
        }
    }

    for (auto source: sources)
    {
        targetAnalysis->addSource(source->getEventId(), source->getModuleId(), source);
    }

    for (auto op: operators)
    {
        s32 baseUserLevel = eventMaxUserLevels.value(op->getEventId(), 0);
        s32 targetUserLevel = baseUserLevel + op->getUserLevel();
        if (op->getUserLevel() == 0 && qobject_cast<SinkInterface *>(op.get()))
        {
            targetUserLevel = 0;
        }
        targetAnalysis->addOperator(op->getEventId(), targetUserLevel, op);
    }

    repopulate();
}
#endif

void AnalysisWidgetPrivate::actionClearHistograms()
{
    AnalysisPauser pauser(m_context);

    for (auto &op: m_context->getAnalysis()->getOperators())
    {
        if (auto histoSink = qobject_cast<Histo1DSink *>(op.get()))
        {
            for (auto &histo: histoSink->m_histos)
            {
                histo->clear();
            }
        }
        else if (auto histoSink = qobject_cast<Histo2DSink *>(op.get()))
        {
            if (histoSink->m_histo)
            {
                histoSink->m_histo->clear();
            }
        }
    }
}

#ifdef MVME_ENABLE_HDF5

static const QString SessionFileFilter = QSL("MVME Sessions (*.hdf5);; All Files (*.*)");
static const QString SessionFileExtension = QSL(".hdf5");

void handle_session_error(const QString &title, const QString &message)
{
    SessionErrorDialog dialog(title, message);
    dialog.exec();

    //m_context->logMessage(QString("Error saving session:"));
    //m_context->logMessageRaw(result.second);
}

void AnalysisWidgetPrivate::actionSaveSession()
{
    qDebug() << __PRETTY_FUNCTION__;

    using ResultType = QPair<bool, QString>;

    ResultType result;

    auto sessionPath = m_context->getWorkspacePath(QSL("SessionDirectory"));

    if (sessionPath.isEmpty())
    {
        sessionPath = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString filename = QFileDialog::getSaveFileName(
        m_q, QSL("Save session"), sessionPath, SessionFileFilter);

    if (filename.isEmpty())
        return;

    QFileInfo fileInfo(filename);

    if (fileInfo.completeSuffix().isEmpty())
    {
        filename += SessionFileExtension;
    }

    AnalysisPauser pauser(m_context);

#if 1 // The QtConcurrent path
    QProgressDialog progressDialog;
    progressDialog.setLabelText(QSL("Saving session..."));
    progressDialog.setMinimum(0);
    progressDialog.setMaximum(0);

    QFutureWatcher<ResultType> watcher;
    QObject::connect(&watcher, &QFutureWatcher<ResultType>::finished,
                     &progressDialog, &QDialog::close);

    QFuture<ResultType> future = QtConcurrent::run(save_analysis_session, filename,
                                                   m_context->getAnalysis());
    watcher.setFuture(future);

    progressDialog.exec();

    result = future.result();
#else // The blocking path
    result = save_analysis_session(filename, m_context->getAnalysis());
#endif

    if (!result.first)
    {
        handle_session_error(result.second, "Error saving session");
    }
}

void AnalysisWidgetPrivate::actionLoadSession()
{
    auto sessionPath = m_context->getWorkspacePath(QSL("SessionDirectory"));

    if (sessionPath.isEmpty())
    {
        sessionPath = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString filename = QFileDialog::getOpenFileName(
        m_q, QSL("Load session"), sessionPath, SessionFileFilter);

    if (filename.isEmpty())
        return;

    AnalysisPauser pauser(m_context);

    QProgressDialog progressDialog;
    progressDialog.setLabelText(QSL("Loading session config..."));
    progressDialog.setMinimum(0);
    progressDialog.setMaximum(0);
    progressDialog.show();

    QEventLoop loop;

    QJsonDocument analysisJson;

    // load the config first
    {
#if 1 // The QtConcurrent path
        using ResultType = QPair<QJsonDocument, QString>;

        QFutureWatcher<ResultType> watcher;
        QObject::connect(&watcher, &QFutureWatcher<ResultType>::finished, &loop, &QEventLoop::quit);

        QFuture<ResultType> future = QtConcurrent::run(load_analysis_config_from_session_file, filename);
        watcher.setFuture(future);

        loop.exec();

        auto result = future.result();
#else // The blocking path
        auto result = load_analysis_config_from_session_file(filename);
#endif

        if (result.first.isNull())
        {
            progressDialog.hide();

            handle_session_error(result.second, "Error loading session config");

            //m_context->logMessage(QString("Error loading session:"));
            //m_context->logMessageRaw(result.second);
            return;
        }

        analysisJson = QJsonDocument(result.first);
    }

    if (m_context->getAnalysis()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save analysis configuration?"),
                           QSL("The current analysis configuration has modifications."
                               " Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);

        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            auto result = saveAnalysisConfig(
                m_context->getAnalysis(),
                m_context->getAnalysisConfigFileName(),
                m_context->getWorkspaceDirectory(),
                AnalysisFileFilter,
                m_context);

            if (!result.first)
            {
                m_context->logMessage(QSL("Error: ") + result.second);
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    // This is the standard procedure when loading an analysis config
    closeAllUniqueWidgets();
    closeAllHistogramWidgets();

    if (m_context->loadAnalysisConfig(analysisJson, filename, { .NoAutoResume = true }))
    {
        m_context->setAnalysisConfigFileName(QString());
        progressDialog.setLabelText(QSL("Loading session data..."));


#if 1 // The QtConcurrent path
        using ResultType = QPair<bool, QString>;

        QFutureWatcher<ResultType> watcher;
        QObject::connect(&watcher, &QFutureWatcher<ResultType>::finished, &loop, &QEventLoop::quit);

        QFuture<ResultType> future = QtConcurrent::run(load_analysis_session, filename,
                                                       m_context->getAnalysis());
        watcher.setFuture(future);

        loop.exec();

        auto result = future.result();
#else // The blocking path
        auto result = load_analysis_session(filename, m_context->getAnalysis());
#endif

        if (!result.first)
        {
            handle_session_error(result.second, "Error loading session data");
            return;
        }
    }
}
#endif

void AnalysisWidgetPrivate::updateActions()
{
    auto streamWorker = m_context->getMVMEStreamWorker();
    auto workerState = streamWorker->getState();

    switch (workerState)
    {
        case MVMEStreamWorkerState::Idle:
            m_actionPause->setIcon(QIcon(":/control_pause.png"));
            m_actionPause->setText(QSL("Pause"));
            m_actionPause->setEnabled(true);
            m_actionPause->setChecked(streamWorker->getStartPaused());
            m_actionStepNextEvent->setEnabled(false);
            break;

        case MVMEStreamWorkerState::Running:
            m_actionPause->setIcon(QIcon(":/control_pause.png"));
            m_actionPause->setText(QSL("Pause"));
            m_actionPause->setEnabled(true);
            m_actionPause->setChecked(false);
            m_actionStepNextEvent->setEnabled(false);
            break;

        case MVMEStreamWorkerState::Paused:
            m_actionPause->setIcon(QIcon(":/control_play.png"));
            m_actionPause->setText(QSL("Resume"));
            m_actionPause->setEnabled(true);
            m_actionPause->setChecked(true);
            m_actionStepNextEvent->setEnabled(true);
            break;

        case MVMEStreamWorkerState::SingleStepping:
            m_actionPause->setEnabled(false);
            m_actionStepNextEvent->setEnabled(false);
            break;
    }
}

void AnalysisWidgetPrivate::actionExploreWorkspace()
{
    QString path = m_context->getWorkspaceDirectory();

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void AnalysisWidgetPrivate::actionPause(bool actionIsChecked)
{
    auto streamWorker = m_context->getMVMEStreamWorker();
    auto workerState = streamWorker->getState();

    switch (workerState)
    {
        case MVMEStreamWorkerState::Idle:
            streamWorker->setStartPaused(actionIsChecked);
            break;

        case MVMEStreamWorkerState::Running:
            streamWorker->pause();
            break;

        case MVMEStreamWorkerState::Paused:
            streamWorker->resume();
            break;

        case MVMEStreamWorkerState::SingleStepping:
            // cannot pause/resume during the time a singlestep is active
            InvalidCodePath;
            break;
    }
}

void AnalysisWidgetPrivate::actionStepNextEvent()
{
    auto streamWorker = m_context->getMVMEStreamWorker();
    auto workerState = streamWorker->getState();

    switch (workerState)
    {
        case MVMEStreamWorkerState::Idle:
        case MVMEStreamWorkerState::Running:
        case MVMEStreamWorkerState::SingleStepping:
            InvalidCodePath;
            break;

        case MVMEStreamWorkerState::Paused:
            streamWorker->singleStep();
            break;
    }
}

void AnalysisWidgetPrivate::updateWindowTitle()
{
    QString fileName = m_context->getAnalysisConfigFileName();

    if (fileName.isEmpty())
        fileName = QSL("<not saved>");

    auto wsDir = m_context->getWorkspaceDirectory() + '/';

    if (fileName.startsWith(wsDir))
        fileName.remove(wsDir);

    auto title = QString(QSL("%1 - [Analysis UI]")).arg(fileName);

    if (m_context->getAnalysis()->isModified())
    {
        title += " *";
    }

    m_q->setWindowTitle(title);
}

void AnalysisWidgetPrivate::updateAddRemoveUserLevelButtons()
{
    qDebug() << __PRETTY_FUNCTION__;

    QUuid eventId = m_eventSelectCombo->currentData().toUuid();
    auto analysis = m_context->getAnalysis();
    s32 maxUserLevel = 0;

    for (const auto &op: analysis->getOperators(eventId))
    {
        maxUserLevel = std::max(maxUserLevel, op->getUserLevel());
    }

    s32 numUserLevels = maxUserLevel + 1;

    EventWidget *eventWidget = m_eventWidgetsByEventId.value(eventId);

    s32 visibleUserLevels = 0;

    if (eventWidget)
    {
        visibleUserLevels = eventWidget->m_d->m_levelTrees.size();
    }

    m_removeUserLevelButton->setEnabled(visibleUserLevels > 1 && visibleUserLevels > numUserLevels);
}

AnalysisWidget::AnalysisWidget(MVMEContext *ctx, QWidget *parent)
    : QWidget(parent)
    , m_d(new AnalysisWidgetPrivate)
{
    m_d->m_q = this;
    m_d->m_context = ctx;

    m_d->m_periodicUpdateTimer = new QTimer(this);
    m_d->m_periodicUpdateTimer->start(PeriodicUpdateTimerInterval_ms);
    m_d->m_geometrySaver = new WidgetGeometrySaver(this);

    /* Note: This code is not efficient at all. This AnalysisWidget and the
     * EventWidgets are recreated and repopulated more often than is really
     * necessary. Rebuilding everything when the underlying objects change was
     * just the easiest way to implement it.
     */

    auto do_repopulate_lambda = [this]() { m_d->repopulate(); };

    // DAQ config changes
    connect(m_d->m_context, &MVMEContext::daqConfigChanged, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::eventAdded, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::eventAboutToBeRemoved, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::moduleAdded, this, do_repopulate_lambda);
    connect(m_d->m_context, &MVMEContext::moduleAboutToBeRemoved, this, do_repopulate_lambda);

    // Analysis changes
    auto on_analysis_changed = [this]()
    {
        /* Assuming the old analysis has been deleted, thus no
         * QObject::disconnect() is needed. */
        connect(m_d->m_context->getAnalysis(), &Analysis::modifiedChanged, this, [this]() {
            m_d->updateWindowTitle();
        });
        m_d->repopulate();
    };

    connect(m_d->m_context, &MVMEContext::analysisChanged, this, on_analysis_changed);

    connect(m_d->m_context, &MVMEContext::analysisConfigFileNameChanged, this, [this](const QString &) {
        m_d->updateWindowTitle();
    });

    // QStackedWidgets for EventWidgets and their toolbars
    m_d->m_eventWidgetStack = new QStackedWidget;
    m_d->m_eventWidgetToolBarStack = new QStackedWidget;
    connect(m_d->m_eventWidgetStack, &QStackedWidget::currentChanged,
            m_d->m_eventWidgetToolBarStack, &QStackedWidget::setCurrentIndex);

    m_d->m_eventWidgetEventSelectAreaToolBarStack = new QStackedWidget;
    connect(m_d->m_eventWidgetStack, &QStackedWidget::currentChanged,
            m_d->m_eventWidgetEventSelectAreaToolBarStack, &QStackedWidget::setCurrentIndex);

    // toolbar
    {
        m_d->m_toolbar = make_toolbar();

        QAction *action;

        // new, open, save, save as
        m_d->m_toolbar->addAction(QIcon(":/document-new.png"), QSL("New"),
                                  this, [this]() { m_d->actionNew(); });

        m_d->m_toolbar->addAction(QIcon(":/document-open.png"), QSL("Open"),
                                  this, [this]() { m_d->actionOpen(); });

        m_d->m_toolbar->addAction(QIcon(":/document-save.png"), QSL("Save"),
                                  this, [this]() { m_d->actionSave(); });

        m_d->m_toolbar->addAction(QIcon(":/document-save-as.png"), QSL("Save As"),
                                  this, [this]() { m_d->actionSaveAs(); });

        // clear histograms
        m_d->m_toolbar->addSeparator();
        m_d->m_toolbar->addAction(QIcon(":/clear_histos.png"), QSL("Clear Histos"),
                                  this, [this]() { m_d->actionClearHistograms(); });

        // info window
        m_d->m_toolbar->addSeparator();
        m_d->m_toolbar->addAction(QIcon(":/info.png"), QSL("Info && Stats"), this, [this]() {

            AnalysisInfoWidget *widget = nullptr;

            if (m_d->m_infoWidget)
            {
                widget = m_d->m_infoWidget;
            }
            else
            {
                widget = new AnalysisInfoWidget(m_d->m_context);
                widget->setAttribute(Qt::WA_DeleteOnClose);
                add_widget_close_action(widget);
                m_d->m_geometrySaver->addAndRestore(widget, QSL("WindowGeometries/AnalysisInfo"));

                connect(widget, &QObject::destroyed, this, [this]() {
                    m_d->m_infoWidget = nullptr;
                });

                m_d->m_infoWidget = widget;
            }

            show_and_activate(widget);
        });

        // pause, resume, step
        connect(m_d->m_context->getMVMEStreamWorker(), &MVMEStreamWorker::stateChanged,
                this, [this](MVMEStreamWorkerState) { m_d->updateActions(); });

        m_d->m_toolbar->addSeparator();
        m_d->m_actionPause = m_d->m_toolbar->addAction(
            QIcon(":/control_pause.png"), QSL("Pause"),
            this, [this](bool checked) { m_d->actionPause(checked); });

        m_d->m_actionPause->setCheckable(true);

        m_d->m_actionStepNextEvent = m_d->m_toolbar->addAction(
            QIcon(":/control_play_stop.png"), QSL("Next Event"),
            this, [this] { m_d->actionStepNextEvent(); });

#ifdef MVME_ENABLE_HDF5
        m_d->m_toolbar->addSeparator();
        m_d->m_toolbar->addAction(QIcon(":/document-open.png"), QSL("Load Session"),
                                  this, [this]() { m_d->actionLoadSession(); });
        m_d->m_toolbar->addAction(QIcon(":/document-save.png"), QSL("Save Session"),
                                  this, [this]() { m_d->actionSaveSession(); });
#endif

        m_d->m_toolbar->addSeparator();

        m_d->m_toolbar->addAction(QIcon(QSL(":/folder_orange.png")), QSL("Explore Workspace"),
                                  this, [this]() { m_d->actionExploreWorkspace(); });

    }

    // After the toolbar entries the EventWidget specific action will be added.
    // See EventWidget::makeToolBar()

    auto toolbarFrame = new QFrame;
    toolbarFrame->setFrameStyle(QFrame::StyledPanel);
    auto toolbarFrameLayout = new QHBoxLayout(toolbarFrame);
    toolbarFrameLayout->setContentsMargins(0, 0, 0, 0);
    toolbarFrameLayout->setSpacing(0);
    toolbarFrameLayout->addWidget(m_d->m_toolbar);
    toolbarFrameLayout->addWidget(m_d->m_eventWidgetToolBarStack);
    toolbarFrameLayout->addStretch();

    // event select combo
    m_d->m_eventSelectCombo = new QComboBox;
    m_d->m_eventSelectCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    connect(m_d->m_eventSelectCombo,
            static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
            this, [this] (int index) {
        m_d->m_eventWidgetStack->setCurrentIndex(index);
        updateAddRemoveUserLevelButtons();
    });

    // remove user level
    m_d->m_removeUserLevelButton = new QToolButton();
    m_d->m_removeUserLevelButton->setIcon(QIcon(QSL(":/list_remove.png")));
    connect(m_d->m_removeUserLevelButton, &QPushButton::clicked, this, [this]() {
        QUuid eventId = m_d->m_eventSelectCombo->currentData().toUuid();
        EventWidget *eventWidget = m_d->m_eventWidgetsByEventId.value(eventId);
        if (eventWidget)
        {
            eventWidget->removeUserLevel();
            updateAddRemoveUserLevelButtons();
        }
    });

    // add user level
    m_d->m_addUserLevelButton = new QToolButton();
    m_d->m_addUserLevelButton->setIcon(QIcon(QSL(":/list_add.png")));

    connect(m_d->m_addUserLevelButton, &QPushButton::clicked, this, [this]() {
        QUuid eventId = m_d->m_eventSelectCombo->currentData().toUuid();
        EventWidget *eventWidget = m_d->m_eventWidgetsByEventId.value(eventId);
        if (eventWidget)
        {
            eventWidget->addUserLevel();
            updateAddRemoveUserLevelButtons();
        }
    });

    // Layout containing event select combo, a 2nd event widget specific
    // toolbar and the add and remove userlevel buttons
    auto eventSelectLayout = new QHBoxLayout;
    eventSelectLayout->addWidget(new QLabel(QSL("Event:")));
    eventSelectLayout->addWidget(m_d->m_eventSelectCombo);
    auto separatorFrame = new QFrame;
    separatorFrame->setFrameStyle(QFrame::VLine | QFrame::Sunken);
    eventSelectLayout->addWidget(separatorFrame);
    eventSelectLayout->addWidget(m_d->m_eventWidgetEventSelectAreaToolBarStack);
    eventSelectLayout->addStretch();
    eventSelectLayout->addWidget(m_d->m_removeUserLevelButton);
    eventSelectLayout->addWidget(m_d->m_addUserLevelButton);

    // statusbar
    m_d->m_statusBar = make_statusbar();
    // efficiency
    m_d->m_labelEfficiency = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_labelEfficiency);
    // timeticks label
    m_d->m_labelTimetickCount = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_labelTimetickCount);
    // histo storage label
    m_d->m_labelSinkStorageSize = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_labelSinkStorageSize);
    // a2 label
    m_d->m_statusLabelA2 = new QLabel;
    m_d->m_statusBar->addPermanentWidget(m_d->m_statusLabelA2);

    m_d->m_statusLabelA2->setText(QSL("a2::"));

    // main layout
    auto layout = new QGridLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    s32 row = 0;
    layout->addWidget(toolbarFrame, row++, 0);
    layout->addLayout(eventSelectLayout, row++, 0);
    layout->addWidget(m_d->m_eventWidgetStack, row++, 0);
    layout->setRowStretch(row-1, 1);
    layout->addWidget(m_d->m_statusBar, row++, 0);

    auto analysis = ctx->getAnalysis();

    analysis->beginRun(ctx->getRunInfo(),
                       vme_analysis_common::build_id_to_index_mapping(
                           ctx->getVMEConfig()));

    on_analysis_changed();

    // Update the histo storage size in the statusbar
    connect(m_d->m_periodicUpdateTimer, &QTimer::timeout, this, [this]() {
        double storageSize = m_d->m_context->getAnalysis()->getTotalSinkStorageSize();
        QString unit("B");

        if (storageSize > Gigabytes(1))
        {
            storageSize /= Gigabytes(1);
            unit = QSL("GiB");
        }
        else if (storageSize > Megabytes(1))
        {
            storageSize /= Megabytes(1);
            unit = QSL("MiB");
        }
        else if (storageSize == 0.0)
        {
            unit = QSL("MiB");
        }

        m_d->m_labelSinkStorageSize->setText(QString("Histo Storage: %1 %2")
                                             .arg(storageSize, 0, 'f', 2)
                                             .arg(unit));
    });

    // Update statusbar timeticks label
    connect(m_d->m_periodicUpdateTimer, &QTimer::timeout, this, [this]() {

        double tickCount = m_d->m_context->getAnalysis()->getTimetickCount();

        m_d->m_labelTimetickCount->setText(QString("Timeticks: %1 s")
                                           .arg(tickCount));


        if (!m_d->m_context->getAnalysis()->getRunInfo().isReplay)
        {

            auto daqStats = m_d->m_context->getDAQStats();
            double efficiency = daqStats.getAnalysisEfficiency();
            efficiency = std::isnan(efficiency) ? 0.0 : efficiency;

            m_d->m_labelEfficiency->setText(QString("Efficiency: %1")
                                            .arg(efficiency, 0, 'f', 2));

            auto tt = (QString("Analyzed Buffers:\t%1\n"
                               "Skipped Buffers:\t%2\n"
                               "Total Buffers:\t%3")
                       .arg(daqStats.getAnalyzedBuffers())
                       .arg(daqStats.droppedBuffers)
                       .arg(daqStats.totalBuffersRead)
                      );

            m_d->m_labelEfficiency->setToolTip(tt);
        }
        else
        {
            m_d->m_labelEfficiency->setText(QSL("Replay  |"));
            m_d->m_labelEfficiency->setToolTip(QSL(""));
        }
    });

    // Run the periodic update
    connect(m_d->m_periodicUpdateTimer, &QTimer::timeout, this, [this]() { m_d->doPeriodicUpdate(); });

    m_d->updateActions();
    resize(800, 600);
}

AnalysisWidget::~AnalysisWidget()
{
    if (m_d->m_infoWidget)
    {
        m_d->m_infoWidget->close();
    }

    delete m_d;
    qDebug() << __PRETTY_FUNCTION__;
}

void AnalysisWidget::operatorAddedExternally(const OperatorPtr &op)
{
    const auto &operators(m_d->m_context->getAnalysis()->getOperators());

    if (auto eventWidget = m_d->m_eventWidgetsByEventId.value(op->getEventId()))
    {
        eventWidget->m_d->repopulate();
    }
}

void AnalysisWidget::operatorEditedExternally(const OperatorPtr &op)
{
    const auto &operators(m_d->m_context->getAnalysis()->getOperators());

    if (auto eventWidget = m_d->m_eventWidgetsByEventId.value(op->getEventId()))
    {
        eventWidget->m_d->repopulate();
    }
}

void AnalysisWidget::updateAddRemoveUserLevelButtons()
{
    m_d->updateAddRemoveUserLevelButtons();
}

void AnalysisWidget::eventConfigModified()
{
    m_d->repopulateEventSelectCombo();
}

bool AnalysisWidget::event(QEvent *e)
{
    if (e->type() == QEvent::StatusTip)
    {
        m_d->m_statusBar->showMessage(reinterpret_cast<QStatusTipEvent *>(e)->tip());
        return true;
    }

    return QWidget::event(e);
}

} // end namespace analysis

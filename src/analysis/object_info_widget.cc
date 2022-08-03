/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "object_info_widget.h"

#include "a2_adapter.h"
#include "analysis.h"
#include "analysis_util.h"
#include "mvme_context.h"
#include "qt_util.h"
#include "graphviz_util.h"
#include "graphicsview_util.h"
#include <qgv/QGVCore/QGVScene.h>

#include <QClipboard>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QMenu>
#include <spdlog/fmt/fmt.h>
#include <sstream>
#include <memory>
#include <set>

namespace analysis
{

using namespace mesytec::graphviz_util;

struct ObjectInfoWidget::Private
{
    Private()
        : m_qgvScene("qgv")
    { }
    //std::vector<std::unique_ptr<ObjectInfoHandler>> handlers_;

    AnalysisServiceProvider *m_serviceProvider;
    AnalysisObjectPtr m_analysisObject;
    const ConfigObject *m_configObject;

    QLabel *m_infoLabel;
    QGraphicsView *m_graphView;

    //mesytec::graphviz_util::DotSvgGraphicsSceneManager m_dotManager;
    QGVScene m_qgvScene;

    void refreshGraphView(const AnalysisObjectPtr &obj);
    void showGraphViewContextMenu(const QPoint &pos);
};

std::string make_basic_label(const AnalysisObject *obj)
{
    auto label = escape_dot_string(obj->objectName().toStdString());

    if (auto ps = dynamic_cast<const PipeSourceInterface *>(obj))
    {
        label = fmt::format("<<b>{}</b><br/>{}>",
            escape_dot_string(ps->getDisplayName().toStdString()),
            label);
    }

    return label;
}

using Attributes = std::map<std::string, std::string>;

std::ostream &write_attributes(std::ostream &out, const Attributes &attributes)
{
    for (const auto &kv: attributes)
    {
        if (!kv.second.empty() && kv.second.front() == '<' && kv.second.back() == '>')
            out << fmt::format("{}={} ", kv.first, kv.second); // unquoted html value
        else
            out << fmt::format("{}=\"{}\" ", kv.first, kv.second); // quoted plain text value
    }

    return out;
}

std::ostream &write_node(std::ostream &out, const std::string &id, const std::map<std::string, std::string> &attributes)
{
    out << fmt::format("\"{}\" [id=\"{}\" ", id, id);
    write_attributes(out, attributes);
    out << "]" << std::endl;

    return out;
}

std::ostream &write_node(std::ostream &out, const QString &id, const std::map<QString, QString> &attributes)
{
    std::map<std::string, std::string> stdMap;

    for (const auto &kv: attributes)
        stdMap.insert({ kv.first.toStdString(), kv.second.toStdString() });

    return write_node(out, id.toStdString(), stdMap);
}

std::ostream &write_edge(std::ostream &out, const std::string &sourceId, const std::string &destId, const std::map<std::string, std::string> &attributes)
{
    out << fmt::format("\"{}\" -> \"{}\" [", sourceId, destId);
    write_attributes(out, attributes);
    out << "]" << std::endl;
    return out;
}

template<typename T>
std::string id_str(const T &t)
{
    return t->getId().toString().toStdString();
}

std::ostream &format_object(std::ostream &out , const AnalysisObject *obj, Attributes attribs = {})
{
    auto id = id_str(obj);
    write_node(out, id, attribs);
    return out;
}

static const char *FontName = "Bitstream Vera Sans";

void generate_dot(std::ostream &dotOut,
                  const AnalysisObjectPtr &obj,
                  std::set<QUuid> &nodeSet,
                  std::set<std::pair<QUuid, QUuid>> &edgeSet,
                  const Attributes &objOverrideAttributes = {})
{
    if (nodeSet.count(obj->getId()))
        return;

    Attributes attribs =
    {
        { "label", make_basic_label(obj.get()) },
        { "fontname", FontName },
        { "style", "filled" },
        { "fillcolor", "#fffbcc" },

    };

    for (const auto &it: objOverrideAttributes)
        attribs[it.first] = it.second;

    write_node(dotOut, id_str(obj), attribs);
    nodeSet.insert(obj->getId());

    auto ana = obj->getAnalysis();
    auto op = std::dynamic_pointer_cast<OperatorInterface>(obj);

    if (ana && op)
    {
        auto condSet = ana->getActiveConditions(op);

        // cluster for the conditions referenced by the operator
        // TODO: other operators can reference the same conditions but the
        // subgraph will be closed at the time they are visited so we will get
        // edges from some object to the condition subgraph created for the
        // current operator.
        if (!condSet.isEmpty())
        {
            dotOut << fmt::format("  subgraph \"clusterConditions{}\" {{",
                                  op->getId().toString().toStdString())
                   << std::endl;
            dotOut << "  label=Conditions" << std::endl;
            dotOut << "  style=\"filled\"" << std::endl;
            dotOut << "  fillcolor=\"#eeeeee\"" << std::endl;
            dotOut << fmt::format("  fontname=\"{}\"", FontName) << std::endl;

            for (const auto &cond : condSet)
            {
                auto label = make_basic_label(cond.get());

                if (auto exprCond = qobject_cast<const ExpressionCondition *>(cond.get()))
                {
                    auto expr = escape_dot_string(exprCond->getExpression().toStdString());

                    label = fmt::format("<<b>{}</b><br/>{}<br/><i>{}</i>>",
                                        escape_dot_string(exprCond->getDisplayName().toStdString()),
                                        escape_dot_string(exprCond->objectName().toStdString()),
                                        escape_dot_string(exprCond->getExpression().toStdString()));
                }

                std::map<std::string, std::string> attribs =
                {
                    { "label", label },
                    { "shape", "hexagon" },
                    { "fontname", FontName },
                    { "style", "filled" },
                    { "fillcolor", "lightblue" },
                };

                format_object(dotOut, cond.get(), attribs);
                nodeSet.insert(cond->getId());
            }

            dotOut << "}" << std::endl;

            // cond -> op edges (from cluster to op)
            for (const auto &cond : condSet)
            {
                auto edgeIds = std::make_pair(cond->getId(), op->getId());

                if (!edgeSet.count(edgeIds))
                {
                    write_edge(dotOut, id_str(cond), id_str(op),
                               {
                                   {"arrowhead", "diamond"},
                                   {"color", "blue"},
                               });
                    edgeSet.insert(edgeIds);
                }
            }

            // recurse over the inputs of the op
            for (s32 inputIndex = 0; inputIndex < op->getNumberOfSlots(); ++inputIndex)
            {
                auto inputSlot = op->getSlot(inputIndex);

                if (inputSlot->isConnected())
                {
                    auto nextObj = inputSlot->inputPipe->source->shared_from_this();
                    generate_dot(dotOut, nextObj, nodeSet, edgeSet);

                    auto edgeIds = std::make_pair(nextObj->getId(), op->getId());

                    if (!edgeSet.count(edgeIds))
                    {
                        // nextObj -> op input edges
                        write_edge(dotOut, id_str(nextObj), id_str(op),
                                   {
                                       { "label", inputSlot->name.toStdString() },
                                   });
                        edgeSet.insert(edgeIds);
                    }
                }
            }
        }

        // FIXME: for some reason this is never true.
        if (auto source = std::dynamic_pointer_cast<SourceInterface>(obj))
        {
            auto moduleId = source->getModuleId();

            if (!nodeSet.count(moduleId))
            {
                Attributes attribs =
                {
                    { "label", escape_dot_string(moduleId.toString().toStdString()) },
                    { "shape", "box" },
                };

                write_node(dotOut, moduleId.toString().toStdString(), attribs);
                nodeSet.insert(moduleId);

                auto edgeIds = std::make_pair(moduleId, source->getId());

                if (!edgeSet.count(edgeIds))
                {
                    write_edge(dotOut, moduleId.toString().toStdString(), id_str(source), {});
                }
            }
        }
    }
}

std::string generate_dot_graph(const AnalysisObjectPtr &obj)
{
    std::ostringstream dotOut;

    dotOut << "strict digraph {" << std::endl;
    dotOut << "  rankdir=LR" << std::endl;
    dotOut << "  id=OuterGraph" << std::endl;

    std::set<QUuid> nodeSet;
    std::set<std::pair<QUuid, QUuid>> edgeSet;

    generate_dot(dotOut, obj, nodeSet, edgeSet, { {"fillcolor", "#fff580"}, {"root", "true"} });

    dotOut << "}" << std::endl;

    return dotOut.str();
}

void ObjectInfoWidget::Private::refreshGraphView(const AnalysisObjectPtr &obj)
{
    // operator background color: fillcolor="#fffbcc"
    // condition cluster background color: bgcolor="#eeeeee"
    // conditions fillcolor=lightblue


#if 0
    // TODO: use a font that exists on each target platform
    const char *FontName = "Bitstream Vera Sans";

    std::ostringstream dotOut;
    dotOut << "strict digraph {" << std::endl;
    dotOut << "  rankdir=LR" << std::endl;
    dotOut << "  id=OuterGraph" << std::endl;
    dotOut << fmt::format("  fontname=\"{}\"", FontName) << std::endl;

    {
        Attributes attribs =
        {
            { "label", make_basic_label(obj.get()) },
            { "fontname", FontName },
            { "style", "filled" },
            { "fillcolor", "#fffbcc" },

        };
        write_node(dotOut, id_str(obj), attribs);
    }

    auto ana = obj->getAnalysis();
    auto op = std::dynamic_pointer_cast<OperatorInterface>(obj);

    if (ana && op)
    {
        auto condSet = ana->getActiveConditions(op);

        if (!condSet.isEmpty())
        {
            dotOut << fmt::format("  subgraph \"clusterConditions{}\" {{",
                                  op->getId().toString().toStdString())
                   << std::endl;
            dotOut << "  label=Conditions" << std::endl;
            dotOut << "  style=\"filled\"" << std::endl;
            dotOut << "  fillcolor=\"#eeeeee\"" << std::endl;
            dotOut << fmt::format("  fontname=\"{}\"", FontName) << std::endl;

            for (const auto &cond : condSet)
            {
                auto label = make_basic_label(cond.get());

                if (auto exprCond = qobject_cast<const ExpressionCondition *>(cond.get()))
                {
                    auto expr = escape_dot_string(exprCond->getExpression().toStdString());

                    label = fmt::format("<<b>{}</b><br/>{}<br/><i>{}</i>>",
                                        escape_dot_string(exprCond->getDisplayName().toStdString()),
                                        escape_dot_string(exprCond->objectName().toStdString()),
                                        escape_dot_string(exprCond->getExpression().toStdString()));
                }

                std::map<std::string, std::string> attribs =
                {
                    { "label", label },
                    { "shape", "hexagon" },
                    { "fontname", FontName },
                    { "style", "filled" },
                    { "fillcolor", "lightblue" },
                };

                format_object(dotOut, cond.get(), attribs);
            }

            dotOut << "}" << std::endl;

            for (const auto &cond : condSet)
            {
                //dotOut << fmt::format("\"{}\" -> \"{}\" [arrowhead=diamond, color=blue]",
                //                        id_str(op), id_str(cond))
                //        << std::endl;
                write_edge(dotOut, id_str(op), id_str(cond),
                           {
                               {"arrowhead", "diamond"},
                               {"color", "blue"},
                           });
            }
        }
    }

    dotOut << "}" << std::endl;
    spdlog::info("dot output:\n {}", dotOut.str());
    auto dotStr = dotOut.str();
#else
    auto dotStr = generate_dot_graph(obj);
#endif
    spdlog::info("dot output:\n {}", dotStr);


#if 0
    m_dotManager.setDot(dotOut.str());

    spdlog::info("dot -> svg data:\n{}\n", m_dotManager.svgData().toStdString());
#else
    m_qgvScene.loadLayout(QString::fromStdString(dotStr));
#endif

    //m_graphView->setTransform({}); // resets zoom
}

void ObjectInfoWidget::Private::showGraphViewContextMenu(const QPoint &pos)
{
#if 0
    auto menu = new QMenu;
    menu->addAction("Copy DOT code", [this]() {
        auto dotString = m_dotManager.dotString();
        auto clipboard = QGuiApplication::clipboard();
        clipboard->setText(QString::fromStdString(dotString));
    });

    menu->exec(m_graphView->mapToGlobal(pos));
    menu->deleteLater();
#endif
}

ObjectInfoWidget::ObjectInfoWidget(AnalysisServiceProvider *asp, QWidget *parent)
    : QFrame(parent)
    , m_d(std::make_unique<Private>())
{
    setFrameStyle(QFrame::NoFrame);

    m_d->m_serviceProvider = asp;
    m_d->m_infoLabel = new QLabel;
    m_d->m_infoLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_d->m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    set_widget_font_pointsize_relative(m_d->m_infoLabel, -2);

    m_d->m_graphView = new QGraphicsView;
#if 0
    m_d->m_graphView->setScene(m_d->m_dotManager.scene());
#else
    m_d->m_graphView->setScene(&m_d->m_qgvScene);
#endif
    m_d->m_graphView->setRenderHints(
        QPainter::Antialiasing | QPainter::TextAntialiasing |
        QPainter::SmoothPixmapTransform | QPainter::HighQualityAntialiasing);
    m_d->m_graphView->setDragMode(QGraphicsView::ScrollHandDrag);
    m_d->m_graphView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_d->m_graphView->setContextMenuPolicy(Qt::CustomContextMenu);
    new MouseWheelZoomer(m_d->m_graphView, m_d->m_graphView);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_d->m_infoLabel);
    layout->addWidget(m_d->m_graphView);
    //layout->setStretch(0, 1);
    layout->setStretch(1, 1);

    connect(asp, &AnalysisServiceProvider::vmeConfigAboutToBeSet,
            this, &ObjectInfoWidget::clear);

    connect(m_d->m_graphView, &QWidget::customContextMenuRequested,
            this, [this] (const QPoint &pos) { m_d->showGraphViewContextMenu(pos); });
}

ObjectInfoWidget::~ObjectInfoWidget()
{ }

void ObjectInfoWidget::setAnalysisObject(const AnalysisObjectPtr &obj)
{
    m_d->m_analysisObject = obj;
    m_d->m_configObject = nullptr;

    connect(obj.get(), &QObject::destroyed,
            this, [this] {
                m_d->m_analysisObject = nullptr;
                refresh();
            });

    refresh();
}

void ObjectInfoWidget::setVMEConfigObject(const ConfigObject *obj)
{
    m_d->m_analysisObject = {};
    m_d->m_configObject = obj;

    connect(obj, &QObject::destroyed,
            this, [this] {
                m_d->m_configObject = nullptr;
                refresh();
            });

    refresh();
}

void ObjectInfoWidget::refresh()
{
    auto refresh_analysisObject_infoLabel = [this] (const AnalysisObjectPtr &obj)
    {
        assert(obj);
        auto &label(m_d->m_infoLabel);

        /*
         * AnalysisObject: className(), objectName(), getId(),
         *                 getUserLevel(), eventId(), objectFlags()
         *
         * PipeSourceInterface: getNumberOfOutputs(), getOutput(idx)
         *
         * OperatorInterface: getMaximumInputRank(), getMaximumOutputRank(),
         *                    getNumberOfSlots(), getSlot(idx)
         *
         * SinkInterface: getStorageSize(), isEnabled()
         *
         * ConditionInterface: getNumberOfBits()
         */

        if (!obj->getAnalysis())
        {
            label->clear();
            return;
        }

        QString text;

        text += QSL("cls=%1, n=%2")
            .arg(obj->metaObject()->className())
            .arg(obj->objectName())
            ;

        text += QSL("\nusrLvl=%1, flags=%2")
            .arg(obj->getUserLevel())
            .arg(to_string(obj->getObjectFlags()))
            ;

        auto analysis = m_d->m_serviceProvider->getAnalysis();

        if (auto op = std::dynamic_pointer_cast<OperatorInterface>(obj))
        {
            text += QSL("\nrank=%1").arg(op->getRank());

            text += QSL("\n#inputs=%1, maxInRank=%2")
                .arg(op->getNumberOfSlots())
                .arg(op->getMaximumInputRank());

            text += QSL("\n#outputs=%1, maxOutRank=%2")
                .arg(op->getNumberOfOutputs())
                .arg(op->getMaximumOutputRank());

            for (auto cond: analysis->getActiveConditions(op))
            {
                text += QSL("\ncondLink=%1, condRank=%2")
                    .arg(cond->objectName())
                    .arg(cond->getRank());
            }

            auto inputSet = collect_input_set(op.get());

            if (!inputSet.empty())
            {
                text += QSL("\ninputSet: ");
                for (auto obj: inputSet)
                    text += QSL("%1, ").arg(obj->objectName());
            }
        }

        auto a2State = analysis->getA2AdapterState();
        auto cond = qobject_cast<ConditionInterface *>(obj.get());

        if (a2State && a2State->a2 && cond && a2State->conditionBitIndexes.contains(cond))
        {
            // FIXME: async copy or direct reference here. does access to the
            // bitset need to be guarded? if copying try to find a faster way than
            // testing and setting bit-by-bit. maybe alloc, clear and use an
            // overloaded OR operator
            const auto &condBits = a2State->a2->conditionBits;
            s32 bitIndex = a2State->conditionBitIndexes.value(cond);

            text += QSL("\nconditionBitValue=%1").arg(condBits.test(bitIndex));
        }

        label->setText(text);
    };

    auto refresh_vmeConfigObject = [this] (const ConfigObject *obj)
    {
        assert(obj);

        if (auto moduleConfig = qobject_cast<const ModuleConfig *>(obj))
        {
            QString text;
            text += QSL("VME module\nname=%1, type=%2\naddress=0x%3")
                .arg(moduleConfig->objectName())
                .arg(moduleConfig->getModuleMeta().typeName)
                .arg(moduleConfig->getBaseAddress(), 8, 16, QChar('0'));

            m_d->m_infoLabel->setText(text);
        }
        else
        {
            m_d->m_infoLabel->clear();
        }
    };

    if (m_d->m_analysisObject)
        //&& m_d->m_analysisObject->getAnalysis().get() == m_d->m_serviceProvider->getAnalysis())
    {
        refresh_analysisObject_infoLabel(m_d->m_analysisObject);
        m_d->refreshGraphView(m_d->m_analysisObject);
    }
    else if (m_d->m_configObject)
    {
        refresh_vmeConfigObject(m_d->m_configObject);
    }
    else
    {
        m_d->m_infoLabel->clear();
    }
}

void ObjectInfoWidget::clear()
{
    m_d->m_analysisObject = {};
    m_d->m_configObject = nullptr;
    m_d->m_infoLabel->clear();
}

} // end namespace analysis

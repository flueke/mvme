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

#include <QGraphicsView>
#include <spdlog/fmt/fmt.h>
#include <sstream>
#include <regex>
#include <memory>

namespace analysis
{

struct ObjectInfoWidget::Private
{
    //std::vector<std::unique_ptr<ObjectInfoHandler>> handlers_;

    AnalysisServiceProvider *m_serviceProvider;
    AnalysisObjectPtr m_analysisObject;
    const ConfigObject *m_configObject;

    QLabel *m_infoLabel;
    QGraphicsView *m_graphView;
    std::unique_ptr<QGraphicsScene> m_scene;

    void refreshGraphView(const AnalysisObjectPtr &obj);
};

std::string escape_dot_string(std::string label)
{
    label = std::regex_replace(label, std::regex("&"), "&amp;");
    label = std::regex_replace(label, std::regex("\""), "&quot;");
    label = std::regex_replace(label, std::regex(">"), "&gt;");
    label = std::regex_replace(label, std::regex("<"), "&lt;");
    return label;
}

std::string make_label(const AnalysisObject *obj)
{
    auto label = escape_dot_string(obj->objectName().toStdString());

    if (auto ps = dynamic_cast<const PipeSourceInterface *>(obj))
    {
        label = fmt::format("{}<br/><b>{}</b>",
            label,
            escape_dot_string(ps->getDisplayName().toStdString()));
    }

    return label;
}

std::ostream &format_object(std::ostream &out , const AnalysisObject *obj, const char *fontName = "sans")
{
    auto id = obj->getId().toString().toStdString();
    auto label = make_label(obj);

    out << fmt::format("\"{}\" [id=\"{}\" label=<{}>, fontname=\"{}\"]",
                       id, id, label, fontName)
        << std::endl;

    return out;
}

void ObjectInfoWidget::Private::refreshGraphView(const AnalysisObjectPtr &obj)
{
    const char *FontName = "sans";
    std::ostringstream dotOut;
    dotOut << "strict digraph {" << std::endl;
    dotOut << "  rankdir=TB" << std::endl;
    dotOut << "  id=OuterGraph" << std::endl;
    dotOut << fmt::format("  fontname=\"{}\"", FontName) << std::endl;

    format_object(dotOut, obj.get());

    auto ana = obj->getAnalysis();
    auto op = std::dynamic_pointer_cast<const OperatorInterface>(obj);

    if (ana && op)
    {
        auto condSet = ana->getActiveConditions(op);
    }


    dotOut << "}" << std::endl;

    auto svgData = mesytec::graphviz_util::layout_and_render_dot_q(dotOut.str());
    mesytec::graphviz_util::DomAndRenderer dr(svgData);
    auto items = mesytec::graphviz_util::create_svg_graphics_items(svgData, dr);

    m_scene->clear();
    m_graphView->setTransform({});

    for (auto &item: items)
        m_scene->addItem(item.release());

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

    m_d->m_scene = std::make_unique<QGraphicsScene>();
    m_d->m_graphView = new QGraphicsView;
    m_d->m_graphView->setScene(m_d->m_scene.get());
    m_d->m_graphView->setRenderHints(
        QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    m_d->m_graphView->setDragMode(QGraphicsView::ScrollHandDrag);
    m_d->m_graphView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
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

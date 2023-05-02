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
#ifndef __MVME_CONDITION_UI_H__
#define __MVME_CONDITION_UI_H__

#include <memory>
#include <QDialog>
#include <QToolBar>
#include <QWidget>

#include "analysis_service_provider.h"
#include "histo_ui.h"


namespace analysis
{
namespace ui
{

class ConditionDialogBase: public QDialog
{
    Q_OBJECT
    signals:
        void newConditionButtonClicked();
        void conditionSelected(const QUuid &objectId);
        void conditionNameChanged(const QUuid &objectId, const QString &name);
        void applied();

    public:
        // (object id, name)
        using ConditionInfo = std::pair<QUuid, QString>;

        ConditionDialogBase(QWidget *parent = nullptr);
        ~ConditionDialogBase() override;
};

class IntervalConditionDialog: public ConditionDialogBase
{
    Q_OBJECT
    public:
        using IntervalData = IntervalCondition::IntervalData;

    signals:
        void intervalsEdited(const QVector<IntervalData> &intervals);
        void intervalSelected(int index);

    public:
        IntervalConditionDialog(QWidget *parent = nullptr);
        ~IntervalConditionDialog() override;

        QVector<IntervalData> getIntervals() const;
        QString getConditionName() const;
        bool shouldEditAllIntervals() const;

    public slots:
        void setConditionList(const QVector<ConditionInfo> &condInfos);
        void setIntervals(const QVector<IntervalData> &intervals);
        void setInfoText(const QString &txt);
        void selectCondition(const QUuid &objectId);
        void selectInterval(int index);
        void reject() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;

};

class IntervalConditionEditorController: public QObject
{
    Q_OBJECT
    public:
        IntervalConditionEditorController(
            const Histo1DSinkPtr &sinkPtr,
            histo_ui::IPlotWidget *histoWidget,
            AnalysisServiceProvider *asp,
            QObject *parent = nullptr);

        ~IntervalConditionEditorController() override;

        bool eventFilter(QObject *watched, QEvent *event) override;

        void setEnabled(bool on);
        IntervalConditionDialog *getDialog() const;
        bool hasUnsavedChanges() const;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class PolygonConditionDialog: public ConditionDialogBase
{
    Q_OBJECT
    signals:
        void polygonEdited(const QPolygonF &poly);

    public:
        PolygonConditionDialog(QWidget *parent = nullptr);
        ~PolygonConditionDialog() override;

        QPolygonF getPolygon() const;
        QString getConditionName() const;
        void setConditionName(const QString &newName);
        QToolBar *getToolBar();

    public slots:
        void setConditionList(const QVector<ConditionInfo> &condInfos);
        void setPolygon(const QPolygonF &poly);
        void setInfoText(const QString &txt);
        void selectCondition(const QUuid &objectId);
        void selectPoint(int index);
        void reject() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class PolygonConditionEditorController: public QObject
{
    Q_OBJECT
    public:
        PolygonConditionEditorController(
            const Histo2DSinkPtr &sinkPtr,
            histo_ui::IPlotWidget *histoWidget,
            AnalysisServiceProvider *asp,
            QObject *parent = nullptr);

        ~PolygonConditionEditorController() override;

        bool eventFilter(QObject *watched, QEvent *event) override;

        void setEnabled(bool on);
        PolygonConditionDialog *getDialog() const;

    private slots:
        void onPointsSelected(const QVector<QPointF> &points);
        void onPointAppended(const QPointF &p);
        void onPointMoved(const QPointF &p);
        void onPointRemoved(const QPointF &p);

    private:
        struct Private;
        std::unique_ptr<Private> d;
        friend class ModifyPolygonCommand;
};

bool edit_condition_in_sink(AnalysisServiceProvider *asp, const ConditionPtr &cond, const SinkPtr &sink);
bool edit_condition_in_first_available_sink(AnalysisServiceProvider *asp, const ConditionPtr &cond);

} // ns ui
} // ns analysis


#endif /* __MVME_CONDITION_UI_H__ */

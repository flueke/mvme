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
#ifndef __MVME_CONDITION_UI_H__
#define __MVME_CONDITION_UI_H__

#include <memory>
#include <QWidget>
#include <QDialog>

#include "analysis_service_provider.h"
#include "histo_ui.h"


namespace analysis
{
namespace ui
{
#if 0

class ConditionWidget: public QWidget
{
    Q_OBJECT
    signals:
        void conditionLinkSelected(const ConditionLink &cl);
        void applyConditionAccept();
        void applyConditionReject();
        void editCondition(const ConditionLink &cond);
        void objectSelected(const AnalysisObjectPtr &obj);

    public:
        ConditionWidget(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        virtual ~ConditionWidget() override;

    public slots:
        void repopulate();
        void repopulate(int eventIndex);
        void repopulate(const QUuid &eventId);
        void doPeriodicUpdate();

        void selectEvent(int eventIndex);
        void selectEventById(const QUuid &eventId);
        void clearTreeSelections();
        void clearTreeHighlights();

        void highlightConditionLink(const ConditionLink &cl);
        void setModificationButtonsVisible(const ConditionLink &cl, bool visible);


    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};
#endif

class IntervalConditionDialog: public QDialog
{
    Q_OBJECT
    signals:
        void newConditionButtonClicked();
        void conditionSelected(const QUuid &objectId);
        void conditionNameChanged(const QUuid &objectId, const QString &name);
        void intervalsEdited(const QVector<QwtInterval> &intervals);
        void applied();

    public:
        IntervalConditionDialog(QWidget *parent = nullptr);
        ~IntervalConditionDialog() override;

        // (object id, name)
        using ConditionInfo = std::pair<QUuid, QString>;

        QVector<QwtInterval> getIntervals() const;
        QString getConditionName(const QUuid &objectId) const;
        QString getCurrentConditionName() const;

    public slots:
        void setConditionList(const QVector<ConditionInfo> &condInfos);
        void setIntervals(const QVector<QwtInterval> &intervals);
        void setInfoText(const QString &txt);
        void selectCondition(const QUuid &objectId);
        void selectInterval(int index);

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

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // ns ui
} // ns analysis


#endif /* __MVME_CONDITION_UI_H__ */

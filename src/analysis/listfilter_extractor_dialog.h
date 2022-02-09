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
#ifndef __LISTFILTER_EXTRACTOR_DIALOG_H__
#define __LISTFILTER_EXTRACTOR_DIALOG_H__

#include <QDialog>
#include "analysis.h"
#include "object_editor_dialog.h"
#include "analysis_service_provider.h"

class MVMEContext;

namespace analysis
{

class ListFilterExtractorDialog: public ObjectEditorDialog
{
    Q_OBJECT
    public:
        ListFilterExtractorDialog(ModuleConfig *mod, analysis::Analysis *analysis,
                                  AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        virtual ~ListFilterExtractorDialog();

        void editListFilterExtractor(const std::shared_ptr<ListFilterExtractor> &lfe);

        QVector<ListFilterExtractorPtr> getExtractors() const;

    public slots:
        virtual void accept() override;
        virtual void reject() override;

        void newFilter();

    private slots:
        void apply();
        void removeFilter();
        void cloneFilter();
        void updateWordCount();

    private:
        void repopulate();
        int addFilterToUi(const ListFilterExtractorPtr &ex);

        struct ListFilterExtractorDialogPrivate;

        std::unique_ptr<ListFilterExtractorDialogPrivate> m_d;
};

}

#endif /* __LISTFILTER_EXTRACTOR_DIALOG_H__ */

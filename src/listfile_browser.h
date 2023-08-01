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
#ifndef __MVME_LISTFILE_BROWSER_H__
#define __MVME_LISTFILE_BROWSER_H__

#include <QComboBox>
#include <QCheckBox>
#include <QFileSystemModel>
#include <QTableView>

class MVMEContext;
class MVMEMainWindow;

class ListfileBrowser: public QWidget
{
    Q_OBJECT
    public:
        ListfileBrowser(MVMEContext *context, MVMEMainWindow *mainWindow, QWidget *parent = nullptr);
        ~ListfileBrowser() override;

    private:
        void onWorkspacePathChanged();
        void onGlobalStateChanged();
        void periodicUpdate();

        void onItemDoubleClicked(const QModelIndex &mi);

        MVMEContext *m_context;
        MVMEMainWindow *m_mainWindow;
        QFileSystemModel *m_fsModel;
        QTableView *m_fsView;
        QComboBox *m_analysisLoadActionCombo;
        QCheckBox *m_cb_replayAllParts;
};

#endif /* __LISTFILE_BROWSER_H__ */

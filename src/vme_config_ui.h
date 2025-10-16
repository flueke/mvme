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
#ifndef __CONFIG_WIDGETS_H__
#define __CONFIG_WIDGETS_H__

#include "vme_config.h"
#include <QDialog>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <memory>


class EventConfig;
class ModuleConfig;
class MVMEContext;
struct EventConfigDialogPrivate;

namespace analysis
{
    class Analysis;
}

namespace Ui
{
    class DataFilterDialog;
    class DualWordDataFilterDialog;
}

class EventConfigDialog: public QDialog
{
    Q_OBJECT
    public:
        EventConfigDialog(
            VMEControllerType vmeControllerType,
            EventConfig *config,
            const VMEConfig *vmeConfig,
            QWidget *parent = 0);
        ~EventConfigDialog();

        EventConfig *getConfig() const { return m_config; }

        virtual void accept() override;

    private:
        void loadFromConfig();
        void saveToConfig();
        void setReadOnly(bool readOnly);

        EventConfigDialogPrivate *m_d;
        MVMEContext *m_context;
        VMEControllerType m_vmeControllerType;
        EventConfig *m_config;
};

class QComboBox;
class QLineEdit;

// TODO: make members private and move them into the Private struct
class ModuleConfigDialog: public QDialog
{
    Q_OBJECT
    public:
        ModuleConfigDialog(
            ModuleConfig *module_,
            const EventConfig *parentEvent,
            const VMEConfig *vmeConfig,
            QWidget *parent = 0);
        ~ModuleConfigDialog() override;

        ModuleConfig *getModule() const;

        // If set to false the module type cannot be changed. Used when editing
        // an existing module instance or importing from a .mvmemodule file.
        void setAllowTypeChange(bool allow);

        virtual void accept() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

QLineEdit *make_vme_address_edit(u32 address = 0u, QWidget *parent = nullptr);

QString info_text(const VMEConfig *config);
QString info_text(const EventConfig *config);
QString info_text(const ModuleConfig *config);

class DataFilterEditItemDelegate: public QStyledItemDelegate
{
    public:
        using QStyledItemDelegate::QStyledItemDelegate;
        QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const override;
};

// Table to display and edit vme module event header filters. Allows adding,
// removing and editing of filters and their descriptions.
class ModuleEventHeaderFiltersTable: public QTableWidget
{
    Q_OBJECT
    public:
        ModuleEventHeaderFiltersTable(QWidget *parent = nullptr);

        void setData(const std::vector<vats::VMEModuleEventHeaderFilter> &filterDefs);
        std::vector<vats::VMEModuleEventHeaderFilter> getData() const;
        void appendRow(const vats::VMEModuleEventHeaderFilter &filterDef);
};

class ModuleEventHeaderFiltersEditor: public QWidget
{
    Q_OBJECT
    public:
        ModuleEventHeaderFiltersEditor(QWidget *parent = nullptr);

        void setData(const std::vector<vats::VMEModuleEventHeaderFilter> &filterDefs);
        std::vector<vats::VMEModuleEventHeaderFilter> getData() const;

        ModuleEventHeaderFiltersTable *getTable() const { return m_table; }

    private:
        ModuleEventHeaderFiltersTable *m_table;
};

#endif /* __CONFIG_WIDGETS_H__ */

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
#ifndef __VME_SCRIPT_EDITOR_H__
#define __VME_SCRIPT_EDITOR_H__

#include "analysis/code_editor.h"
#include "util.h"
#include "vme_config.h"
#include "vme_script.h"
#include "vme_config_scripts.h"

class QCloseEvent;

struct VMEScriptEditorPrivate;

class VMEScriptEditor: public MVMEWidget
{
    Q_OBJECT
    signals:
        void logMessage(const QString &msg);
        void runScript(const vme_script::VMEScript &script, const mesytec::mvme::ScriptConfigRunner::Options &options = {});
#if 0
        void runScriptWritesBatched(const vme_script::VMEScript &script);
        void loopScript(const vme_script::VMEScript &script, bool enableLooping);
#endif
        void addApplicationWidget(QWidget *widget);

    public:
        VMEScriptEditor(VMEScriptConfig *script, QWidget *parent = 0);
        ~VMEScriptEditor();

        bool isModified() const;
        void applyChanges() { apply(); }

        virtual bool event(QEvent *event) override;

        CodeEditor *textEdit();
        QString toPlainText() const;

        VMEScriptConfig *getScriptConfig() const;

    public slots:
        void reloadFromScriptConfig() { revert(); }

    private:
        void updateWindowTitle();

        void onEditorTextChanged();
        void onScriptModified(bool isModified);

        void runScript_(const mesytec::mvme::ScriptConfigRunner::Options &options);
#if 0
        void loopScript_(bool enableLooping);
        void runScriptWritesBatched_();
#endif
        void loadFromFile();
        void loadFromTemplate();
        void saveToFile();
        void apply();
        void revert();
        void search();
        void onSearchTextEdited(const QString &text);
        void findNext(bool hasWrapped = false);
        void findPrev();

        virtual void closeEvent(QCloseEvent *event) override;

        friend struct VMEScriptEditorPrivate;
        VMEScriptEditorPrivate *m_d;
};

#endif /* __VME_SCRIPT_EDITOR_H__ */

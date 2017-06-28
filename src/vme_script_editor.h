/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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

#include "util.h"

class QCloseEvent;

class MVMEContext;
class VMEScriptConfig;
class VMEScriptEditorPrivate;

class VMEScriptEditor: public MVMEWidget
{
    Q_OBJECT
    public:
        VMEScriptEditor(MVMEContext *context, VMEScriptConfig *script, QWidget *parent = 0);
        ~VMEScriptEditor();

        bool isModified() const;
        void applyChanges() { apply(); }

        virtual bool event(QEvent *event) override;

    private:
        void updateWindowTitle();

        void onEditorTextChanged();
        void onScriptModified(bool isModified);

        void runScript();
        void loadFromFile();
        void loadFromTemplate();
        void saveToFile();
        void apply();
        void revert();

        virtual void closeEvent(QCloseEvent *event);

        friend class VMEScriptEditorPrivate;
        VMEScriptEditorPrivate *m_d;
};

#endif /* __VME_SCRIPT_EDITOR_H__ */

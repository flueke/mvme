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
#include "vme_debug_widget.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include "mvme_context.h"
#include "ui_vme_debug_widget.h"
#include "util/qt_font.h"
#include "vme_controller.h"
#include "vme_script_util.h"

static const int tabStop = 4;
static const QString scriptFileSetting = QSL("Files/LastDebugScriptDirectory");
static const int loopInterval = 1000;

using namespace std::placeholders;
using namespace vme_script;

VMEDebugWidget::VMEDebugWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::VMEDebugWidget)
    , m_context(context)
{
    ui->setupUi(this);

    new vme_script::SyntaxHighlighter(ui->scriptInput);
    set_tabstop_width(ui->scriptInput, tabStop);

    auto onControllerStateChanged = [this] (ControllerState state)
    {
        bool enable = (state == ControllerState::Connected);
        ui->gb_read->setEnabled(enable);
        ui->gb_write->setEnabled(enable);
        ui->gb_vmeScript->setEnabled(enable);
    };

    connect(m_context, &MVMEContext::controllerStateChanged, this, onControllerStateChanged);

    onControllerStateChanged(m_context->getControllerState());
}

VMEDebugWidget::~VMEDebugWidget()
{
    delete ui;
}

void VMEDebugWidget::on_writeLoop1_toggled(bool checked)
{
    ui->writeLoop2->setEnabled(!checked);
    ui->writeLoop3->setEnabled(!checked);
    ui->readLoop1->setEnabled(!checked);
    ui->readLoop2->setEnabled(!checked);
    ui->readLoop3->setEnabled(!checked);

    if (checked)
        on_writeWrite1_clicked();
}

void VMEDebugWidget::on_writeLoop2_toggled(bool checked)
{
    ui->writeLoop1->setEnabled(!checked);
    ui->writeLoop3->setEnabled(!checked);
    ui->readLoop1->setEnabled(!checked);
    ui->readLoop2->setEnabled(!checked);
    ui->readLoop3->setEnabled(!checked);

    if (checked)
        on_writeWrite2_clicked();
}

void VMEDebugWidget::on_writeLoop3_toggled(bool checked)
{
    ui->writeLoop1->setEnabled(!checked);
    ui->writeLoop2->setEnabled(!checked);
    ui->readLoop1->setEnabled(!checked);
    ui->readLoop2->setEnabled(!checked);
    ui->readLoop3->setEnabled(!checked);

    if (checked)
        on_writeWrite3_clicked();
}

void VMEDebugWidget::on_writeWrite1_clicked()
{
    bool ok;
    u32 offset = ui->writeOffset1->text().toUInt(&ok, 0);
    u32 address = ui->writeAddress1->text().toUInt(&ok, 0);
    u32 value = ui->writeValue1->text().toUInt(&ok, 0);
    address += (offset << 16);

    doWrite(address, value);

    if (ui->writeLoop1->isChecked())
        QTimer::singleShot(loopInterval, this, &VMEDebugWidget::on_writeWrite1_clicked);
}

void VMEDebugWidget::on_writeWrite2_clicked()
{
    bool ok;
    u32 offset = ui->writeOffset2->text().toUInt(&ok, 0);
    u32 address = ui->writeAddress2->text().toUInt(&ok, 0);
    u32 value = ui->writeValue2->text().toUInt(&ok, 0);
    address += (offset << 16);

    doWrite(address, value);

    if (ui->writeLoop2->isChecked())
        QTimer::singleShot(loopInterval, this, &VMEDebugWidget::on_writeWrite2_clicked);
}

void VMEDebugWidget::on_writeWrite3_clicked()
{
    bool ok;
    u32 offset = ui->writeOffset3->text().toUInt(&ok, 0);
    u32 address = ui->writeAddress3->text().toUInt(&ok, 0);
    u32 value = ui->writeValue3->text().toUInt(&ok, 0);
    address += (offset << 16);

    doWrite(address, value);

    if (ui->writeLoop3->isChecked())
        QTimer::singleShot(loopInterval, this, &VMEDebugWidget::on_writeWrite3_clicked);
}

void VMEDebugWidget::on_readLoop1_toggled(bool checked)
{
    ui->writeLoop1->setEnabled(!checked);
    ui->writeLoop2->setEnabled(!checked);
    ui->writeLoop3->setEnabled(!checked);
    ui->readLoop2->setEnabled(!checked);
    ui->readLoop3->setEnabled(!checked);

    if (checked)
        on_readRead1_clicked();
}

void VMEDebugWidget::on_readLoop2_toggled(bool checked)
{
    ui->writeLoop1->setEnabled(!checked);
    ui->writeLoop2->setEnabled(!checked);
    ui->writeLoop3->setEnabled(!checked);
    ui->readLoop1->setEnabled(!checked);
    ui->readLoop3->setEnabled(!checked);

    if (checked)
        on_readRead2_clicked();
}

void VMEDebugWidget::on_readLoop3_toggled(bool checked)
{
    ui->writeLoop1->setEnabled(!checked);
    ui->writeLoop2->setEnabled(!checked);
    ui->writeLoop3->setEnabled(!checked);
    ui->readLoop1->setEnabled(!checked);
    ui->readLoop2->setEnabled(!checked);

    if (checked)
        on_readRead3_clicked();
}

void VMEDebugWidget::on_readRead1_clicked()
{
    bool ok;
    u32 offset = ui->readOffset1->text().toUInt(&ok, 0);
    u32 address = ui->readAddress1->text().toUInt(&ok, 0);
    address += (offset << 16);

    ui->bltResult->clear();
    ui->readResult1_hex->clear();
    ui->readResult1_dec->clear();

    if (ui->readModeSingle->isChecked())
    {
        u16 value = doRead(address);

        ui->readResult1_hex->setText(QString("0x%1")
                                 .arg(value, 4, 16, QChar('0'))
                                );
        ui->readResult1_dec->setText(QString("%1")
                                 .arg(value, 0, 10, QChar(' '))
                                );
    }
    else
    {
        Command cmd;
        cmd.type = ui->readModeBLT->isChecked() ? CommandType::BLT : CommandType::MBLT;
        cmd.addressMode = (cmd.type == CommandType::BLT) ? vme_address_modes::BLT32 : vme_address_modes::MBLT64;
        cmd.address = address;
        cmd.transfers = static_cast<u32>(ui->blockReadCount->value());

        VMEScript script = { cmd };

        auto results = m_context->runScript(script);

        if (!results.isEmpty())
        {
            auto result = results[0];

            if (result.error.isError())
            {
                m_context->logMessage(QString("VME Debug: block read (amod=0x%1) 0x%2, vmeError=%3")
                                      .arg(static_cast<int>(cmd.addressMode), 2, 16, QLatin1Char('0'))
                                      .arg(address, 8, 16, QChar('0'))
                                      .arg(result.error.toString())
                                     );
            }
            else
            {
                m_context->logMessage(QString("VME Debug: block read (amod=0x%1) 0x%2, read %3 32-bit words")
                                      .arg(static_cast<int>(cmd.addressMode), 2, 16, QLatin1Char('0'))
                                      .arg(address, 8, 16, QChar('0'))
                                      .arg(result.valueVector.size()));
            }

            QString buffer;
            for (int i=0; i<result.valueVector.size(); ++i)
            {
                buffer += QString(QSL("%1: 0x%2\n"))
                    .arg(i, 2, 10, QChar(' '))
                    .arg(result.valueVector[i], 8, 16, QChar('0'));
            }
            ui->bltResult->setText(buffer);
        }
    }

    if (ui->readLoop1->isChecked())
        QTimer::singleShot(loopInterval, this, &VMEDebugWidget::on_readRead1_clicked);
}

void VMEDebugWidget::on_readRead2_clicked()
{
    bool ok;
    u32 offset = ui->readOffset2->text().toUInt(&ok, 0);
    u32 address = ui->readAddress2->text().toUInt(&ok, 0);
    address += (offset << 16);

    u16 value = doRead(address);

    ui->readResult2_hex->setText(QString("0x%1")
                             .arg(value, 4, 16, QChar('0'))
                            );
    ui->readResult2_dec->setText(QString("%1")
                             .arg(value, 0, 10, QChar(' '))
                            );

    if (ui->readLoop2->isChecked())
        QTimer::singleShot(loopInterval, this, &VMEDebugWidget::on_readRead2_clicked);
}

void VMEDebugWidget::on_readRead3_clicked()
{
    bool ok;
    u32 offset = ui->readOffset3->text().toUInt(&ok, 0);
    u32 address = ui->readAddress3->text().toUInt(&ok, 0);
    address += (offset << 16);

    u16 value = doRead(address);

    ui->readResult3_hex->setText(QString("0x%1")
                             .arg(value, 4, 16, QChar('0'))
                            );
    ui->readResult3_dec->setText(QString("%1")
                             .arg(value, 0, 10, QChar(' '))
                            );

    if (ui->readLoop3->isChecked())
        QTimer::singleShot(loopInterval, this, &VMEDebugWidget::on_readRead3_clicked);
}

void VMEDebugWidget::doWrite(u32 address, u32 value)
{
    Command cmd;
    cmd.type = CommandType::Write;
    cmd.addressMode = vme_address_modes::A32;
    cmd.dataWidth = DataWidth::D16;
    cmd.address = address;
    cmd.value = value;

    VMEScript script = { cmd };

    auto logger = [this](const QString &str) { m_context->logMessage(QSL("  ") + str); };
    auto results = m_context->runScript(script, logger);

    if (!results.isEmpty())
    {
        auto result = results[0];

        if (result.error.isError())
        {
            m_context->logMessage(QString("VME Debug: write 0x%1 -> 0x%2, vmeError=%3")
                                  .arg(address, 8, 16, QChar('0'))
                                  .arg(value, 4, 16, QChar('0'))
                                  .arg(result.error.toString())
                                 );
        }
        else
        {
            m_context->logMessage(QString("VME Debug: write 0x%1 -> 0x%2, write ok")
                                  .arg(address, 8, 16, QChar('0'))
                                  .arg(value, 4, 16, QChar('0'))
                                 );
        }
    }
}

u16 VMEDebugWidget::doRead(u32 address)
{
    Command cmd;
    cmd.type = CommandType::Read;
    cmd.addressMode = vme_address_modes::A32;
    cmd.dataWidth = DataWidth::D16;
    cmd.address = address;

    VMEScript script = { cmd };

    auto logger = [this](const QString &str) { m_context->logMessage(QSL("  ") + str); };
    auto results = m_context->runScript(script, logger);

    if (!results.isEmpty())
    {
        auto result = results[0];

        if (result.error.isError())
        {
            m_context->logMessage(QString("VME Debug: read 0x%1, vmeError=%2")
                                  .arg(address, 8, 16, QChar('0'))
                                  .arg(result.error.toString())
                                 );
        }
        else
        {
            m_context->logMessage(QString("VME Debug: read 0x%1 -> 0x%2")
                                  .arg(address, 8, 16, QChar('0'))
                                  .arg(result.value, 4, 16, QChar('0'))
                                 );
        }
        return result.value;
    }

    return 0;
}

void VMEDebugWidget::on_runScript_clicked()
{
    ui->scriptOutput->clear();
    //auto logger = std::bind(&QTextEdit::append, ui->scriptOutput, _1);
    auto logger = std::bind(&MVMEContext::logMessage, m_context, _1);
    try
    {
        bool ok;
        auto baseAddress = ui->scriptOffset->text().toUInt(&ok, 0);
        baseAddress <<= 16;

        auto script = vme_script::parse(ui->scriptInput->toPlainText(), baseAddress);
        auto resultList = m_context->runScript(script, logger);

        for (auto result: resultList)
        {
            QString str = format_result(result);
            if (!str.isEmpty())
                ui->scriptOutput->append(str);
        }
    }
    catch (const vme_script::ParseError &e)
    {
        logger(QSL("Parse error: ") + e.toString());
    }
}

void VMEDebugWidget::on_saveScript_clicked()
{
    QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    QSettings settings;
    if (settings.contains(scriptFileSetting))
    {
        path = settings.value(scriptFileSetting).toString();
    }

    QFileDialog fd(this, QSL("Save vme script"), path,
                   QSL("VME scripts (*.vmescript *.vme);; All Files (*)"));

    fd.setDefaultSuffix(".vmescript");
    fd.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);

    if (fd.exec() != QDialog::Accepted || fd.selectedFiles().isEmpty())
        return;

    auto fileName = fd.selectedFiles().front();

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, "File error", QString("Error opening \"%1\" for writing").arg(fileName));
        return;
    }

    QTextStream stream(&file);
    stream << ui->scriptInput->toPlainText();

    if (stream.status() != QTextStream::Ok)
    {
        QMessageBox::critical(this, "File error", QString("Error writing to \"%1\"").arg(fileName));
        return;
    }

    settings.setValue(scriptFileSetting, QFileInfo(fileName).absolutePath());
}

void VMEDebugWidget::on_loadScript_clicked()
{
    QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    QSettings settings;
    if (settings.contains(scriptFileSetting))
    {
        path = settings.value(scriptFileSetting).toString();
    }

    QString fileName = QFileDialog::getOpenFileName(this, QSL("Load vme script file"), path,
                                                    QSL("VME scripts (*.vmescript *.vme);; All Files (*)"));
    if (!fileName.isEmpty())
    {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly))
        {
            QTextStream stream(&file);
            ui->scriptInput->setPlainText(stream.readAll());
            QFileInfo fi(fileName);
            settings.setValue(scriptFileSetting, fi.absolutePath());
            ui->scriptOutput->clear();
        }
    }
}

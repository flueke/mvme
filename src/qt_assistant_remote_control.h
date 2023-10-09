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
#ifndef __MVME_QT_ASSISTANT_REMOTE_CONTROL_H__
#define __MVME_QT_ASSISTANT_REMOTE_CONTROL_H__

#include <chrono>
#include <memory>
#include <QObject>
#include <thread>

namespace mesytec
{
namespace mvme
{

class QtAssistantRemoteControl: public QObject
{
    public:
        static QtAssistantRemoteControl &instance();
        ~QtAssistantRemoteControl() override;

        bool startAssistant();
        bool isRunning() const;
        bool sendCommand(const QString &cmd);

        bool activateKeyword(const QString &keyword)
        {
            // TODO: remove the sleeps. They don't fix the Qt Assistant issues
            auto sleep = [] { std::this_thread::sleep_for(std::chrono::milliseconds(500)); };

            sendCommand(QStringLiteral("activateKeyword ") + keyword);
            sleep();
            showContents();
            sleep();
            syncContents();

            return true;
        }

        bool activateIdentifier(const QString &id)
        {
            return sendCommand(QStringLiteral("activateIdentifier ") + id);
        }

        bool showContents()
        {
            return sendCommand(QStringLiteral("show contents"));
        }

        bool syncContents()
        {
            return sendCommand(QStringLiteral("syncContents"));
        }

    private:
        QtAssistantRemoteControl();

        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace mvme
} // end namespace mesytec

#endif /* __MVME_QT_ASSISTANT_REMOTE_CONTROL_H__ */

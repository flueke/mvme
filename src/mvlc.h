/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (c) 2020 mesytec GmbH & Co. KG
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __MESYTEC_MVLC_MVLC_H__
#define __MESYTEC_MVLC_MVLC_H__

#include <memory>
#include <netinet/ip.h>
#include <system_error>
#include <typeinfo>

#include "mvlc_constants.h"

namespace mesytec::mvlc
{

class MVLC
{
    public:
        template<typename T>
            explicit MVLC(T &&impl)
            : m_impl(std::make_shared<Model<T>>(std::forward<T>(impl)))
            {
            }

    private:
        struct Concept
        {
            virtual ~Concept() {};

            virtual std::error_code connect() = 0;
            virtual std::error_code disconnect() = 0;
            virtual bool isConnected() const = 0;
            virtual std::string connectionInfo() const = 0;

            virtual std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                                          size_t &bytesTransferred) = 0;
            virtual std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                                         size_t &bytesTransferred) = 0;

            virtual const std::type_info &typeInfo() = 0;
        };

        template<typename T>
            struct Model: public Concept
        {
            explicit Model(T &&data)
                : d(data)
            {}

            std::error_code connect() override
            {
                return d.connect();
            }

            std::error_code disconnect() override
            {
                return d.disconnect();
            }

            bool isConnected() const override
            {
                return d.isConnected();
            }

            std::string connectionInfo() const override
            {
                return d.connectionInfo();
            }


            std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                                  size_t &bytesTransferred) override
            {
                return d.write(pipe, buffer, size, bytesTransferred);
            }

            std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                                 size_t &bytesTransferred) override
            {
                return d.read(pipe, buffer, size, bytesTransferred);
            }

            const std::type_info &typeInfo() override
            {
                return typeid(T);
            }

            T d;
        };

        std::shared_ptr<Concept> m_impl;
};

}

#endif /* __MESYTEC_MVLC_MVLC_H__ */

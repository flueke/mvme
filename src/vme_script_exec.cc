#include "vme_script_exec.h"

#include <QDebug>
#include <QThread>

#include "mvlc/mvlc_vme_controller.h"
#include "mvlc/mvlc_util.h"
#include "vmusb.h"

namespace vme_script
{

ResultList run_script(
    VMEController *controller, const VMEScript &script,
    LoggerFun logger, const run_script_options::Flag &options)
{
    return run_script(controller, script, logger, logger, options);
}

ResultList run_script(
    VMEController *controller, const VMEScript &script,
    LoggerFun logger, LoggerFun error_logger,
    const run_script_options::Flag &options)
{
    int cmdNumber = 1;
    RunState state;
    ResultList results;

    for (const auto &cmd: script)
    {
        if (cmd.type != CommandType::Invalid)
        {
            if (!cmd.warning.isEmpty())
            {
                logger(QString("Warning: %1 on line %2 (cmd=%3)")
                       .arg(cmd.warning)
                       .arg(cmd.lineNumber)
                       .arg(to_string(cmd.type))
                      );
            }

            auto tStart = QDateTime::currentDateTime();

            //qDebug() << __FUNCTION__
            //    << tStart << "begin run_command" << cmdNumber << "of" << script.size();

            auto result = run_command(controller, cmd, state, logger);

            auto tEnd = QDateTime::currentDateTime();
            results.push_back(result);

            //qDebug() << __FUNCTION__
            //    << tEnd
            //    << "  " << cmdNumber << "of" << script.size() << ":"
            //    << format_result(result)
            //    << "duration:" << tStart.msecsTo(tEnd) << "ms";

            if (options & run_script_options::LogEachResult)
            {
                if (result.error.isError())
                    error_logger(format_result(result));
                else
                    logger(format_result(result));
            }

            if ((options & run_script_options::AbortOnError)
                && result.error.isError())
            {
                break;
            }
        }

        ++cmdNumber;
    }

    return results;
}

Result run_command(VMEController *controller, const Command &cmd, RunState &state, LoggerFun logger)
{
    /*
    if (logger)
        logger(to_string(cmd));
    */

    Result result;

    result.command = cmd;

    switch (cmd.type)
    {
        case CommandType::Invalid:
        case CommandType::SetBase:
        case CommandType::ResetBase:
        case CommandType::SetVariable:
            /* Note: The commands in this block have already been handled at parse time. */
            break;

        case CommandType::Read:
        case CommandType::ReadAbs:
            {
                switch (cmd.dataWidth)
                {
                    case DataWidth::D16:
                        {
                            uint16_t value = 0;
                            result.error = controller->read16(cmd.address, &value, cmd.addressMode);
                            result.value = value;
                            state.accu = value;
                        } break;
                    case DataWidth::D32:
                        {
                            uint32_t value = 0;
                            result.error = controller->read32(cmd.address, &value, cmd.addressMode);
                            result.value = value;
                            state.accu = value;
                        } break;
                }
            } break;

        case CommandType::Write:
        case CommandType::WriteAbs:
            {
                switch (cmd.dataWidth)
                {
                    case DataWidth::D16:
                        result.error = controller->write16(cmd.address, cmd.value, cmd.addressMode);
                        break;
                    case DataWidth::D32:
                        result.error = controller->write32(cmd.address, cmd.value, cmd.addressMode);
                        break;
                }
            } break;

        case CommandType::Wait:
            {
                QThread::msleep(cmd.delay_ms);
            } break;

        case CommandType::Marker:
            {
                result.value = cmd.value;
            } break;

        case CommandType::BLT:
        case CommandType::MBLT:
            {
                result.error = controller->blockRead(
                    cmd.address, cmd.transfers, &result.valueVector,
                    cmd.addressMode, false);
            } break;

        case CommandType::BLTFifo:
        case CommandType::MBLTFifo:
            {
                result.error = controller->blockRead(
                    cmd.address, cmd.transfers, &result.valueVector,
                    cmd.addressMode, true);
            } break;

        case CommandType::MBLTSwapped:
            if (auto mvlc = qobject_cast<mesytec::mvme_mvlc::MVLC_VMEController *>(controller))
            {
                result.error = mvlc->blockReadSwapped(
                    cmd.address, cmd.transfers, &result.valueVector);
            }
            else
            {
                result.error = VMEError(VMEError::WrongControllerType,
                                        QSL("MVLC controller required"));
            } break;

        case CommandType::Blk2eSST64:
            if (auto mvlc = qobject_cast<mesytec::mvme_mvlc::MVLC_VMEController *>(controller))
            {
                result.error = mvlc->blockRead(
                    cmd.address, static_cast<mesytec::mvlc::Blk2eSSTRate>(cmd.blk2eSSTRate),
                    cmd.transfers, &result.valueVector);
            } break;

        case CommandType::Blk2eSST64Swapped:
            if (auto mvlc = qobject_cast<mesytec::mvme_mvlc::MVLC_VMEController *>(controller))
            {
                result.error = mvlc->blockReadSwapped(
                    cmd.address, static_cast<mesytec::mvlc::Blk2eSSTRate>(cmd.blk2eSSTRate),
                    cmd.transfers, &result.valueVector);
            } break;

        case CommandType::VMUSB_WriteRegister:
            if (auto vmusb = qobject_cast<VMUSB *>(controller))
            {
                result.error = vmusb->writeRegister(cmd.address, cmd.value);
            }
            else
            {
                result.error = VMEError(VMEError::WrongControllerType,
                                        QSL("VMUSB controller required"));
            } break;

        case CommandType::VMUSB_ReadRegister:
            if (auto vmusb = qobject_cast<VMUSB *>(controller))
            {
                result.value = 0u;
                result.error = vmusb->readRegister(cmd.address, &result.value);
            }
            else
            {
                result.error = VMEError(VMEError::WrongControllerType,
                                        QSL("VMUSB controller required"));
            } break;

        case CommandType::MVLC_Custom:
            /* Custom blocks can be directly executed: an MVLC stack is built,
             * uploaded and executed vis MVLC::stackTransaction(). */
            if (auto mvlc = qobject_cast<mesytec::mvme_mvlc::MVLC_VMEController *>(controller))
            {
                // Build the custom stack (it needs to start with a marker
                // command for the request/response logic to be able to
                // correctly identify the resulting data).
                vme_script::Command marker;
                marker.type = vme_script::CommandType::Marker;
                marker.value = 0xabcdef00u;

                VMEScript stackScript =
                {
                    marker,
                    cmd,
                };

                auto stack = mesytec::mvme_mvlc::build_mvlc_stack(stackScript);
                // execute it
                std::vector<u32> destBuffer;
                auto ec = mvlc->getMVLC().stackTransaction(stack, destBuffer);
                result.error = VMEError(ec);
                std::copy(destBuffer.begin(), destBuffer.end(), std::back_inserter(result.valueVector));
            }
            else
            {
                result.error = VMEError(VMEError::WrongControllerType,
                                        QSL("MVLC controller required"));
            } break;

        case CommandType::MVLC_WriteSpecial:
        case CommandType::MVLC_SetAddressIncMode:
        case CommandType::MVLC_Wait:
        case CommandType::MVLC_SignalAccu:
        case CommandType::MVLC_MaskShiftAccu:
        case CommandType::MVLC_SetAccu:
        case CommandType::MVLC_ReadToAccu:
        case CommandType::MVLC_CompareLoopAccu:
            {
                auto msg = QSL("%1 is not supported by vme_script::run_command().")
                    .arg(to_string(cmd.type));
                result.error = VMEError(VMEError::UnsupportedCommand, msg);
                if (logger) logger(msg);
            }
            break;


        case CommandType::MetaBlock:
        case CommandType::Print:
            // These just don't do anything.
            break;

        case CommandType::MVLC_InlineStack:
            if (auto mvlc = qobject_cast<mesytec::mvme_mvlc::MVLC_VMEController *>(controller))
            {
                // Build the inline stack (it needs to start with a marker
                // command for the logic to be able to correctly identify the
                // resulting data).
                vme_script::Command marker;
                marker.type = vme_script::CommandType::Marker;
                marker.value = 0xabcdef01u;
                VMEScript stackScript = { marker };

                for (const auto &cmd: cmd.mvlcInlineStack)
                {
                    stackScript.push_back(*cmd);
                }

                auto stack = mesytec::mvme_mvlc::build_mvlc_stack(stackScript);
                std::vector<u32> destBuffer;
                auto ec = mvlc->getMVLC().stackTransaction(stack, destBuffer);
                result.error = VMEError(ec);
                std::copy(destBuffer.begin(), destBuffer.end(), std::back_inserter(result.valueVector));
            }
            else
            {
                result.error = VMEError(VMEError::WrongControllerType,
                                        QSL("MVLC controller required"));
            } break;

        case CommandType::Accu_Set:
            {
                state.accu = cmd.value;
            } break;
        case CommandType::Accu_MaskAndRotate:
            {
                state.accu &= cmd.accuMask;
                state.accu = mesytec::mvlc::util::rotl32(state.accu, cmd.accuRotate);
            } break;
        case CommandType::Accu_Test:
            {
                bool testResult = {};
                QString opStr = {};
                switch (cmd.accuTestOp)
                {
                    case AccuTestOp::EQ:
                        testResult = state.accu == cmd.accuTestValue;
                        opStr = "==";
                        break;

                    case AccuTestOp::NEQ:
                        testResult = state.accu != cmd.accuTestValue;
                        opStr = "!=";
                        break;

                    case AccuTestOp::LT:
                        testResult = state.accu < cmd.accuTestValue;
                        opStr = "<";
                        break;

                    case AccuTestOp::LTE:
                        testResult = state.accu <= cmd.accuTestValue;
                        opStr = "<=";
                        break;

                    case AccuTestOp::GT:
                        testResult = state.accu > cmd.accuTestValue;
                        opStr = ">";
                        break;

                    case AccuTestOp::GTE:
                        testResult = state.accu >= cmd.accuTestValue;
                        opStr = ">=";
                        break;
                }

                if (!testResult)
                {
                    result.error = VMEError(QSL("%4 (accu test '0x%1 %2 0x%3' is false)")
                        .arg(state.accu, 8, 16, QLatin1Char('0'))
                        .arg(opStr)
                        .arg(cmd.value, 8, 16, QLatin1Char('0'))
                        .arg(cmd.accuTestMessage)
                        );
                }

            } break;
    }

    return result;
}

QString format_result(const Result &result)
{
    if (result.error.isError())
    {
        QString ret = QString("Error from \"%1\": %2")
            .arg(to_string(result.command))
            .arg(result.error.toString());

#if 0 // too verbose
        if (auto ec = result.error.getStdErrorCode())
        {
            ret += QString(" (std::error_code: msg=%1, value=%2, cat=%3)")
                .arg(ec.message().c_str())
                .arg(ec.value())
                .arg(ec.category().name());
        }
#endif

        return ret;
    }

    QString ret(to_string(result.command));

    switch (result.command.type)
    {
        case CommandType::Invalid:
        case CommandType::Wait:
        case CommandType::Marker:
        case CommandType::SetBase:
        case CommandType::ResetBase:
        case CommandType::MetaBlock:
        case CommandType::SetVariable:
        case CommandType::MVLC_WriteSpecial:
        case CommandType::MVLC_SetAddressIncMode:
        case CommandType::MVLC_Wait:
        case CommandType::MVLC_SignalAccu:
        case CommandType::MVLC_MaskShiftAccu:
        case CommandType::MVLC_SetAccu:
        case CommandType::MVLC_ReadToAccu:
        case CommandType::MVLC_CompareLoopAccu:
            break;

        case CommandType::Write:
        case CommandType::WriteAbs:
        case CommandType::VMUSB_WriteRegister:
            // Append the decimal form of the written value and a message that
            // the write was ok.
            ret += QSL(" (%1 dec), write ok").arg(result.command.value);
            break;

        case CommandType::Read:
        case CommandType::ReadAbs:
            ret += QString(" -> 0x%1 (%2 dec)")
                .arg(result.value, 8, 16, QChar('0'))
                .arg(result.value)
                ;
            break;

        case CommandType::BLT:
        case CommandType::BLTFifo:
        case CommandType::MBLT:
        case CommandType::MBLTFifo:
        case CommandType::MBLTSwapped:
        case CommandType::Blk2eSST64:
        case CommandType::Blk2eSST64Swapped:
        case CommandType::MVLC_Custom:
        case CommandType::MVLC_InlineStack:
            {
                // Special handling for the mvlc where a marker command used as
                // a reference word is put into the data stream when running
                // single commands.
                bool secondWordIsReference = false;

                if (result.valueVector.size() >= 2
                    && mesytec::mvlc::get_frame_type(result.valueVector[0]) == mesytec::mvlc::frame_headers::StackFrame)
                    secondWordIsReference = true;

                ret += "\n";
                for (int i=0; i<result.valueVector.size(); ++i)
                {
                    if (i == 1 && secondWordIsReference)
                        ret += QString(QSL("%1: 0x%2 (reference marker)\n"))
                            .arg(i, 2, 10, QChar(' '))
                            .arg(result.valueVector[i], 8, 16, QChar('0'));
                    else
                        ret += QString(QSL("%1: 0x%2\n"))
                            .arg(i, 2, 10, QChar(' '))
                            .arg(result.valueVector[i], 8, 16, QChar('0'));
                }
            } break;

        case CommandType::VMUSB_ReadRegister:
            ret += QSL(" -> 0x%1, %2")
                .arg(result.value, 8, 16, QChar('0'))
                .arg(result.value)
                ;
            break;

        case CommandType::Print:
            {
                ret = result.command.printArgs.join(' ');
            }
            break;
    }

    return ret;
}

} // end namespace vme_script

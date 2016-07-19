#include "vmemodule.h"
#include <QTextStream>

QString VMECommand::toString() const
{
    ulong _addr      = static_cast<ulong>(address);
    ulong _val       = static_cast<ulong>(value);
    ulong _amod      = static_cast<ulong>(amod);
    ulong _transfers = static_cast<ulong>(transfers);

    static const QChar Zero = QLatin1Char('0');

    switch (type)
    {
        case NotSet:
            {
                return "NotSet";
            } break;
        case Write32:
            {
                return QString("Write32 0x%1 -> 0x%2 (amod=0x%3)")
                    .arg(address, 8, 16, Zero)
                    .arg(value, 8, 16, Zero)
                    .arg(amod, 2, 16, Zero);
            } break;
        case Write16:
            {
                return QString("Write16 0x%1 -> 0x%2 (amod=0x%3)")
                    .arg(address, 8, 16, Zero)
                    .arg(value, 4, 16, Zero)
                    .arg(amod, 2, 16, Zero);
            } break;
        case Read32:
            {
                return QString("Read32 0x%1 (amod=0x%2)")
                    .arg(address, 8, 16, Zero)
                    .arg(amod, 2, 16, Zero);
            } break;
        case Read16:
            {
                return QString("Read16 0x%1 (amod=0x%2)")
                    .arg(address, 8, 16, Zero)
                    .arg(amod, 2, 16, Zero);
            } break;
        case BlockRead32:
            {
                return QString("BlockRead32 base=0x%1, transfers=%2, amod=0x%3")
                    .arg(address, 8, 16, Zero)
                    .arg(transfers)
                    .arg(amod, 2, 16, Zero);
            } break;
        case FifoRead32:
            {
                return QString("FifoRead32 address=0x%1, transfers=%2, amod=0x%3")
                    .arg(address, 8, 16, Zero)
                    .arg(transfers)
                    .arg(amod, 2, 16, Zero);
            } break;
        case BlockCountRead16:
            {
                return QString("BlockCountRead16 address=0x%1, mask=0x%2, amod=0x%3")
                    .arg(address, 8, 16, Zero)
                    .arg(blockCountMask, 4, 16, Zero)
                    .arg(amod, 2, 16, Zero);
            } break;
        case BlockCountRead32:
            {
                return QString("BlockCountRead32 address=0x%1, mask=0x%2, amod=0x%3")
                    .arg(address, 8, 16, Zero)
                    .arg(blockCountMask, 8, 16, Zero)
                    .arg(amod, 2, 16, Zero);
            } break;
        case MaskedCountBlockRead32:
            {
                return QString("MaskedCountBlockRead32 base=0x%1, amod=0x%2")
                    .arg(address, 8, 16, Zero)
                    .arg(amod, 2, 16, Zero);
            } break;
        case MaskedCountFifoRead32:
            {
                return QString("MaskedCountFifoRead32 base=0x%1, amod=0x%2")
                    .arg(address, 8, 16, Zero)
                    .arg(amod, 2, 16, Zero);
            } break;
        case Delay:
            {
                return QString("Delay %1*200ns")
                    .arg(delay200nsClocks);
            } break;
        case Marker:
            {
                return QString("Marker 0x%1")
                    .arg(value, 4, 16, Zero);
            } break;
    }

    Q_ASSERT(false);
    return QString();
}

QTextStream &VMECommandList::dump(QTextStream &out)
{
    for (auto command: commands)
    {
        out << command.toString() << endl;
    }

    return out;
}

#include "sis3153.h"
#include "sis3153/sis3153eth.h"
#include <QTextStream>

static const char *DefaultSISAddress = "sis3153-0040";

int main(int argc, char *argv[])
{
    QTextStream qout(stdout);
    QString sisAddress(DefaultSISAddress);

    if (argc > 1)
    {
        sisAddress = QString(argv[1]);
    }

    qout << "Sending stop command to " << sisAddress << endl;

    try
    {

        SIS3153 sis;
        sis.setAddress(sisAddress);

        VMEError error;

        error = sis.open();
        if (error.isError()) throw error;

        error = sis.writeRegister(SIS3153ETH_STACK_LIST_CONTROL, 1 << 16);
        if (error.isError()) throw error;

        dump_registers(&sis, [&qout](const QString &str) {
            qout << str << endl;
        });
    }
    catch (const VMEError &e)
    {
        qout << "Error: " << e.toString() << endl;
        return 1;
    }

    return 0;
}

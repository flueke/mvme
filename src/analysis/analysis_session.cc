#include "analysis_session.h"
#include <H5Cpp.h>
#include <QDir>
#include <stdio.h>
#include "analysis.h"

using namespace analysis;
using namespace H5;

namespace
{

template<size_t Size>
struct Slab
{
    std::array<hsize_t, Size> start;
    std::array<hsize_t, Size> stride;
    std::array<hsize_t, Size> count;
    std::array<hsize_t, Size> block;
};

static const u32 compressionLevel = 1; // 0 to 9 from no compression to best

DataType make_datatype_native_double()
{
    DataType datatype_native_double(PredType::NATIVE_DOUBLE);
    herr_t err = H5Tset_order(datatype_native_double.getId(), H5T_ORDER_LE);
    assert(err >= 0);
    return datatype_native_double;
}

void save_Histo1DSink(H5File &outfile, Histo1DSink *histoSink)
{
    auto histoName = (QString("H1D.R%1.%2")
                      .arg(histoSink->getMaximumInputRank())
                      .arg(histoSink->objectName())
                     );
    histoName.replace('/', '_');

    qDebug() << "histoName =" << histoName << histoSink;

    const hsize_t dimensions[] =
    {
        (hsize_t)histoSink->m_histos.size(),
        (hsize_t)histoSink->m_bins
    };

    DataSpace memspace(2, dimensions, nullptr);

    Slab<2> memSlab;
    memSlab.start   = { 0, 0 };
    memSlab.count   = { 1, (hsize_t)histoSink->m_bins };

    memspace.selectHyperslab(
        H5S_SELECT_SET,
        memSlab.count.data(),
        memSlab.start.data());

    DataSpace filespace(2, dimensions, NULL);

    DSetCreatPropList dataset_creation_plist;

    // one histo
    const hsize_t chunk_dimensions[] = { 1, (hsize_t)histoSink->m_bins };

    dataset_creation_plist.setChunk(2, chunk_dimensions);
    dataset_creation_plist.setDeflate(compressionLevel);

    DataSet dataset = outfile.createDataSet(
        histoName.toLocal8Bit().constData(),
        make_datatype_native_double(),
        filespace,
        dataset_creation_plist
        );

    for (s32 histoIndex = 0; histoIndex < histoSink->m_histos.size(); histoIndex++)
    {
        const auto histo = histoSink->m_histos[histoIndex];

        Slab<2> fileSlab;
        fileSlab.start  = { (hsize_t)histoIndex, 0 };
        fileSlab.count  = { 1, (hsize_t)histoSink->m_bins };

        filespace.selectHyperslab(
            H5S_SELECT_SET,
            fileSlab.count.data(),
            fileSlab.start.data());

        dataset.write(histo->data(),
                      make_datatype_native_double(),
                      memspace,
                      filespace);
    }
}

void save_Histo2DSink(H5File &outfile, Histo2DSink *histoSink)
{
    auto histoName = (QString("H2D.R%1.%2")
                      .arg(histoSink->getMaximumInputRank())
                      .arg(histoSink->objectName())
                     );
    histoName.replace('/', '_');

    qDebug() << "histoName =" << histoName << histoSink;

    auto histo = histoSink->m_histo;
    auto binnings = histo->getAxisBinnings();

    const hsize_t dimensions[] =
    {
        binnings[Qt::XAxis].getBins(),
        binnings[Qt::YAxis].getBins()
    };

    DataSpace dataspace(2, dimensions, nullptr);

    DSetCreatPropList dataset_creation_plist;

    // one row
    const hsize_t chunk_dimensions[] = { binnings[Qt::XAxis].getBins(), 1 };

    dataset_creation_plist.setChunk(2, chunk_dimensions);
    dataset_creation_plist.setDeflate(compressionLevel);

    DataSet dataset = outfile.createDataSet(
        histoName.toLocal8Bit().constData(),
        make_datatype_native_double(),
        dataspace,
        dataset_creation_plist
        );

    dataset.write(histo->data(),
                  make_datatype_native_double(),
                  dataspace,
                  dataspace);
}

void save_analysis_session_(const QString &filename, analysis::Analysis *analysis)
{
    qDebug() << __PRETTY_FUNCTION__ << filename;

    herr_t err;

    H5File outfile(filename.toLocal8Bit().constData(), H5F_ACC_TRUNC);

    auto operators = analysis->getOperators();

    for (auto oe: operators)
    {
        if (auto histoSink = qobject_cast<Histo1DSink *>(oe.op.get()))
        {
            save_Histo1DSink(outfile, histoSink);
        }
        else if (auto histoSink = qobject_cast<Histo2DSink *>(oe.op.get()))
        {
            save_Histo2DSink(outfile, histoSink);
        }
    }
}

} // anon namespace

/* Error stack traversal callback function pointers */
/*
typedef herr_t (*H5E_walk2_t)(unsigned n, const H5E_error2_t *err_desc,
    void *client_data);

typedef herr_t (*H5E_auto2_t)(hid_t estack, void *client_data);
*/

QPair<bool, QString> save_analysis_session(const QString &filename, analysis::Analysis *analysis)
{
    //H5E_auto2_t func(reinterpret_cast<H5E_auto2_t>(H5Eprint2));
    //H5::Exception::setAutoPrint(func, stderr);

    //H5::Exception::dontPrint();

    try
    {
        save_analysis_session_(filename, analysis);
        return qMakePair(true, QString());
    }
    catch (const H5::Exception &e)
    {
        char errorBuffer[1u << 16];
        std::fill(errorBuffer, errorBuffer + sizeof(errorBuffer), '\0');

        if (FILE *errorStream = fmemopen(errorBuffer, sizeof(errorBuffer), "w"))
        {
            fprintf(errorStream, "Hello, World!\n");

            Exception::printErrorStack(errorStream);

            fclose(errorStream); // "Hello, World!\n" shows up in the debugger at this point

            return qMakePair(false, QString(errorBuffer));
        }
    }

    return qMakePair(false, QString(QSL("Unknown Error")));
}

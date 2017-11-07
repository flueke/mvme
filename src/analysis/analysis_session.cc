#include "analysis_session.h"
#include <hdf5.h>
#include <QDir>
#include "analysis.h"

using namespace analysis;

template<size_t Size>
struct Slab
{
    std::array<hsize_t, Size> start;
    std::array<hsize_t, Size> stride;
    std::array<hsize_t, Size> count;
    std::array<hsize_t, Size> block;
};

void save_analysis_session(const QString &filename, analysis::Analysis *analysis)
{
    qDebug() << __PRETTY_FUNCTION__ << filename;

    herr_t err;

    auto outfile = H5Fcreate(filename.toLocal8Bit().constData(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

    auto operators = analysis->getOperators();

    for (auto oe: operators)
    {
        if (auto histoSink = qobject_cast<Histo1DSink *>(oe.op.get()))
        {
            const hsize_t dimensions[] =
            {
                (hsize_t)histoSink->m_histos.size(),
                (hsize_t)histoSink->m_bins
            };

            const hsize_t mem_dimensions[] =
            {
                (hsize_t)histoSink->m_bins
            };

            auto memspace  = H5Screate_simple(2, dimensions, NULL);

            Slab<2> memSlab;
            memSlab.start   = { 0, 0 };
            memSlab.stride  = { 1, 1, };
            memSlab.count   = { 1, (hsize_t)histoSink->m_bins };
            memSlab.block   = { 1, 1 };

            err = H5Sselect_hyperslab(
                memspace, H5S_SELECT_SET, memSlab.start.data(),
                memSlab.stride.data(), memSlab.count.data(),
                memSlab.block.data());

            assert(err >= 0);

            auto filespace = H5Screate_simple(2, dimensions, NULL);
            auto datatype  = H5Tcopy(H5T_NATIVE_DOUBLE);
            err = H5Tset_order(datatype, H5T_ORDER_LE);
            assert(err >= 0);

            auto histoName = (QString("R%1.%2")
                              .arg(histoSink->getMaximumInputRank())
                              .arg(histoSink->objectName())
                              );
            histoName.replace('/', '_');

            qDebug() << "histoName =" << histoName << histoSink;

            auto dataset = H5Dcreate(
                outfile,
                histoName.toLocal8Bit().constData(),
                datatype,
                filespace,
                H5P_DEFAULT,
                H5P_DEFAULT,
                H5P_DEFAULT);

            assert(dataset >= 0);

            for (s32 histoIndex = 0; histoIndex < histoSink->m_histos.size(); histoIndex++)
            {
                const auto histo = histoSink->m_histos[histoIndex];

                Slab<2> fileSlab;
                fileSlab.start  = { (hsize_t)histoIndex, 0 };
                fileSlab.stride = { 1, 1 };
                fileSlab.count  = { 1, (hsize_t)histoSink->m_bins };
                fileSlab.block  = { 1, 1 };

                err = H5Sselect_hyperslab(
                    filespace, H5S_SELECT_SET, fileSlab.start.data(), fileSlab.stride.data(), fileSlab.count.data(), fileSlab.block.data());
                assert(err >= 0);


                /*
                herr_t H5Dwrite(
                    hid_t dataset_id, hid_t mem_type_id, hid_t mem_space_id,
                    hid_t file_space_id, hid_t xfer_plist_id, const void *buf)
                    */
                err = H5Dwrite(
                    dataset,
                    datatype,
                    memspace,
                    filespace,
                    H5P_DEFAULT,
                    histo->data());

                assert(err >= 0);
            }

            H5Tclose(datatype);
            H5Dclose(dataset);
            H5Sclose(filespace);
            H5Sclose(memspace);
        }
    }

    err = H5Fclose(outfile);
}

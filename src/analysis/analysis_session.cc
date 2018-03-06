#include "analysis_session.h"
#include <H5Cpp.h>
#include <QDir>
#include <QJsonDocument>
#include <stdio.h>
#include "analysis.h"

using namespace analysis;
using namespace H5;

/* Stuff I need to load a session:
 * - Analysis Config:
 *   To load into the analysis system. Will create histograms and reserve histogram space.
 * - histo sink uuid and hsitogram data (1d or 2d)
 *
 */

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

StrType make_datatype_string(size_t len)
{
    StrType result(PredType::C_S1, len);
    return result;
}

StrType make_datatype_string(const std::string &str)
{
    return make_datatype_string(str.size());
}

void add_string_attribute(H5Object &h5obj, const std::string &name, const std::string &value)
{
    if (!value.empty())
    {
        DataSpace attr_space(H5S_SCALAR);
        auto datatype = make_datatype_string(value);
        auto attr = h5obj.createAttribute(name, datatype, attr_space);
        attr.write(datatype, value);
    }
}

std::string read_string_attribute(H5Object &h5obj, const std::string &name)
{
    std::string result;

    if (h5obj.attrExists(name))
    {
        Attribute attr = h5obj.openAttribute(name);
        auto datatype = attr.getStrType();
        attr.read(datatype, result);
    }

    return result;
}

void add_string_attribute(H5Object &h5obj, const QString &name, const QString &value)
{
    add_string_attribute(h5obj, name.toStdString(), value.toStdString());
}

QString make_operator_name(OperatorInterface *op)
{
    QString objectName = op->objectName();

    objectName.replace('/', '_');
    objectName.replace("..", "__");

    // "className.rank.objectName.uuid"
    QString result(QString("%1.%2.%3.%4")
                   .arg(op->metaObject()->className())
                   .arg(op->getMaximumInputRank())
                   .arg(objectName)
                   .arg(op->getId().toString())
                  );
    return result;
}

void save_Histo1DSink(H5File &outfile, Histo1DSink *histoSink)
{
    assert(histoSink);

    auto histoName = make_operator_name(histoSink);

    const hsize_t dimensions[] =
    {
        (hsize_t)histoSink->m_histos.size(),
        (hsize_t)histoSink->m_bins
    };

    DataSpace memspace(2, dimensions, nullptr);

    Slab<2> memSlab;
    memSlab.start   = {{ 0, 0 }};
    memSlab.count   = {{ 1, (hsize_t)histoSink->m_bins }};

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

    add_string_attribute(dataset, QString("className"), QString(histoSink->metaObject()->className()));
    add_string_attribute(dataset, "id", histoSink->getId().toString());

    for (s32 histoIndex = 0; histoIndex < histoSink->m_histos.size(); histoIndex++)
    {
        const auto histo = histoSink->m_histos[histoIndex];

        Slab<2> fileSlab;
        fileSlab.start  = { { (hsize_t)histoIndex, 0 } };
        fileSlab.count  = { { 1, (hsize_t)histoSink->m_bins } };

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
    assert(histoSink);
    assert(histoSink->m_histo);

    auto histoName = make_operator_name(histoSink);

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

    add_string_attribute(dataset, QString("className"), QString(histoSink->metaObject()->className()));
    add_string_attribute(dataset, "id", histoSink->getId().toString());

    dataset.write(
        histo->data(),
        make_datatype_native_double(),
        dataspace,
        dataspace);
}

void save_RateMonitorSink(H5File &outfile, RateMonitorSink *rms)
{
    auto name = make_operator_name(rms);
    const auto samplerCount = rms->rateSamplerCount();
    const auto capacity     = rms->getRateHistoryCapacity();

    qDebug() << __PRETTY_FUNCTION__ << "count =" << samplerCount << ", capacity =" << capacity;

    const hsize_t dimensions[] =
    {
        (hsize_t)samplerCount,
        (hsize_t)capacity + 1 // HACK: store RateSampler::totalSamples as the first element of the dataset
    };

    DataSpace memspace(2, dimensions, nullptr);

    Slab<2> memSlab;
    memSlab.start   = {{ 0, 0 }};
    memSlab.count   = {{ 1, (hsize_t)capacity + 1 }};

    memspace.selectHyperslab(
        H5S_SELECT_SET,
        memSlab.count.data(),
        memSlab.start.data());

    DataSpace filespace(2, dimensions, NULL);

    DSetCreatPropList dataset_creation_plist;

    // one sampler
    const hsize_t chunk_dimensions[] = { 1, (hsize_t)capacity + 1 };

    dataset_creation_plist.setChunk(2, chunk_dimensions);
    dataset_creation_plist.setDeflate(compressionLevel);

    DataSet dataset = outfile.createDataSet(
        name.toLocal8Bit().constData(),
        make_datatype_native_double(),
        filespace,
        dataset_creation_plist
        );

    add_string_attribute(dataset, QString("className"), QString(rms->metaObject()->className()));
    add_string_attribute(dataset, "id", rms->getId().toString());

    std::vector<double> buffer;
    buffer.resize(capacity + 1, 0.0);

    for (s32 si = 0; si < samplerCount; si++)
    {
        auto sampler = rms->getRateSampler(si);

        std::fill(buffer.begin(), buffer.end(), make_quiet_nan());

        buffer[0] = sampler->totalSamples; // HACK: store RateSampler::totalSamples as the first element of the dataset

        for (size_t historyIndex = 0; historyIndex < sampler->rateHistory.size(); historyIndex++)
        {
            buffer[historyIndex + 1] = sampler->rateHistory[historyIndex];
        }

        Slab<2> fileSlab;
        fileSlab.start  = { { (hsize_t)si, 0 } };
        fileSlab.count  = { { 1, (hsize_t)capacity + 1 } };

        filespace.selectHyperslab(
            H5S_SELECT_SET,
            fileSlab.count.data(),
            fileSlab.start.data());

        dataset.write(buffer.data(),
                      make_datatype_native_double(),
                      memspace,
                      filespace);
    }
}

QJsonObject analysis_to_json(analysis::Analysis *analysis)
{
    QJsonObject result;
    analysis->write(result);
    return result;
}

void save_analysis_session_(const QString &filename, analysis::Analysis *analysis)
{
    H5File outfile(filename.toLocal8Bit().constData(), H5F_ACC_TRUNC);

    // Histograms
    auto operators = analysis->getOperators();

    for (auto oe: operators)
    {
        if (auto histoSink = qobject_cast<Histo1DSink *>(oe.op.get()))
        {
            save_Histo1DSink(outfile, histoSink);
        }
        else if (auto histoSink = qobject_cast<Histo2DSink *>(oe.op.get()))
        {
            if (histoSink->m_histo)
            {
                save_Histo2DSink(outfile, histoSink);
            }
        }
        else if (auto rms = qobject_cast<RateMonitorSink *>(oe.op.get()))
        {
            save_RateMonitorSink(outfile, rms);
        }
    }

    // RunInfo group
    auto runInfo = analysis->getRunInfo();

    if (!runInfo.runId.isEmpty())
    {
        auto runInfo_group = outfile.createGroup("RunInfo");
        add_string_attribute(runInfo_group, "runId", runInfo.runId);
    }

    // analysis json stored in a dataset of characters
    QJsonDocument doc(analysis_to_json(analysis));
    QByteArray analysisJson = doc.toJson();

    DataType analysis_datatype(PredType::NATIVE_CHAR);

    const hsize_t dimensions[] = { (hsize_t) analysisJson.size() };
    DataSpace analysis_dataspace(1, dimensions, nullptr);

    const hsize_t chunk_dimensions[] = { 4096 };
    DSetCreatPropList analysis_dataset_creation_plist;
    analysis_dataset_creation_plist.setChunk(1, chunk_dimensions);
    analysis_dataset_creation_plist.setDeflate(compressionLevel);

    DataSet analysis_dataset = outfile.createDataSet(
        "Analysis",
        analysis_datatype,
        analysis_dataspace,
        analysis_dataset_creation_plist);

    analysis_dataset.write(
        analysisJson.data(),
        analysis_datatype,
        analysis_dataspace);
}

void load_Histo1DSink(DataSet &dataset, Histo1DSink *histoSink)
{
    auto dataspace = dataset.getSpace();

    if (!(dataspace.isSimple() && dataspace.getSimpleExtentNdims() == 2))
        return;

    hsize_t dimensions[2];

    dataspace.getSimpleExtentDims(dimensions);

    if (dimensions[0] != (hsize_t)histoSink->m_histos.size())
        return;

    DataSpace memspace(2, dimensions, nullptr);

    Slab<2> memSlab;
    memSlab.start   = { 0, 0 };
    memSlab.count   = { 1, dimensions[1] };

    memspace.selectHyperslab(
        H5S_SELECT_SET,
        memSlab.count.data(),
        memSlab.start.data());

    for (s32 histoIndex = 0; histoIndex < histoSink->m_histos.size(); histoIndex++)
    {
        auto histo = histoSink->m_histos[histoIndex];

        if (dimensions[1] != histo->getNumberOfBins())
            continue;

        Slab<2> fileSlab;
        fileSlab.start  = { (hsize_t)histoIndex, 0 };
        fileSlab.count  = { 1, dimensions[1] };

        dataspace.selectHyperslab(
            H5S_SELECT_SET,
            fileSlab.count.data(),
            fileSlab.start.data());

        dataset.read(
            histo->data(),
            make_datatype_native_double(),
            memspace,
            dataspace);
    }
}

void load_Histo2DSink(DataSet &dataset, Histo2DSink *histoSink)
{
    assert(histoSink);
    assert(histoSink->m_histo);

    auto histo = histoSink->m_histo;
    auto binnings = histo->getAxisBinnings();

    auto dataspace = dataset.getSpace();

    if (!(dataspace.isSimple() && dataspace.getSimpleExtentNdims() == 2))
        return;

    hsize_t dimensions[2];

    dataspace.getSimpleExtentDims(dimensions);

    if (dimensions[0] != binnings[Qt::XAxis].getBins()
        || dimensions [1] != binnings[Qt::YAxis].getBins())
        return;

    dataset.read(
        histo->data(),
        make_datatype_native_double());
}

void load_RateMonitorSink(DataSet &dataset, RateMonitorSink *rms)
{
    const auto samplerCount = rms->rateSamplerCount();
    const auto capacity     = rms->getRateHistoryCapacity();

    auto dataspace = dataset.getSpace();

    if (!(dataspace.isSimple() && dataspace.getSimpleExtentNdims() == 2))
        return;

    hsize_t dimensions[2];

    dataspace.getSimpleExtentDims(dimensions);

    if (dimensions[0] != (hsize_t)rms->rateSamplerCount())
        return;

    DataSpace memspace(2, dimensions, nullptr);

    Slab<2> memSlab;
    memSlab.start   = { { 0, 0 } };
    memSlab.count   = { { 1, dimensions[1] } };

    memspace.selectHyperslab(
        H5S_SELECT_SET,
        memSlab.count.data(),
        memSlab.start.data());

    std::vector<double> buffer;
    buffer.resize(capacity + 1, 0.0);

    for (s32 si = 0; si < samplerCount; si++)
    {
        auto sampler = rms->getRateSampler(si);

        if (dimensions[1] != sampler->rateHistory.capacity() + 1)
            continue;

        Slab<2> fileSlab;
        fileSlab.start  = { { (hsize_t)si, 0 } };
        fileSlab.count  = { { 1, dimensions[1] } };

        dataspace.selectHyperslab(
            H5S_SELECT_SET,
            fileSlab.count.data(),
            fileSlab.start.data());

        std::fill(buffer.begin(), buffer.end(), 0.0);

        dataset.read(
            buffer.data(),
            make_datatype_native_double(),
            memspace,
            dataspace);

        sampler->totalSamples = buffer[0];
        sampler->rateHistory.clear();

        for (size_t historyIndex = 1; historyIndex < buffer.size(); historyIndex++)
        {
            if (!std::isnan(buffer[historyIndex]))
                sampler->rateHistory.push_back(buffer[historyIndex]);
        }
    }
}

void load_analysis_session_(const QString &filename, analysis::Analysis *analysis)
{
    H5File infile(filename.toStdString(), H5F_ACC_RDONLY);

    const hsize_t numObj = infile.getNumObjs();

    for (hsize_t objIndex = 0;
         objIndex < numObj;
         objIndex++)
    {
        auto objType = infile.childObjType(objIndex);

        if (objType == H5O_TYPE_DATASET)
        {
            auto objName = infile.getObjnameByIdx(objIndex);
            DataSet dataset = infile.openDataSet(objName);

            if (dataset.attrExists("className") && dataset.attrExists("id"))
            {
                auto id_str = read_string_attribute(dataset, "id");
                auto id = QUuid(QString::fromStdString(id_str));

                if (auto histoSink = qobject_cast<Histo1DSink *>(analysis->getOperator(id).get()))
                {
                    load_Histo1DSink(dataset, histoSink);
                }
                else if (auto histoSink = qobject_cast<Histo2DSink *>(analysis->getOperator(id).get()))
                {
                    if (histoSink->m_histo)
                    {
                        load_Histo2DSink(dataset, histoSink);
                    }
                }
                else if (auto rms = qobject_cast<RateMonitorSink *>(analysis->getOperator(id).get()))
                {
                    load_RateMonitorSink(dataset, rms);
                }
            }
        }
    }
}

QJsonDocument load_analysis_config_from_session_file_(const QString &filename)
{
    QJsonDocument result;

    H5File infile(filename.toStdString(), H5F_ACC_RDONLY);

    DataSet dataset = infile.openDataSet("Analysis");

    DataType analysis_datatype(PredType::NATIVE_CHAR);
    std::string jsonString;

    dataset.read(jsonString, analysis_datatype);

    auto jsonData = QByteArray::fromStdString(jsonString);

    result = QJsonDocument::fromJson(jsonData);

    qDebug() << result.toJson();

    return result;
}

struct WalkerData
{
    QStringList buffer;
    u8 flags = 0;

    static const u8 Abort = 1u << 0;
};

/* Note: H5E_error2_t is defined in /usr/include/hdf5/serial/H5Epublic.h */
herr_t error_stack_walker(unsigned n, const H5E_error2_t *err, void *client_data)
{
    auto d = reinterpret_cast<WalkerData *>(client_data);

    if (d->flags & WalkerData::Abort)
        return 0;

#if 0
    qDebug() << __PRETTY_FUNCTION__ << n;
    qDebug() << "\t" << err->cls_id << err->maj_num << err->min_num << err->line
             << err->func_name << err->file_name << err->desc;
#endif

    auto msg = (QString("%1(): %2")
                .arg(err->func_name)
                .arg(err->desc)
               );

    d->buffer.push_back(msg);

    if (err->func_name && strcmp(err->func_name, "H5FD_sec2_open") == 0)
        d->flags |= WalkerData::Abort;

    return 0; // -1 causes a "can not walk error"
}

QString get_h5_error_message(const H5::Exception &e)
{
    //qDebug() << __PRETTY_FUNCTION__;
    //qDebug() << e.getCDetailMsg();
    //qDebug() << e.getCFuncName();

    WalkerData walkerData = {};

    Exception::walkErrorStack(H5E_WALK_UPWARD, error_stack_walker, reinterpret_cast<void *>(&walkerData));

    auto result = walkerData.buffer.join('\n');

    qDebug() << __PRETTY_FUNCTION__ << result;

    return result;

#if 0
    // Note: No fmemopen() under Windows.
    char errorBuffer[1u << 16];
    std::fill(errorBuffer, errorBuffer + sizeof(errorBuffer), '\0');

    if (FILE *errorStream = fmemopen(errorBuffer, sizeof(errorBuffer), "w"))
    {
        Exception::printErrorStack(errorStream);

        fclose(errorStream);

        return QString(errorBuffer);
    }

    return QString();
#endif
}

} /* end anon namespace */

namespace analysis
{

QPair<bool, QString> save_analysis_session(const QString &filename, analysis::Analysis *analysis)
{
    qDebug() << __PRETTY_FUNCTION__;

    auto result = qMakePair(true, QString());

    try
    {
        save_analysis_session_(filename, analysis);
    }
    catch (const H5::Exception &e)
    {
        result.first = false;
        result.second = get_h5_error_message(e);
    }

    return result;
}

QPair<bool, QString> load_analysis_session(const QString &filename, analysis::Analysis *analysis)
{
    qDebug() << __PRETTY_FUNCTION__;

    auto result = qMakePair(true, QString());

    try
    {
        load_analysis_session_(filename, analysis);
    }
    catch (const H5::Exception &e)
    {
        result.first = false;
        result.second = get_h5_error_message(e);
    }

    return result;
}

QPair<QJsonDocument, QString> load_analysis_config_from_session_file(const QString &filename)
{
    qDebug() << __PRETTY_FUNCTION__;

    QPair<QJsonDocument, QString> result;

    try
    {
        result.first = load_analysis_config_from_session_file_(filename);
    }
    catch (const H5::Exception &e)
    {
        result.second = get_h5_error_message(e);
    }

    return result;
}

QPair<bool, QString> analysis_session_system_init()
{
    qDebug() << __PRETTY_FUNCTION__;

    auto result = qMakePair(true, QString());

#if 0
    try
    {
        H5Library::open();
        H5Library::initH5cpp();
    }
    catch (const H5::Exception &e)
    {
        result.first = false;
        result.second = get_h5_error_message(e);
    }
#endif

    return result;
}

QPair<bool, QString> analysis_session_system_destroy()
{
    qDebug() << __PRETTY_FUNCTION__;

    auto result = qMakePair(true, QString());

#if 0
    try
    {
        H5Library::termH5cpp();
        H5Library::close();
    }
    catch (const H5::Exception &e)
    {
        result.first = false;
        result.second = get_h5_error_message(e);
    }
#endif

    return result;
}

} // end namespace analysis

#include "json_rpc_server.h"
#include "jcon_assert.h"
#include "json_rpc_endpoint.h"
#include "json_rpc_error.h"
#include "json_rpc_file_logger.h"
#include "string_util.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QMetaMethod>

namespace {
    QString logInvoke(const QMetaMethod& meta_method,
                      const QVariantList& args,
                      const QVariant& return_value,
                      const QVariant &exception_info = {});
}

namespace jcon {

const QString JsonRpcServer::InvalidRequestId = "";

JsonRpcServer::JsonRpcServer(QObject* parent,
                             std::shared_ptr<JsonRpcLogger> logger)
    : QObject(parent)
    , m_logger(logger)
{
    if (!m_logger) {
        m_logger = std::make_shared<JsonRpcFileLogger>("json_server_log.txt");
    }
}

JsonRpcServer::~JsonRpcServer()
{
}

void JsonRpcServer::registerServices(const QObjectList& services)
{
    m_services = services;
}

void JsonRpcServer::jsonRequestReceived(const QJsonObject& request,
                                        QObject* socket)
{
    //qDebug() << __PRETTY_FUNCTION__ << "received request:" <<
    //    QJsonDocument(request).toJson();

    JCON_ASSERT(request.value("jsonrpc").toString() == "2.0");

    if (request.value("jsonrpc").toString() != "2.0") {
        logError("invalid protocol tag");
        return;
    }

    QString method_name = request.value("method").toString();

    if (method_name.isEmpty()) {
        logError("no method present in request");
        return;
    }

    QVariant params = request.value("params").toVariant();

    if (params.isNull())
    {
        params = QVariantList();
    }

    QString request_id = request.value("id").toVariant().toString();

    QVariant return_value;
    QVariant error_info;

    if (!dispatch(method_name, params, request_id, return_value, error_info))
    {
        QJsonDocument error;

        if (!error_info.isNull())
        {
            error = createErrorResponse(request_id, error_info);
            logError(error.toJson());
        }
        else
        {
            auto msg = QString("method '%1' not found, check name and "
                               "parameter types ").arg(method_name);

            error = createErrorResponse(request_id, JsonRpcError::EC_MethodNotFound, msg);
            logError(msg);
        }

        // send error response if request had valid ID
        if (request_id != InvalidRequestId)
        {
            if (JsonRpcEndpoint* endpoint = findClient(socket))
            {
                endpoint->send(error);
            }
            else
            {
                logError("invalid client socket, cannot send response");
            }
        }

        return;
    }

    // send response if request had valid ID
    if (request_id != InvalidRequestId) {
        QJsonDocument response = createResponse(request_id,
                                                return_value,
                                                method_name);

        JsonRpcEndpoint* endpoint = findClient(socket);
        if (!endpoint) {
            logError("invalid client socket, cannot send response");
            return;
        }

        endpoint->send(response);
    }
}

void JsonRpcServer::invalidJsonReceived(const QString &data, QObject *socket)
{
    //qDebug() << __PRETTY_FUNCTION__ << "received invalid json data:" << data
    //    << "sending error response";

    if (JsonRpcEndpoint *endpoint = findClient(socket))
    {
        QJsonDocument error = createErrorResponse(
            QString(), JsonRpcError::EC_ParseError, "Parse error");

        endpoint->send(error);
    }
}

bool JsonRpcServer::dispatch(const QString& method_name,
                             const QVariant& params,
                             const QString& request_id,
                             QVariant& return_value,
                             QVariant &error_info)
{
    for (auto& s : m_services) {
        const QMetaObject* meta_obj = s->metaObject();
        for (int i = 0; i < meta_obj->methodCount(); ++i) {
            auto meta_method = meta_obj->method(i);
            if (meta_method.name() == method_name) {
                if (params.type() == QVariant::List ||
                    params.type() == QVariant::StringList)
                {
                    if (call(s, meta_method, params.toList(), return_value, error_info)) {
                        return true;
                    }
                } else if (params.type() == QVariant::Map) {
                    if (call(s, meta_method, params.toMap(), return_value, error_info)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool JsonRpcServer::call(QObject* object,
                         const QMetaMethod& meta_method,
                         const QVariantList& args,
                         QVariant& return_value,
                         QVariant &error_info)
{
    return_value = QVariant();

    QVariantList converted_args;
    if (!convertArgs(meta_method, args, converted_args)) {
        return false;
    }

    return doCall(object, meta_method, converted_args, return_value, error_info);
}

bool JsonRpcServer::call(QObject* object,
                         const QMetaMethod& meta_method,
                         const QVariantMap& args,
                         QVariant& return_value,
                         QVariant &error_info)
{
    return_value = QVariant();

    QVariantList converted_args;
    if (!convertArgs(meta_method, args, converted_args)) {
        return false;
    }

    return doCall(object, meta_method, converted_args, return_value, error_info);
}

bool JsonRpcServer::convertArgs(const QMetaMethod& meta_method,
                                const QVariantList& args,
                                QVariantList& converted_args)
{
    QList<QByteArray> param_types = meta_method.parameterTypes();
    if (args.size() != param_types.size()) {
        logError(QString("wrong number of arguments to method %1 -- "
                         "expected %2 arguments, but got %3")
                 .arg(QString(meta_method.methodSignature()))
                 .arg(meta_method.parameterCount())
                 .arg(args.size()));
        return false;
    }

    for (int i = 0; i < param_types.size(); i++) {
        const QVariant& arg = args.at(i);
        if (!arg.isValid()) {
            logError(QString("argument %1 of %2 to method %3 is invalid")
                     .arg(i + 1)
                     .arg(param_types.size())
                     .arg(QString(meta_method.methodSignature())));
            return false;
        }

        QByteArray arg_type_name = arg.typeName();
        QByteArray param_type_name = param_types.at(i);

        QVariant::Type param_type = QVariant::nameToType(param_type_name);

        QVariant copy = QVariant(arg);

        if (copy.type() != param_type) {
            if (copy.canConvert(param_type)) {
                if (!copy.convert(param_type)) {
                    // qDebug() << "cannot convert" << arg_type_name
                    //          << "to" << param_type_name;
                    return false;
                }
            }
        }

        converted_args << copy;
    }
    return true;
}

bool JsonRpcServer::convertArgs(const QMetaMethod& meta_method,
                                const QVariantMap& args,
                                QVariantList& converted_args)
{
    QList<QByteArray> param_types = meta_method.parameterTypes();
    if (args.size() != param_types.size()) {
        logError(QString("wrong number of arguments to method %1 -- "
                         "expected %2 arguments, but got %3")
                 .arg(QString(meta_method.methodSignature()))
                 .arg(meta_method.parameterCount())
                 .arg(args.size()));
        return false;
    }

    for (int i = 0; i < param_types.size(); i++) {
        QByteArray param_name = meta_method.parameterNames().at(i);
        if (args.find(param_name) == args.end()) {
            // no arg with param name found
            return false;
        }
        const QVariant& arg = args.value(param_name);
        if (!arg.isValid()) {
            logError(QString("argument %1 of %2 to method %3 is invalid")
                     .arg(i + 1)
                     .arg(param_types.size())
                     .arg(QString(meta_method.methodSignature())));
            return false;
        }

        QByteArray arg_type_name = arg.typeName();
        QByteArray param_type_name = param_types.at(i);

        QVariant::Type param_type = QVariant::nameToType(param_type_name);

        QVariant copy = QVariant(arg);

        if (copy.type() != param_type) {
            if (copy.canConvert(param_type)) {
                if (!copy.convert(param_type)) {
                    // qDebug() << "cannot convert" << arg_type_name
                    //          << "to" << param_type_name;
                    return false;
                }
            }
        }

        converted_args << copy;
    }
    return true;
}

// based on https://gist.github.com/andref/2838534.
bool JsonRpcServer::doCall(QObject* object,
                           const QMetaMethod& meta_method,
                           QVariantList& converted_args,
                           QVariant& return_value,
                           QVariant &error_info)
{
    QList<QGenericArgument> arguments;

    for (int i = 0; i < converted_args.size(); i++) {

        // Notice that we have to take a reference to the argument, else we'd be
        // pointing to a copy that will be destroyed when this loop exits.
        QVariant& argument = converted_args[i];

        // A const_cast is needed because calling data() would detach the
        // QVariant.
        QGenericArgument generic_argument(
            QMetaType::typeName(argument.userType()),
            const_cast<void*>(argument.constData())
        );

        arguments << generic_argument;
    }

    const char* return_type_name = meta_method.typeName();
    int return_type = QMetaType::type(return_type_name);
    if (return_type != QMetaType::Void) {
        return_value = QVariant(return_type, nullptr);
    }

    QGenericReturnArgument return_argument(
        return_type_name,
        const_cast<void*>(return_value.constData())
    );

    // Perform the call. If an exception is thrown error_info will be filled in.
    try
    {
        // perform the call
        bool ok = meta_method.invoke(
            object,
            Qt::DirectConnection,
            return_argument,
            arguments.value(0),
            arguments.value(1),
            arguments.value(2),
            arguments.value(3),
            arguments.value(4),
            arguments.value(5),
            arguments.value(6),
            arguments.value(7),
            arguments.value(8),
            arguments.value(9)
        );

        if (!ok) {
            qDebug() << "Error: calling" << meta_method.methodSignature() << "failed.";
            return false;
        }

        logInfo(logInvoke(meta_method, converted_args, return_value));
    }
    catch (const QVariant &exception_info)
    {
        /* This is a way for the service implementation to gain fine grained control over
         * the returned JSON-RPC error object. The thrown QVariant can be a map containing
         * "code", "message" and optionally "data" which is then passed through as the
         * error object returned to the caller.
         * If the QVariant is not a map or does not contain the required keys it is
         * converted to a Json object and used as the value of "data" in the resulting
         * error object.
         * Error objects: http://www.jsonrpc.org/specification#error_object
         */

        //qDebug() << "Error: calling" << meta_method.methodSignature()
        //    << "raised a QVariantMap exception:" << exception_info;

        error_info = exception_info;
        logInfo(logInvoke(meta_method, converted_args, return_value, exception_info));

        return false;
    }
    catch (const QVariantMap &exception_info)
    {
        error_info = exception_info;
        logInfo(logInvoke(meta_method, converted_args, return_value, exception_info));
        return false;
    }
    catch (const std::exception &e)
    {
        //qDebug() << "Error: calling" << meta_method.methodSignature()
        //    << "raised a std::exception:" << e.what();

        QVariantMap exception_info {
            { "code", -32603 }, // internal server error
            { "message", QString("Procedure call raised an exception: %1").arg(e.what()) }
        };

        error_info = exception_info;
        logInfo(logInvoke(meta_method, converted_args, return_value, exception_info));

        return false;
    }

    return true;
}

QJsonDocument JsonRpcServer::createResponse(const QString& request_id,
                                            const QVariant& return_value,
                                            const QString& method_name)
{
    QJsonObject res_json_obj {
        { "jsonrpc", "2.0" },
        { "id", request_id }
    };

    if (return_value.type() == QVariant::Invalid) {
        res_json_obj["result"] = QJsonValue();
    } else if (return_value.type() == QVariant::List) {
        auto ret_doc = QJsonDocument::fromVariant(return_value);
        res_json_obj["result"] = ret_doc.array();
    } else if (return_value.type() == QVariant::Map) {
        auto ret_doc = QJsonDocument::fromVariant(return_value);
        res_json_obj["result"] = ret_doc.object();
    } else if (return_value.type() == QVariant::Int) {
        res_json_obj["result"] = return_value.toInt();
    } else if (return_value.type() == QVariant::LongLong) {
        res_json_obj["result"] = return_value.toLongLong();
    } else if (return_value.type() == QVariant::Double) {
        res_json_obj["result"] = return_value.toDouble();
    } else if (return_value.type() == QVariant::Bool) {
        res_json_obj["result"] = return_value.toBool();
    } else if (return_value.type() == QVariant::String) {
        res_json_obj["result"] = return_value.toString();
    } else if (return_value.type() == QVariant::StringList) {
        res_json_obj["result"] = QJsonValue::fromVariant(return_value);
    } else {
        auto msg =
            QString("method '%1' has unknown return type: %2")
            .arg(method_name)
            .arg(return_value.type());
        logError(msg);
        return createErrorResponse(request_id,
                                   JsonRpcError::EC_InvalidRequest,
                                   msg);
    }

    return QJsonDocument(res_json_obj);
}

QJsonDocument JsonRpcServer::createErrorResponse(const QString& request_id,
                                                 int code,
                                                 const QString& message)
{
    QJsonObject error_object {
        { "code", code },
        { "message", message }
    };

    QJsonObject res_json_obj {
        { "jsonrpc", "2.0" },
        { "error", error_object }
    };

    if (!request_id.isNull())
    {
        res_json_obj["id"] = request_id;
    }
    else
    {
        res_json_obj["id"] = QJsonValue::Null;
    }

    return QJsonDocument(res_json_obj);
}

QJsonDocument JsonRpcServer::createErrorResponse(const QString& request_id,
                                                 const QVariant &error_info)
{
    QJsonObject error_object;

    if (error_info.type() == QVariant::Map)
    {
        auto error_map = error_info.toMap();

        if (error_map.contains("code") && error_map.contains("message"))
        {
            // error_info is in json spec format: pass it through
            error_object = QJsonObject::fromVariantMap(error_map);
        }
        else
        {
            error_object["code"] = -32603; // internal server error
            error_object["message"] = "Procedure call raised an exception";
            error_object["data"] = QJsonObject::fromVariantMap(error_map);
        }
    }
    else
    {
        error_object["code"] = -32603; // internal server error
        error_object["message"] = "Procedure call raised an exception";
        error_object["data"] = error_info.toJsonObject();
    }

    QJsonObject res_json_obj {
        { "jsonrpc", "2.0" },
        { "error", error_object }
    };

    if (!request_id.isNull())
    {
        res_json_obj["id"] = request_id;
    }
    else
    {
        res_json_obj["id"] = QJsonValue::Null;
    }

    return QJsonDocument(res_json_obj);
}

void JsonRpcServer::logInfo(const QString& msg)
{
    m_logger->logInfo("JSON RPC server: " + msg);
}

void JsonRpcServer::logError(const QString& msg)
{
    m_logger->logError("JSON RPC server error: " + msg);
}

}

namespace {

QString logInvoke(const QMetaMethod& meta_method,
                  const QVariantList& args,
                  const QVariant& return_value,
                  const QVariant &exception_info)
{
    const auto ns = meta_method.parameterNames();
    auto ps = jcon::variantListToStringList(args);
    QStringList args_sl;
    std::transform(ns.begin(), ns.end(), ps.begin(),
                   std::back_inserter(args_sl),
                   [](auto x, auto y) -> QString {
                       return static_cast<QString>(x) + ": " + y;
                   }
        );

    auto msg = QString("%1 invoked ")
        .arg(static_cast<QString>(meta_method.name()));

    if (args_sl.empty()) {
        msg += "without arguments";
    } else {
        msg += QString("with argument%1: %2")
            .arg(args_sl.size() == 1 ? "" : "s")
            .arg(args_sl.join(", "));
    }

    if (!exception_info.isNull())
    {
        msg += " -> exception_info: " + jcon::variantToString(exception_info);
    }

    if (return_value.isValid()) {
        msg += " -> returning: " + jcon::variantToString(return_value);
    }

    return msg;
}

}

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
#ifndef __MVME_UTIL_H__
#define __MVME_UTIL_H__

#include <QMetaType>
#include <QPair>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>

#include "libmvme_export.h"
#include "typedefs.h"
#include "qt_util.h"
#include "util/assert.h"

#define ArrayCount(a) (sizeof(a) / sizeof((a)[0]))

// Allows storing std::shared_ptr to QObject or derived inside QVariant.
Q_DECLARE_SMART_POINTER_METATYPE(std::shared_ptr);

class QTextStream;

LIBMVME_EXPORT void qDebugOutputBuffer(u8 *dataBuffer, size_t bufferSize);
LIBMVME_EXPORT QTextStream &debugOutputBuffer(QTextStream &out, u8 *dataBuffer, size_t bufferSize);

LIBMVME_EXPORT QVector<u32> parseStackFile(QTextStream &input);
LIBMVME_EXPORT QVector<u32> parseStackFile(const QString &input);

typedef QPair<u32, QVariant> RegisterSetting; // (addr, value)
typedef QVector<RegisterSetting> RegisterList;

LIBMVME_EXPORT RegisterList parseRegisterList(QTextStream &input, u32 baseAddress = 0);
LIBMVME_EXPORT RegisterList parseRegisterList(const QString &input, u32 baseAddress = 0);

inline bool isFloat(const QVariant &var)
{
    return (static_cast<QMetaType::Type>(var.type()) == QMetaType::Float);
}

LIBMVME_EXPORT QString toString(const RegisterList &registerList);
LIBMVME_EXPORT QStringList toStringList(const RegisterList &registerList);

class LIBMVME_EXPORT end_of_buffer: public std::runtime_error
{
    public:
        explicit end_of_buffer(const char *arg): std::runtime_error(arg) {}
        end_of_buffer(): std::runtime_error("end_of_buffer") {}
};

struct LIBMVME_EXPORT BufferIterator
{
    enum Alignment { Align16, Align32 };

    u8 *data = nullptr;
    u8 *buffp = nullptr;
    u8 *endp = nullptr;
    size_t size = 0;
    Alignment alignment = Align32;


    BufferIterator()
    {}

    BufferIterator(u8 *data, size_t size, Alignment alignment = Align32)
        : data(data)
        , buffp(data)
        , endp(data + size)
        , size(size)
        , alignment(alignment)
    {}

    BufferIterator(u32 *d, size_t sz)
        : data(reinterpret_cast<u8 *>(d))
        , buffp(data)
        , endp(data + sz * sizeof(u32))
        , size(sz * sizeof(u32))
        , alignment(Align32)
    {}

    inline bool align32() const { return alignment == Align32; }

    inline u8 extractU8()
    {
        if (buffp + sizeof(u8) > endp)
            throw end_of_buffer();

        u8 ret = *buffp;
        buffp += sizeof(u8);
        return ret;
    }

    inline u16 extractU16()
    {
        if (buffp + sizeof(u16) > endp)
            throw end_of_buffer();

        u32 ret = *reinterpret_cast<u16 *>(buffp);
        buffp += sizeof(u16);
        return ret;
    }

    inline u32 extractU32()
    {
        if (buffp + sizeof(u32) > endp)
            throw end_of_buffer();

        u32 ret = *reinterpret_cast<u32 *>(buffp);
        buffp += sizeof(u32);
        return ret;
    }

    inline u32 extractWord()
    {
        return align32() ? extractU32() : extractU16();
    }

    inline u8 extractByte()
    {
        return extractU8();
    }

    inline u16 extractShortword()
    {
        return extractU16();
    }

    inline u32 extractLongword()
    {
        return extractU32();
    }

    inline u16 peekU16() const
    {
        if (buffp + sizeof(u16) > endp)
            throw end_of_buffer();

        u32 ret = *reinterpret_cast<u16 *>(buffp);
        return ret;
    }

    inline u32 peekU32(size_t index = 0) const
    {
        if (buffp + sizeof(u32) * index > endp)
            throw end_of_buffer();

        u32 ret = *(reinterpret_cast<u32 *>(buffp) + index);
        return ret;
    }

    inline u32 peekWord() const
    {
        return align32() ? peekU32() : peekU16();
    }

    // Pushes a value onto the back of the buffer. Returns a pointer to the
    // newly pushed value.
    // Note: this does not take the alignment flag into account.
    template <typename T>
    inline T *push(T value)
    {
        static_assert(std::is_trivial<T>::value, "push<T>() works for trivial types only");

        if (buffp + sizeof(T) > endp)
            throw end_of_buffer();

        T *ret = reinterpret_cast<T *>(buffp);
        buffp += sizeof(T);
        *ret = value;
        return ret;
    }

    inline u32 bytesLeft() const
    {
        return endp - buffp;
    }

    inline u32 wordsLeft() const
    {
        return bytesLeft() / (align32() ? sizeof(u32) : sizeof(u16));
    }

    inline u32 shortwordsLeft() const
    {
        return bytesLeft() / sizeof(u16);
    }

    inline u32 longwordsLeft() const
    {
        return bytesLeft() / sizeof(u32);
    }

    inline u8  *asU8()  { return reinterpret_cast<u8 *>(buffp); }
    inline u16 *asU16() { return reinterpret_cast<u16 *>(buffp); }
    inline u32 *asU32() { return reinterpret_cast<u32 *>(buffp); }

    inline u32 *indexU32(size_t index)
    {
        if (buffp + index * sizeof(u32) > endp)
            throw end_of_buffer();

        return reinterpret_cast<u32 *>(buffp) + index;
    }

    // Skips forward. Truncates to the buffer end if skipping would result in a
    // position behind the buffer end.
    inline void skip(size_t bytes)
    {
        buffp += bytes;
        if (buffp > endp)
            buffp = endp;
    }

    inline void skip(size_t width, size_t count)
    {
        skip(width * count);
    }

    // Skips forward. Throws end_of_buffer if skipping would exceed the end of
    // buffer.
    inline void skipExact(size_t bytes)
    {
        if (buffp + bytes > endp)
            throw end_of_buffer();

        buffp += bytes;
    }

    inline void skipExact(size_t width, size_t count)
    {
        skipExact(width * count);
    }

    inline bool atEnd() const { return buffp == endp; }

    inline void rewind() { buffp = data; }
    inline bool isEmpty() const { return size == 0; }
    inline bool isNull() const { return !data; }
    inline size_t used() const { return buffp - data; }

    inline ptrdiff_t current32BitOffset() const
    {
        return reinterpret_cast<u32 *>(buffp) - reinterpret_cast<u32 *>(data);
    }
};

LIBMVME_EXPORT QString readStringFile(const QString &filename);

template<typename T>
T *Var2Ptr(const QVariant &variant)
{
    return static_cast<T *>(variant.value<void *>());
}

template<typename T>
T *Var2QObject(const QVariant &variant)
{
    return qobject_cast<T *>(Var2Ptr<QObject>(variant));
}

template<typename T>
QVariant Ptr2Var(T *ptr)
{
    return QVariant::fromValue(static_cast<void *>(ptr));
}

LIBMVME_EXPORT QString makeDurationString(qint64 durationSeconds);

/** Emits aboutToClose() before returning from closeEvent() */
class LIBMVME_EXPORT MVMEWidget: public QWidget
{
    Q_OBJECT
    signals:
        void aboutToClose();

    public:
        explicit MVMEWidget(QWidget *parent = 0);

    protected:
        void closeEvent(QCloseEvent *event) override;
};

class LIBMVME_EXPORT TemplateLoader: public QObject
{
    Q_OBJECT
    signals:
        void logMessage(const QString &msg);

    public:
        QString getTemplatePath();
        QString readTemplate(const QString &name);

    private:
        QString m_templatePath;
};

LIBMVME_EXPORT QJsonDocument gui_read_json(QIODevice *input);
LIBMVME_EXPORT QJsonDocument gui_read_json_file(const QString &fileName);

// Writes the JSON data to the output file. Error reporting is done using
// QMessageBoxes.
LIBMVME_EXPORT bool gui_write_json_file(const QString &fileName, const QJsonDocument &doc);

LIBMVME_EXPORT QPair<double, QString> byte_unit(size_t bytes);

//QString format_memory_size(size_t bytes);

LIBMVME_EXPORT void logBuffer(BufferIterator iter, std::function<void (const QString &)> loggerFun);
LIBMVME_EXPORT void logBuffer(const QVector<u32> &data, std::function<void (const QString &)> loggerFun);

static constexpr double make_quiet_nan()
{
    return std::numeric_limits<double>::quiet_NaN();
}

inline constexpr size_t Kilobytes(size_t x) { return x * 1024; }
inline constexpr size_t Megabytes(size_t x) { return Kilobytes(x) * 1024; }
inline constexpr size_t Gigabytes(size_t x) { return Megabytes(x) * 1024; }

#define InvalidCodePath Q_ASSERT(!"invalid code path")
#define InvalidDefaultCase default: { Q_ASSERT(!"invalid default case"); }

#endif // __MVME_UTIL_H_

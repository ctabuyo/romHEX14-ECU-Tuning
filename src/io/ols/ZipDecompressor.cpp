/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ZipDecompressor.h"

#include <zlib.h>
#include <QStringLiteral>

namespace ols {

QByteArray ZipDecompressor::decompress(const QByteArray &compressed, QString *err)
{
    return decompress(compressed, 0, err);
}

QByteArray ZipDecompressor::decompress(const QByteArray &compressed,
                                       qsizetype expectedSize,
                                       QString *err)
{
    if (compressed.isEmpty()) {
        if (err) *err = QStringLiteral("empty input");
        return {};
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    int ret = inflateInit2(&strm, -MAX_WBITS);
    if (ret != Z_OK) {
        if (err) *err = QStringLiteral("inflateInit2 failed: %1").arg(ret);
        return {};
    }

    // Bound decompression output to defend against zip/deflate bombs: a crafted
    // entry can declare a multi-GB uncompressed size, or expand a tiny payload
    // without limit, and exhaust memory. KP `intern` streams are ROM projects,
    // well under this ceiling.
    constexpr qsizetype kMaxDecompressed = 512 * 1024 * 1024; // 512 MiB
    if (expectedSize > kMaxDecompressed) {
        inflateEnd(&strm);
        if (err) *err = QStringLiteral("declared decompressed size %1 exceeds cap %2")
                            .arg(expectedSize).arg(kMaxDecompressed);
        return {};
    }
    qsizetype outSize = (expectedSize > 0) ? expectedSize
                                            : compressed.size() * 4;
    if (outSize < 1024) outSize = 1024;
    if (outSize > kMaxDecompressed) outSize = kMaxDecompressed;
    QByteArray output;
    output.resize(outSize);

    strm.next_in = reinterpret_cast<Bytef *>(
        const_cast<char *>(compressed.constData()));
    strm.avail_in = static_cast<uInt>(compressed.size());
    strm.next_out = reinterpret_cast<Bytef *>(output.data());
    strm.avail_out = static_cast<uInt>(output.size());

    while (true) {
        ret = ::inflate(&strm, Z_NO_FLUSH);

        if (ret == Z_STREAM_END)
            break;

        if (ret == Z_OK && strm.avail_out == 0) {
            qsizetype used = output.size() - strm.avail_out;
            qsizetype newSize = output.size() * 2;
            if (newSize > kMaxDecompressed) {
                inflateEnd(&strm);
                if (err) *err = QStringLiteral("decompressed output exceeds cap %1")
                                    .arg(kMaxDecompressed);
                return {};
            }
            output.resize(newSize);
            strm.next_out = reinterpret_cast<Bytef *>(output.data() + used);
            strm.avail_out = static_cast<uInt>(newSize - used);
            continue;
        }

        if (ret != Z_OK) {
            QString msg = QStringLiteral("inflate error %1").arg(ret);
            if (strm.msg)
                msg += QStringLiteral(": %1").arg(
                    QString::fromLatin1(strm.msg));
            inflateEnd(&strm);
            if (err) *err = msg;
            return {};
        }
    }

    qsizetype totalOut = static_cast<qsizetype>(strm.total_out);
    inflateEnd(&strm);
    output.resize(totalOut);
    return output;
}

}

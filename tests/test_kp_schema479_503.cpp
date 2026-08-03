/* Schema-245..503 cumulative codec corpus regression. SPDX-License-Identifier: GPL-3.0-or-later */

#include "io/ols/KpImporter.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QtEndian>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    int count = 0;
    QDirIterator iterator(QStringLiteral(KP_CORPUS_DIR), {QStringLiteral("*.kp")},
                          QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        QFile file(iterator.next());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = file.readAll();
        if (bytes.size() < 24 || bytes.mid(4, 12) != QByteArrayLiteral("WinOLS File\0"))
            continue;
        const uint32_t schema = qFromLittleEndian<uint32_t>(
            reinterpret_cast<const uchar *>(bytes.constData() + 16));
        if (schema != 245 && schema != 249 && schema != 252 && schema != 264
            && schema != 288 && schema != 290 && schema != 315 && schema != 330
            && schema != 356 && schema != 372
            && schema != 396 && schema != 397 && schema != 440 && schema != 479
            && schema != 503)
            continue;
        ++count;
        const auto result = ols::KpImporter::importFromBytes(bytes, 0, 4u * 1024u * 1024u);
        if (!result.error.isEmpty() || result.maps.isEmpty()) {
            QTextStream(stderr) << "FAIL: schema " << schema << " " << file.fileName()
                                << ": " << result.error << " / "
                                << result.warnings.join(QStringLiteral(" | ")) << '\n';
            ok = false;
        }
    }
    if (count != 28) {
        QTextStream(stderr) << "FAIL: expected twenty-eight schema-245..503 corpus files, got " << count << '\n';
        ok = false;
    }
    return ok ? 0 : 1;
}

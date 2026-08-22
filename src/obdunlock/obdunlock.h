#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace ObdUnlock {

enum class Kind {
    None = 0,
    Gen1,
    Gen2,
    Gen2Pre2020,
    Mevd172G,
};

struct Detection {
    Kind        kind         = Kind::None;
    bool        canPatch     = false;
    bool        alreadyPatched = false;

    QString describe() const;
};

Detection detect(const QByteArray &rom);

struct ApplyReport {
    bool        success = false;
    QStringList messages;
};
ApplyReport applyUnlock(QByteArray &rom, const Detection &d);

}

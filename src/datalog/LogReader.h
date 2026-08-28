#pragma once
#include "LogTable.h"
#include <QString>

namespace datalog {

class LogReader {
public:
    // Reads a datalog CSV/TSV file. On error returns an empty LogTable and
    // sets *err (when err != nullptr).
    //
    // Auto-detects the source format:
    //   - Vehical: 3-row header (name / description / unit), first column
    //     "Time" in milliseconds
    //   - Autotuner: single header row, "timestamp" first column, units
    //     embedded as "(unit)" in the column names
    //   - VCDS (Ross-Tech) group logs: per-group TIME/STAMP columns in
    //     seconds; groups are merged onto one time axis by interpolation
    // plus encoding (utf-8 with BOM / utf-8 / cp1252 fallback) and
    // delimiter (',' / '\t' / ';').
    //
    // Numeric parsing tolerates empty cells (treated as 0.0). The returned
    // table always has "Time" in milliseconds as column 0.
    static LogTable read(const QString &path, QString *err = nullptr);
};

} // namespace datalog

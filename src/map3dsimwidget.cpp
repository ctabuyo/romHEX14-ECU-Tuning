/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "map3dsimwidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <cmath>
#include <algorithm>

static constexpr double PI = 3.14159265358979323846;

Map3DSimWidget::Map3DSimWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // Bottom control bar
    auto *controlBar = new QWidget(this);
    controlBar->setObjectName("controlBar");
    controlBar->setFixedHeight(70);
    controlBar->setStyleSheet("QWidget#controlBar{background:rgba(5,8,16,0.92);"
                              "border-top:1px solid rgba(58,145,208,0.15)}");

    auto *cLay = new QVBoxLayout(controlBar);
    cLay->setContentsMargins(16, 4, 16, 4);
    cLay->setSpacing(2);

    auto *xRow = new QHBoxLayout();
    m_labelX = new QLabel(tr("RPM:"));
    m_labelX->setStyleSheet("color:#3a91d0;font-size:8pt;font-weight:bold");
    m_labelX->setFixedWidth(40);
    m_sliderX = new QSlider(Qt::Horizontal);
    m_sliderX->setRange(0, 1000);
    m_sliderX->setValue(500);
    m_sliderX->setStyleSheet(
        "QSlider::groove:horizontal{height:4px;background:#1c2230;border-radius:2px}"
        "QSlider::handle:horizontal{width:14px;margin:-5px 0;background:#3a91d0;"
        "border-radius:7px;border:2px solid #0c1222}"
        "QSlider::sub-page:horizontal{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #3a91d0,stop:1 #58a6ff);border-radius:2px}");
    xRow->addWidget(m_labelX);
    xRow->addWidget(m_sliderX, 1);
    cLay->addLayout(xRow);

    auto *yRow = new QHBoxLayout();
    m_labelY = new QLabel(tr("Load:"));
    m_labelY->setStyleSheet("color:#7c3aed;font-size:8pt;font-weight:bold");
    m_labelY->setFixedWidth(40);
    m_sliderY = new QSlider(Qt::Horizontal);
    m_sliderY->setRange(0, 1000);
    m_sliderY->setValue(500);
    m_sliderY->setStyleSheet(
        "QSlider::groove:horizontal{height:4px;background:#1c2230;border-radius:2px}"
        "QSlider::handle:horizontal{width:14px;margin:-5px 0;background:#7c3aed;"
        "border-radius:7px;border:2px solid #0c1222}"
        "QSlider::sub-page:horizontal{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #7c3aed,stop:1 #a78bfa);border-radius:2px}");
    yRow->addWidget(m_labelY);
    yRow->addWidget(m_sliderY, 1);
    cLay->addLayout(yRow);

    m_labelValue = new QLabel(QString::fromUtf8("\xe2\x80\x94")); // —
    m_labelValue->setAlignment(Qt::AlignCenter);
    m_labelValue->setStyleSheet(
        "color:#f0f6fc;font-size:11pt;font-weight:bold;"
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 rgba(58,145,208,0.15),"
        "stop:1 rgba(124,58,237,0.15));"
        "border:1px solid rgba(58,145,208,0.2);border-radius:6px;padding:2px 16px");
    cLay->addWidget(m_labelValue);

    connect(m_sliderX, &QSlider::valueChanged, this, [this](int v) {
        m_crossX = v / 1000.0; update(); updateValueLabel();
    });
    connect(m_sliderY, &QSlider::valueChanged, this, [this](int v) {
        m_crossY = v / 1000.0; update(); updateValueLabel();
    });
}

void Map3DSimWidget::updateValueLabel()
{
    if (!m_hasData || m_cols < 1 || m_rows < 1) return;
    int col = cursorCol();
    int row = cursorRow();
    double val = m_grid[row][col];
    QString xL = col < m_xAxis.size() ? QString::number(m_xAxis[col], 'f', 1) : QString::number(col);
    QString yL = row < m_yAxis.size() ? QString::number(m_yAxis[row], 'f', 1) : QString::number(row);

    // Shared status line: (row, col) · address · raw · scaled · unit (#22)
    int cs  = m_map.dataSize;
    int idx = m_map.columnMajor ? col * m_rows + row : row * m_cols + col;
    qint64 off = (qint64)m_map.address + m_map.mapDataOffset + (qint64)idx * cs;
    QString addr = QStringLiteral("$%1").arg(
        QString::number((quint64)off, 16).toUpper());

    quint32 raw = 0;
    if (off >= 0 && off + cs <= m_data.size()) {
        const uchar *d = reinterpret_cast<const uchar *>(m_data.constData());
        if (m_byteOrder == ByteOrder::BigEndian)
            for (int b = 0; b < cs; b++) raw = (raw << 8) | d[off + b];
        else
            for (int b = cs - 1; b >= 0; b--) raw = (raw << 8) | d[off + b];
    }

    QString zu = m_map.scaling.unit;
    m_labelValue->setText(
        QString("(%1, %2)  ·  %3  ·  raw %4  ·  %5%6   |   X %7  ·  Y %8")
            .arg(row).arg(col).arg(addr).arg(raw)
            .arg(QString::number(val, 'f', 2),
                 zu.isEmpty() ? QString() : QStringLiteral(" ") + zu,
                 xL, yL));
    emit cellHovered(row, col, val);
}

int Map3DSimWidget::cursorCol() const
{
    return qBound(0, (int)(m_crossX * (m_cols - 1) + 0.5), m_cols - 1);
}

int Map3DSimWidget::cursorRow() const
{
    return qBound(0, (int)(m_crossY * (m_rows - 1) + 0.5), m_rows - 1);
}

void Map3DSimWidget::setCursorNorm(double normX, double normY)
{
    if (!m_hasData || m_cols < 1 || m_rows < 1) return;
    m_crossX = qBound(0.0, normX, 1.0);
    m_crossY = qBound(0.0, normY, 1.0);
    if (m_sliderX) {
        QSignalBlocker bx(m_sliderX);
        m_sliderX->setValue((int)(m_crossX * 1000 + 0.5));
    }
    if (m_sliderY) {
        QSignalBlocker by(m_sliderY);
        m_sliderY->setValue((int)(m_crossY * 1000 + 0.5));
    }
    update();
    updateValueLabel();
}

void Map3DSimWidget::setCursorCell(int row, int col)
{
    if (!m_hasData || m_cols < 1 || m_rows < 1) return;
    row = qBound(0, row, m_rows - 1);
    col = qBound(0, col, m_cols - 1);
    setCursorNorm((m_cols > 1) ? (double)col / (m_cols - 1) : 0.5,
                  (m_rows > 1) ? (double)row / (m_rows - 1) : 0.5);
}

QPointF Map3DSimWidget::crosshairScreenPos() const
{
    double cx = -1.0 + 2.0 * m_crossX;
    double cy = -1.0 + 2.0 * m_crossY;
    Pt2 top = project(cx, cy, interpZ(m_crossX, m_crossY));
    return QPointF(top.x, top.y);
}

// Invert the bilinear patch P(u,v) = mix of the quad corners for screen
// point p — a few Newton steps converge for the near-parallelogram faces
// the projection produces.
static void invBilinear(const QPointF &p, const QPointF &a, const QPointF &b,
                        const QPointF &c, const QPointF &d, double *u, double *v)
{
    double uu = 0.5, vv = 0.5;
    for (int i = 0; i < 8; i++) {
        QPointF P = (1-uu)*(1-vv)*a + uu*(1-vv)*b + uu*vv*c + (1-uu)*vv*d;
        QPointF r = P - p;
        QPointF Pu = (1-vv)*(b-a) + vv*(c-d);
        QPointF Pv = (1-uu)*(d-a) + uu*(c-b);
        const double det = Pu.x()*Pv.y() - Pu.y()*Pv.x();
        if (qAbs(det) < 1e-9) break;
        uu -= ( r.x()*Pv.y() - r.y()*Pv.x()) / det;
        vv -= (-r.x()*Pu.y() + r.y()*Pu.x()) / det;
    }
    *u = qBound(0.0, uu, 1.0);
    *v = qBound(0.0, vv, 1.0);
}

const QVector<QPointF> &Map3DSimWidget::projectedVertices() const
{
    if (m_projDirty) {
        const double range = m_maxVal - m_minVal;
        m_projCache.resize(m_rows * m_cols);
        for (int r = 0; r < m_rows; r++) {
            for (int c = 0; c < m_cols; c++) {
                double x = -1.0 + 2.0 * c / (m_cols - 1);
                double y = -1.0 + 2.0 * r / (m_rows - 1);
                double z = (m_grid[r][c] - m_minVal) / range * kZScale;
                Pt2 pt = project(x, y, z);
                m_projCache[r * m_cols + c] = QPointF(pt.x, pt.y);
            }
        }
        m_projDirty = false;
    }
    return m_projCache;
}

// Map a screen position to a continuous (normX, normY) on the surface.
// Hit-tests the projected faces (front-most wins, same depth rule as the
// painter's back-to-front sort), then inverts the face's bilinear patch so
// the result tracks the mouse exactly — this is what makes dragging the
// crosshair dot feel glued to the pointer.
bool Map3DSimWidget::pickSurfacePos(const QPoint &pos, double *normX, double *normY) const
{
    if (!m_hasData || m_cols < 2 || m_rows < 2) return false;
    const QPointF fp(pos);
    const QVector<QPointF> &proj = projectedVertices();
    if (proj.size() != m_rows * m_cols) return false;

    double bestDepth = -1e18;
    bool foundFace = false;
    int fr = 0, fc = 0;
    for (int r = 0; r < m_rows - 1; r++) {
        for (int c = 0; c < m_cols - 1; c++) {
            const QPointF &p00 = proj[r * m_cols + c];
            const QPointF &p10 = proj[r * m_cols + c + 1];
            const QPointF &p11 = proj[(r+1) * m_cols + c + 1];
            const QPointF &p01 = proj[(r+1) * m_cols + c];
            // Cheap bbox reject before any allocation — this runs per mouse-move.
            if (fp.x() < qMin(qMin(p00.x(), p10.x()), qMin(p11.x(), p01.x()))
                || fp.x() > qMax(qMax(p00.x(), p10.x()), qMax(p11.x(), p01.x()))
                || fp.y() < qMin(qMin(p00.y(), p10.y()), qMin(p11.y(), p01.y()))
                || fp.y() > qMax(qMax(p00.y(), p10.y()), qMax(p11.y(), p01.y())))
                continue;
            QPolygonF quad;
            quad << p00 << p10 << p11 << p01;
            if (!quad.containsPoint(fp, Qt::OddEvenFill)) continue;
            const double depth = (p00.y() + p10.y() + p11.y() + p01.y()) / 4.0;
            if (depth > bestDepth) { bestDepth = depth; fr = r; fc = c; foundFace = true; }
        }
    }
    if (!foundFace) return false;

    double u = 0.5, v = 0.5;
    invBilinear(fp,
                proj[fr * m_cols + fc],       proj[fr * m_cols + fc + 1],
                proj[(fr+1) * m_cols + fc+1], proj[(fr+1) * m_cols + fc],
                &u, &v);
    *normX = (fc + u) / (m_cols - 1);
    *normY = (fr + v) / (m_rows - 1);
    return true;
}

// Cell-snapped variant of pickSurfacePos, with a vertex-radius fallback for
// clicks just off the surface edge.
bool Map3DSimWidget::pickSurfaceCell(const QPoint &pos, int &row, int &col) const
{
    if (!m_hasData || m_cols < 2 || m_rows < 2) return false;
    double nx = 0, ny = 0;
    if (pickSurfacePos(pos, &nx, &ny)) {
        col = qBound(0, (int)(nx * (m_cols - 1) + 0.5), m_cols - 1);
        row = qBound(0, (int)(ny * (m_rows - 1) + 0.5), m_rows - 1);
        return true;
    }

    const QPointF fp(pos);
    const QVector<QPointF> &proj = projectedVertices();
    constexpr double kPickRadius = 18.0;
    double best = kPickRadius * kPickRadius;
    bool found = false;
    for (int r = 0; r < m_rows; r++) {
        for (int c = 0; c < m_cols; c++) {
            const QPointF d = proj[r * m_cols + c] - fp;
            const double d2 = d.x() * d.x() + d.y() * d.y();
            if (d2 < best) { best = d2; row = r; col = c; found = true; }
        }
    }
    return found;
}

void Map3DSimWidget::selectFromMini(const QPoint &pos)
{
    if (m_miniRect.width() < 1 || m_miniRect.height() < 1) return;
    double tx = (pos.x() - m_miniRect.x()) / (double)m_miniRect.width();
    double ty = (pos.y() - m_miniRect.y()) / (double)m_miniRect.height();
    int col = qBound(0, (int)(tx * m_cols), m_cols - 1);
    int row = qBound(0, (int)(ty * m_rows), m_rows - 1);
    setCursorCell(row, col);
}

void Map3DSimWidget::showMap(const QByteArray &romData, const MapInfo &map, ByteOrder byteOrder)
{
    m_data = romData; m_map = map; m_byteOrder = byteOrder; m_hasData = true;
    rebuildGrid(); m_projDirty = true; update();
}

void Map3DSimWidget::clear() { m_hasData = false; m_grid.clear(); m_cols = m_rows = 0; update(); }

void Map3DSimWidget::rebuildGrid()
{
    if (!m_hasData) return;
    m_cols = m_map.dimensions.x; m_rows = m_map.dimensions.y;
    if (m_cols < 1 || m_rows < 1) return;

    const uchar *d = reinterpret_cast<const uchar *>(m_data.constData());
    int dataStart = m_map.address + m_map.mapDataOffset;
    int cs = m_map.dataSize;
    bool colMaj = m_map.columnMajor;
    bool isSigned = m_map.dataSigned;

    m_grid.resize(m_rows); m_minVal = 1e30; m_maxVal = -1e30;
    for (int r = 0; r < m_rows; r++) {
        m_grid[r].resize(m_cols);
        for (int c = 0; c < m_cols; c++) {
            int idx = colMaj ? c * m_rows + r : r * m_cols + c;
            int off = dataStart + idx * cs;
            if (off + cs > m_data.size()) { m_grid[r][c] = 0; continue; }
            uint32_t raw = 0;
            if (m_byteOrder == ByteOrder::BigEndian)
                for (int b = 0; b < cs; b++) raw = (raw << 8) | d[off + b];
            else
                for (int b = cs - 1; b >= 0; b--) raw = (raw << 8) | d[off + b];
            double val = raw;
            if (isSigned) {
                if (cs == 1 && raw > 127) val = (int8_t)raw;
                else if (cs == 2 && raw > 32767) val = (int16_t)raw;
                else if (cs == 4 && raw > 2147483647u) val = (int32_t)raw;
            }
            if (m_map.hasScaling) val = m_map.scaling.toPhysical(val);
            m_grid[r][c] = val;
            if (val < m_minVal) m_minVal = val;
            if (val > m_maxVal) m_maxVal = val;
        }
    }
    if (m_maxVal <= m_minVal) m_maxVal = m_minVal + 1;

    // Read axes
    m_xAxis.resize(m_cols); m_yAxis.resize(m_rows);
    auto readAxis = [&](int count, const AxisInfo &ai, QVector<double> &out) {
        for (int i = 0; i < count; i++) {
            if (ai.hasPtsAddress) {
                int off = ai.ptsAddress + i * ai.ptsDataSize;
                if (off + ai.ptsDataSize <= m_data.size()) {
                    uint32_t raw = 0;
                    if (m_byteOrder == ByteOrder::BigEndian)
                        for (int b = 0; b < ai.ptsDataSize; b++) raw = (raw << 8) | d[off + b];
                    else
                        for (int b = ai.ptsDataSize - 1; b >= 0; b--) raw = (raw << 8) | d[off + b];
                    double v = raw;
                    if (ai.hasScaling) v = ai.scaling.toPhysical(v);
                    out[i] = v;
                } else out[i] = i;
            } else out[i] = i;
        }
    };
    readAxis(m_cols, m_map.xAxis, m_xAxis);
    readAxis(m_rows, m_map.yAxis, m_yAxis);

    if (m_labelX) {
        QString u = m_map.xAxis.scaling.unit;
        m_labelX->setText(u.isEmpty() ? "X:" : u + ":");
    }
    if (m_labelY) {
        QString u = m_map.yAxis.scaling.unit;
        m_labelY->setText(u.isEmpty() ? "Y:" : u + ":");
    }
    updateValueLabel();
}

// ── Helpers ────────────────────────────────────────────────────────────────

double Map3DSimWidget::interpZ(double normX, double normY) const
{
    // normX, normY in [0..1] → bilinear interpolated grid value → normalized Z
    if (m_cols < 2 || m_rows < 2) return 0;
    double range = m_maxVal - m_minVal;
    double gx = normX * (m_cols - 1);
    double gy = normY * (m_rows - 1);
    int c0 = qBound(0, (int)gx, m_cols - 2);
    int r0 = qBound(0, (int)gy, m_rows - 2);
    double fx = gx - c0, fy = gy - r0;
    double v = m_grid[r0][c0] * (1-fx)*(1-fy) + m_grid[r0][c0+1] * fx*(1-fy)
             + m_grid[r0+1][c0] * (1-fx)*fy + m_grid[r0+1][c0+1] * fx*fy;
    return (v - m_minVal) / range * kZScale;
}

// ── Projection ─────────────────────────────────────────────────────────────

Map3DSimWidget::Pt2 Map3DSimWidget::project(double x, double y, double z) const
{
    double radZ = m_rotZ * PI / 180.0;
    double rx = x * cos(radZ) - y * sin(radZ);
    double ry = x * sin(radZ) + y * cos(radZ);
    double radX = m_rotX * PI / 180.0;
    double tz = ry * sin(radX) + z * cos(radX);
    double w = width(), h = height() - 70.0;
    double scale = qMin(w, h) * 0.30 * m_zoom;
    return {w / 2.0 + rx * scale + m_panX, h / 2.0 - tz * scale + m_panY};
}

QColor Map3DSimWidget::heatColor(double t) const
{
    t = qBound(0.0, t, 1.0);
    struct Stop { double pos; int r, g, b; };
    static const Stop stops[] = {
        {0.00, 10, 30, 120}, {0.15, 20, 80, 200}, {0.30, 30, 180, 200},
        {0.45, 40, 200, 100}, {0.60, 180, 220, 40}, {0.75, 240, 200, 20},
        {0.90, 250, 120, 10}, {1.00, 220, 40, 10}
    };
    for (int i = 0; i < 7; i++) {
        if (t <= stops[i + 1].pos) {
            double f = (t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
            return QColor(
                stops[i].r + (int)(f * (stops[i+1].r - stops[i].r)),
                stops[i].g + (int)(f * (stops[i+1].g - stops[i].g)),
                stops[i].b + (int)(f * (stops[i+1].b - stops[i].b)));
        }
    }
    return QColor(220, 40, 10);
}

// ── Paint ──────────────────────────────────────────────────────────────────

void Map3DSimWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background gradient
    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0, QColor(3, 5, 12));
    bg.setColorAt(0.5, QColor(8, 14, 28));
    bg.setColorAt(1, QColor(3, 5, 12));
    p.fillRect(rect(), bg);

    if (!m_hasData || m_cols < 2 || m_rows < 2) {
        p.setPen(QColor(80, 100, 140));
        p.setFont(QFont("Segoe UI", 12));
        p.drawText(rect().adjusted(0, 0, 0, -70), Qt::AlignCenter,
                   tr("Open a 2D MAP to view 3D simulation"));
        return;
    }

    // ── 1. Base platform ────────────────────────────────────────────────
    {
        double baseH = -0.08;
        Pt2 corners[8];
        double ex = 1.05;
        corners[0] = project(-ex, -ex, 0);
        corners[1] = project( ex, -ex, 0);
        corners[2] = project( ex,  ex, 0);
        corners[3] = project(-ex,  ex, 0);
        corners[4] = project(-ex, -ex, baseH);
        corners[5] = project( ex, -ex, baseH);
        corners[6] = project( ex,  ex, baseH);
        corners[7] = project(-ex,  ex, baseH);

        QPolygonF top;
        top << QPointF(corners[0].x, corners[0].y) << QPointF(corners[1].x, corners[1].y)
            << QPointF(corners[2].x, corners[2].y) << QPointF(corners[3].x, corners[3].y);
        p.setPen(QPen(QColor(30, 50, 80, 100), 0.5));
        p.setBrush(QColor(12, 20, 35, 200));
        p.drawPolygon(top);

        QPolygonF front;
        front << QPointF(corners[0].x, corners[0].y) << QPointF(corners[1].x, corners[1].y)
              << QPointF(corners[5].x, corners[5].y) << QPointF(corners[4].x, corners[4].y);
        p.setBrush(QColor(8, 14, 25, 220));
        p.drawPolygon(front);

        QPolygonF right;
        right << QPointF(corners[1].x, corners[1].y) << QPointF(corners[2].x, corners[2].y)
              << QPointF(corners[6].x, corners[6].y) << QPointF(corners[5].x, corners[5].y);
        p.setBrush(QColor(6, 10, 20, 220));
        p.drawPolygon(right);
    }

    // ── 2. Glass walls (back + side) with grid ──────────────────────────
    drawGlassWalls(p);

    // ── 3. Surface mesh ─────────────────────────────────────────────────
    drawSurface(p);

    // ── 4. Slice planes (semi-transparent) ──────────────────────────────
    drawSlicePlanes(p);

    // ── 5. Surface intersection lines (glowing) ─────────────────────────
    drawSurfaceIntersections(p);

    // ── 6. Slice curves ON the planes ───────────────────────────────────
    drawSliceCurves(p);

    // ── 7. Crosshair dot + vertical line ────────────────────────────────
    drawCrosshair(p);

    // ── 8. Axes labels ──────────────────────────────────────────────────
    drawAxes(p);

    // ── 9. Value badges ─────────────────────────────────────────────────
    drawValueLabel(p);

    // ── 10. Mini heatmap overview (top-left) ────────────────────────────
    drawMiniHeatmap(p);
}

// ── Glass walls ────────────────────────────────────────────────────────────

void Map3DSimWidget::drawGlassWalls(QPainter &p)
{
    double wallH = kZScale + 0.15;

    // Back wall (at y = 1.05)
    Pt2 bl = project(-1.05, 1.05, 0);
    Pt2 br = project( 1.05, 1.05, 0);
    Pt2 tl = project(-1.05, 1.05, wallH);
    Pt2 tr_ = project( 1.05, 1.05, wallH);
    QPolygonF backWall;
    backWall << QPointF(bl.x, bl.y) << QPointF(br.x, br.y)
             << QPointF(tr_.x, tr_.y) << QPointF(tl.x, tl.y);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(15, 25, 50, 50));
    p.drawPolygon(backWall);

    // Grid on back wall
    p.setPen(QPen(QColor(40, 60, 100, 35), 0.5));
    for (int i = 0; i <= 8; i++) {
        double x = -1.05 + 2.1 * i / 8.0;
        Pt2 a = project(x, 1.05, 0);
        Pt2 b = project(x, 1.05, wallH);
        p.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
    }
    for (int i = 0; i <= 6; i++) {
        double z = wallH * i / 6.0;
        Pt2 a = project(-1.05, 1.05, z);
        Pt2 b = project( 1.05, 1.05, z);
        p.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
    }

    // Side wall (at x = 1.05)
    Pt2 sl = project(1.05, -1.05, 0);
    Pt2 sr = project(1.05,  1.05, 0);
    Pt2 stl = project(1.05, -1.05, wallH);
    Pt2 str_ = project(1.05,  1.05, wallH);
    QPolygonF sideWall;
    sideWall << QPointF(sl.x, sl.y) << QPointF(sr.x, sr.y)
             << QPointF(str_.x, str_.y) << QPointF(stl.x, stl.y);
    p.setBrush(QColor(15, 25, 50, 40));
    p.drawPolygon(sideWall);

    // Grid on side wall
    p.setPen(QPen(QColor(40, 60, 100, 30), 0.5));
    for (int i = 0; i <= 8; i++) {
        double y = -1.05 + 2.1 * i / 8.0;
        Pt2 a = project(1.05, y, 0);
        Pt2 b = project(1.05, y, wallH);
        p.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
    }
    for (int i = 0; i <= 6; i++) {
        double z = wallH * i / 6.0;
        Pt2 a = project(1.05, -1.05, z);
        Pt2 b = project(1.05,  1.05, z);
        p.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
    }
}

// ── Surface ────────────────────────────────────────────────────────────────

void Map3DSimWidget::drawSurface(QPainter &p)
{
    double range = m_maxVal - m_minVal;

    struct Face { QPolygonF poly; QColor color; double depth = 0.0; };
    QVector<Face> faces;
    faces.reserve((m_rows - 1) * (m_cols - 1));

    for (int r = 0; r < m_rows - 1; r++) {
        for (int c = 0; c < m_cols - 1; c++) {
            double x0 = -1.0 + 2.0 * c / (m_cols - 1);
            double x1 = -1.0 + 2.0 * (c + 1) / (m_cols - 1);
            double y0 = -1.0 + 2.0 * r / (m_rows - 1);
            double y1 = -1.0 + 2.0 * (r + 1) / (m_rows - 1);
            double z00 = (m_grid[r][c] - m_minVal) / range * kZScale;
            double z10 = (m_grid[r][c+1] - m_minVal) / range * kZScale;
            double z01 = (m_grid[r+1][c] - m_minVal) / range * kZScale;
            double z11 = (m_grid[r+1][c+1] - m_minVal) / range * kZScale;

            Pt2 p00 = project(x0, y0, z00);
            Pt2 p10 = project(x1, y0, z10);
            Pt2 p01 = project(x0, y1, z01);
            Pt2 p11 = project(x1, y1, z11);

            QPolygonF poly;
            poly << QPointF(p00.x, p00.y) << QPointF(p10.x, p10.y)
                 << QPointF(p11.x, p11.y) << QPointF(p01.x, p01.y);

            double avgZ = (z00 + z10 + z01 + z11) / 4.0;
            double avgDepth = (p00.y + p10.y + p01.y + p11.y) / 4.0;

            QColor col = heatColor(avgZ / kZScale);
            // Strong depth shading — back faces darker, front brighter
            double depthT = avgDepth / (height() - 70.0);
            double shade = 0.40 + 0.60 * (1.0 - depthT);
            // Also darken low-Z faces slightly for more height contrast
            double heightShade = 0.85 + 0.15 * (avgZ / kZScale);
            shade *= heightShade;
            col = QColor(qBound(0, (int)(col.red() * shade), 255),
                         qBound(0, (int)(col.green() * shade), 255),
                         qBound(0, (int)(col.blue() * shade), 255), 225);

            faces.append({poly, col, avgDepth});
        }
    }

    std::sort(faces.begin(), faces.end(), [](const Face &a, const Face &b) {
        return a.depth < b.depth;
    });

    for (const auto &f : faces) {
        p.setPen(QPen(QColor(255, 255, 255, 18), 0.5));
        p.setBrush(f.color);
        p.drawPolygon(f.poly);
    }
}

// ── Slice planes (semi-transparent cutting planes) ─────────────────────────

void Map3DSimWidget::drawSlicePlanes(QPainter &p)
{
    if (m_cols < 2 || m_rows < 2) return;

    double cx = -1.0 + 2.0 * m_crossX; // world X of crosshair
    double cy = -1.0 + 2.0 * m_crossY; // world Y of crosshair
    double planeH = kZScale + 0.08;     // slightly above max surface

    // ── X-slice plane (vertical plane at crosshair Y, spans full X range)
    //    This is the "RPM slice" — blue/cyan tint
    {
        Pt2 bl = project(-1.0, cy, 0);
        Pt2 br = project( 1.0, cy, 0);
        Pt2 tl = project(-1.0, cy, planeH);
        Pt2 tr = project( 1.0, cy, planeH);
        QPolygonF plane;
        plane << QPointF(bl.x, bl.y) << QPointF(br.x, br.y)
              << QPointF(tr.x, tr.y) << QPointF(tl.x, tl.y);
        p.setPen(QPen(QColor(58, 145, 208, 60), 1.0));
        p.setBrush(QColor(58, 145, 208, 22));
        p.drawPolygon(plane);

        // Subtle grid on X-slice plane
        p.setPen(QPen(QColor(58, 145, 208, 20), 0.5));
        for (int i = 1; i < 8; i++) {
            double x = -1.0 + 2.0 * i / 8.0;
            Pt2 a = project(x, cy, 0);
            Pt2 b = project(x, cy, planeH);
            p.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
        }
        for (int i = 1; i < 5; i++) {
            double z = planeH * i / 5.0;
            Pt2 a = project(-1.0, cy, z);
            Pt2 b = project( 1.0, cy, z);
            p.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
        }
    }

    // ── Y-slice plane (vertical plane at crosshair X, spans full Y range)
    //    This is the "Load slice" — purple tint
    {
        Pt2 bl = project(cx, -1.0, 0);
        Pt2 br = project(cx,  1.0, 0);
        Pt2 tl = project(cx, -1.0, planeH);
        Pt2 tr = project(cx,  1.0, planeH);
        QPolygonF plane;
        plane << QPointF(bl.x, bl.y) << QPointF(br.x, br.y)
              << QPointF(tr.x, tr.y) << QPointF(tl.x, tl.y);
        p.setPen(QPen(QColor(124, 58, 237, 60), 1.0));
        p.setBrush(QColor(124, 58, 237, 22));
        p.drawPolygon(plane);

        // Subtle grid on Y-slice plane
        p.setPen(QPen(QColor(124, 58, 237, 20), 0.5));
        for (int i = 1; i < 8; i++) {
            double y = -1.0 + 2.0 * i / 8.0;
            Pt2 a = project(cx, y, 0);
            Pt2 b = project(cx, y, planeH);
            p.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
        }
        for (int i = 1; i < 5; i++) {
            double z = planeH * i / 5.0;
            Pt2 a = project(cx, -1.0, z);
            Pt2 b = project(cx,  1.0, z);
            p.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
        }
    }
}

// ── Surface intersection lines (glowing where planes cut the terrain) ──────

void Map3DSimWidget::drawSurfaceIntersections(QPainter &p)
{
    if (m_cols < 2 || m_rows < 2) return;
    double range = m_maxVal - m_minVal;
    int steps = qMax(m_cols, m_rows) * 3; // high resolution

    // ── X-intersection: slice at crosshair Y, trace across X ───────────
    {
        // Glow pass (wide, dim)
        QPainterPath path;
        bool first = true;
        for (int i = 0; i <= steps; i++) {
            double t = (double)i / steps;
            double x = -1.0 + 2.0 * t;
            double cy = -1.0 + 2.0 * m_crossY;
            double z = interpZ(t, m_crossY);
            Pt2 pt = project(x, cy, z);
            if (first) { path.moveTo(pt.x, pt.y); first = false; }
            else path.lineTo(pt.x, pt.y);
        }
        // Outer glow
        p.setPen(QPen(QColor(58, 200, 255, 50), 8.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        // Mid glow
        p.setPen(QPen(QColor(58, 200, 255, 100), 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        // Core line
        p.setPen(QPen(QColor(120, 220, 255, 220), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }

    // ── Y-intersection: slice at crosshair X, trace across Y ───────────
    {
        QPainterPath path;
        bool first = true;
        for (int i = 0; i <= steps; i++) {
            double t = (double)i / steps;
            double y = -1.0 + 2.0 * t;
            double cx = -1.0 + 2.0 * m_crossX;
            double z = interpZ(m_crossX, t);
            Pt2 pt = project(cx, y, z);
            if (first) { path.moveTo(pt.x, pt.y); first = false; }
            else path.lineTo(pt.x, pt.y);
        }
        // Outer glow
        p.setPen(QPen(QColor(180, 100, 255, 50), 8.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        // Mid glow
        p.setPen(QPen(QColor(180, 100, 255, 100), 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        // Core line
        p.setPen(QPen(QColor(200, 160, 255, 220), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }
}

// ── Slice curves ON the planes ─────────────────────────────────────────────

void Map3DSimWidget::drawSliceCurves(QPainter &p)
{
    if (m_cols < 2 || m_rows < 2) return;
    double range = m_maxVal - m_minVal;
    int steps = 80;

    double cx = -1.0 + 2.0 * m_crossX;
    double cy = -1.0 + 2.0 * m_crossY;

    // ── X-slice curve: on the X-slice plane (at crosshair Y) ───────────
    //    Curve follows the terrain profile, drawn ON the plane, with fill
    //    down to base. This makes the curve feel "carved" from the plane.
    {
        QPainterPath curvePath;
        QPainterPath fillPath;
        bool first = true;
        for (int i = 0; i <= steps; i++) {
            double t = (double)i / steps;
            double x = -1.0 + 2.0 * t;
            double z = interpZ(t, m_crossY);
            Pt2 pt = project(x, cy, z);
            if (first) { curvePath.moveTo(pt.x, pt.y); fillPath.moveTo(pt.x, pt.y); first = false; }
            else { curvePath.lineTo(pt.x, pt.y); fillPath.lineTo(pt.x, pt.y); }
        }
        // Close fill down to base
        Pt2 endBase = project(1.0, cy, 0);
        Pt2 startBase = project(-1.0, cy, 0);
        fillPath.lineTo(endBase.x, endBase.y);
        fillPath.lineTo(startBase.x, startBase.y);
        fillPath.closeSubpath();

        // Filled area (connects curve to surface — no longer floating)
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(58, 180, 255, 35));
        p.drawPath(fillPath);

        // Curve line
        p.setPen(QPen(QColor(80, 200, 255, 220), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(curvePath);
    }

    // ── Y-slice curve: on the Y-slice plane (at crosshair X) ───────────
    {
        QPainterPath curvePath;
        QPainterPath fillPath;
        bool first = true;
        for (int i = 0; i <= steps; i++) {
            double t = (double)i / steps;
            double y = -1.0 + 2.0 * t;
            double z = interpZ(m_crossX, t);
            Pt2 pt = project(cx, y, z);
            if (first) { curvePath.moveTo(pt.x, pt.y); fillPath.moveTo(pt.x, pt.y); first = false; }
            else { curvePath.lineTo(pt.x, pt.y); fillPath.lineTo(pt.x, pt.y); }
        }
        Pt2 endBase = project(cx, 1.0, 0);
        Pt2 startBase = project(cx, -1.0, 0);
        fillPath.lineTo(endBase.x, endBase.y);
        fillPath.lineTo(startBase.x, startBase.y);
        fillPath.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(160, 80, 255, 35));
        p.drawPath(fillPath);

        p.setPen(QPen(QColor(180, 140, 255, 220), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(curvePath);
    }
}

// ── Crosshair ──────────────────────────────────────────────────────────────

void Map3DSimWidget::drawCrosshair(QPainter &p)
{
    if (m_cols < 2 || m_rows < 2) return;

    double cx = -1.0 + 2.0 * m_crossX;
    double cy = -1.0 + 2.0 * m_crossY;
    double zNorm = interpZ(m_crossX, m_crossY);

    // Vertical dashed line from base to surface
    Pt2 base = project(cx, cy, 0);
    Pt2 top = project(cx, cy, zNorm);
    p.setPen(QPen(QColor(255, 220, 50, 140), 1.5, Qt::DashLine));
    p.drawLine(QPointF(base.x, base.y), QPointF(top.x, top.y));

    // Crosshair dot (yellow ring with white center)
    // Outer glow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 220, 50, 60));
    p.drawEllipse(QPointF(top.x, top.y), 12, 12);
    // Ring
    p.setBrush(QColor(255, 220, 50, 200));
    p.drawEllipse(QPointF(top.x, top.y), 7, 7);
    // Center
    p.setBrush(QColor(255, 255, 255));
    p.drawEllipse(QPointF(top.x, top.y), 3, 3);

    // Shadow dot on base
    p.setBrush(QColor(255, 220, 50, 50));
    p.drawEllipse(QPointF(base.x, base.y), 5, 5);

    // Transient hover marker on the surface (white ring, no fill)
    if (m_hoverRow >= 0 && m_hoverCol >= 0) {
        double range = m_maxVal - m_minVal;
        double hx = -1.0 + 2.0 * m_hoverCol / (m_cols - 1);
        double hy = -1.0 + 2.0 * m_hoverRow / (m_rows - 1);
        double hz = (m_grid[m_hoverRow][m_hoverCol] - m_minVal) / range * kZScale;
        Pt2 pt = project(hx, hy, hz);
        p.setPen(QPen(QColor(255, 255, 255, 180), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(pt.x, pt.y), 6, 6);
    }
}

// ── Axes ───────────────────────────────────────────────────────────────────

void Map3DSimWidget::drawAxes(QPainter &p)
{
    QFont tickFont("Consolas", 7);
    p.setFont(tickFont);

    // X-axis tick labels along front edge
    p.setPen(QColor(58, 145, 208, 200));
    for (int c = 0; c < m_cols; c += qMax(1, m_cols / 8)) {
        double x = -1.0 + 2.0 * c / (m_cols - 1);
        Pt2 pt = project(x, -1.1, -0.08);
        QString label = c < m_xAxis.size() ? QString::number(m_xAxis[c], 'g', 5) : QString::number(c);
        p.drawText(QPointF(pt.x - 15, pt.y + 12), label);
    }

    // Y-axis tick labels along left edge
    p.setPen(QColor(124, 58, 237, 200));
    for (int r = 0; r < m_rows; r += qMax(1, m_rows / 8)) {
        double y = -1.0 + 2.0 * r / (m_rows - 1);
        Pt2 pt = project(-1.12, y, -0.08);
        QString label = r < m_yAxis.size() ? QString::number(m_yAxis[r], 'g', 5) : QString::number(r);
        p.drawText(QPointF(pt.x - 25, pt.y + 4), label);
    }

    // Z-axis tick labels
    p.setPen(QColor(34, 197, 94, 180));
    for (int i = 0; i <= 4; i++) {
        double t = i / 4.0;
        double z = t * kZScale;
        double val = m_minVal + t * (m_maxVal - m_minVal);
        Pt2 pt = project(-1.14, 1.08, z);
        p.drawText(QPointF(pt.x - 35, pt.y + 4), QString::number(val, 'g', 5));
        p.setPen(QPen(QColor(34, 197, 94, 50), 0.5));
        Pt2 t1 = project(-1.08, 1.05, z);
        Pt2 t2 = project(-1.05, 1.05, z);
        p.drawLine(QPointF(t1.x, t1.y), QPointF(t2.x, t2.y));
        p.setPen(QColor(34, 197, 94, 180));
    }

    // ── Big axis name badges ────────────────────────────────────────────
    auto drawAxisBadge = [&](Pt2 pos, const QString &icon, const QString &name,
                             const QString &unit, QColor bgColor, QColor textColor) {
        QString label = icon + " " + (unit.isEmpty() ? name : unit);
        QFont badgeFont("Segoe UI", 11, QFont::Bold);
        p.setFont(badgeFont);
        QFontMetrics fm(badgeFont);
        int tw = fm.horizontalAdvance(label) + 20;
        int th = fm.height() + 10;
        QRectF bg(pos.x - tw / 2, pos.y - th / 2, tw, th);
        p.setPen(QPen(bgColor.lighter(120), 1.5));
        QColor fill = bgColor; fill.setAlpha(180);
        p.setBrush(fill);
        p.drawRoundedRect(bg, 6, 6);
        p.setPen(textColor);
        p.drawText(bg, Qt::AlignCenter, label);
    };

    // X-axis badge
    {
        Pt2 pos = project(0, -1.35, -0.12);
        QString xu = m_map.xAxis.scaling.unit;
        QString icon = "\xE2\x86\x94"; // horizontal arrow
        if (xu.contains("rpm", Qt::CaseInsensitive) || xu.contains("1/min", Qt::CaseInsensitive))
            icon = "\xF0\x9F\x94\xA7";
        else if (xu.contains("km", Qt::CaseInsensitive) || xu.contains("mph", Qt::CaseInsensitive))
            icon = "\xF0\x9F\x9A\x97";
        else if (xu.contains("deg", Qt::CaseInsensitive) || xu.contains("\xC2\xB0", Qt::CaseInsensitive))
            icon = "\xF0\x9F\x8C\xA1";
        drawAxisBadge(pos, icon, "X", xu, QColor(20, 50, 90), QColor(100, 180, 255));
    }

    // Y-axis badge
    {
        Pt2 pos = project(-1.4, 0, -0.12);
        QString yu = m_map.yAxis.scaling.unit;
        QString icon = "\xE2\x86\x95"; // vertical arrow
        if (yu.contains("%", Qt::CaseInsensitive) || yu.contains("load", Qt::CaseInsensitive))
            icon = "\xE2\x9A\xA1";
        else if (yu.contains("bar", Qt::CaseInsensitive) || yu.contains("kpa", Qt::CaseInsensitive)
                 || yu.contains("hPa", Qt::CaseInsensitive))
            icon = "\xF0\x9F\x92\xA8";
        else if (yu.contains("rpm", Qt::CaseInsensitive) || yu.contains("1/min", Qt::CaseInsensitive))
            icon = "\xF0\x9F\x94\xA7";
        drawAxisBadge(pos, icon, "Y", yu, QColor(50, 20, 80), QColor(180, 140, 255));
    }

    // Z-axis badge
    {
        Pt2 pos = project(-1.3, 1.15, kZScale + 0.08);
        QString zu = m_map.scaling.unit;
        QString icon = "\xE2\xAC\x86"; // up arrow
        if (zu.contains("Nm", Qt::CaseInsensitive) || zu.contains("torque", Qt::CaseInsensitive))
            icon = "\xF0\x9F\x92\xAA";
        else if (zu.contains("deg", Qt::CaseInsensitive) || zu.contains("\xC2\xB0", Qt::CaseInsensitive))
            icon = "\xF0\x9F\x94\xA5";
        else if (zu.contains("%", Qt::CaseInsensitive))
            icon = "\xE2\x9A\xA1";
        else if (zu.contains("mg", Qt::CaseInsensitive))
            icon = "\xE2\x9B\xBD";
        drawAxisBadge(pos, icon, "Z", zu, QColor(15, 60, 35), QColor(80, 220, 140));
    }
}

// ── Value label (small badge on crosshair dot only) ────────────────────────

void Map3DSimWidget::drawValueLabel(QPainter &p)
{
    if (m_cols < 2 || m_rows < 2) return;

    int col = qBound(0, (int)(m_crossX * (m_cols - 1) + 0.5), m_cols - 1);
    int row = qBound(0, (int)(m_crossY * (m_rows - 1) + 0.5), m_rows - 1);
    double val = m_grid[row][col];
    double cx = -1.0 + 2.0 * m_crossX;
    double cy = -1.0 + 2.0 * m_crossY;
    double zNorm = interpZ(m_crossX, m_crossY);
    Pt2 top = project(cx, cy, zNorm);

    // Small value badge above the crosshair dot
    QString zu = m_map.scaling.unit;
    QString text = QString::number(val, 'f', 1);
    if (!zu.isEmpty()) text += " " + zu;
    QFont f("Consolas", 9, QFont::Bold);
    p.setFont(f);
    QFontMetrics fm(f);
    int tw = fm.horizontalAdvance(text) + 12;
    int th = fm.height() + 6;
    QRectF bg(top.x - tw / 2, top.y - th - 14, tw, th);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(12, 18, 34, 230));
    p.drawRoundedRect(bg, 4, 4);
    p.setPen(QPen(QColor(255, 220, 50), 1));
    p.drawRoundedRect(bg.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.drawText(bg, Qt::AlignCenter, text);
}

// ── Mini heatmap with values and units ─────────────────────────────────────

void Map3DSimWidget::drawMiniHeatmap(QPainter &p)
{
    if (m_cols < 2 || m_rows < 2) return;

    double range = m_maxVal - m_minVal;
    int margin = 10;
    int maxSide = 120;

    // Scale to fit while preserving aspect ratio
    double aspect = (double)m_cols / m_rows;
    int mapW, mapH;
    if (aspect >= 1.0) {
        mapW = maxSide;
        mapH = qMax(24, (int)(maxSide / aspect));
    } else {
        mapH = maxSide;
        mapW = qMax(24, (int)(maxSide * aspect));
    }

    // Layout: unit labels sit outside the heatmap
    int labelH = 16;  // height for X unit label below
    int labelW = 14;  // width for Y unit label left
    int valH   = 48;  // height for value readout below labels

    int panelW = labelW + mapW + 10;
    int panelH = mapH + labelH + valH + 10;

    int px = margin;           // panel x
    int py = margin;           // panel y
    int hx = px + labelW + 2;  // heatmap x
    int hy = py + 4;           // heatmap y

    // Panel background
    p.setPen(QPen(QColor(60, 80, 120, 80), 1));
    p.setBrush(QColor(6, 10, 20, 210));
    p.drawRoundedRect(px - 2, py - 2, panelW + 4, panelH + 4, 6, 6);

    // Draw heatmap cells
    double cellW = (double)mapW / m_cols;
    double cellH = (double)mapH / m_rows;

    for (int r = 0; r < m_rows; r++) {
        for (int c = 0; c < m_cols; c++) {
            double t = (m_grid[r][c] - m_minVal) / range;
            QColor col = heatColor(t);
            int cx = hx + (int)(c * cellW);
            int cy = hy + (int)(r * cellH);
            int cw = (int)((c + 1) * cellW) - (int)(c * cellW);
            int ch = (int)((r + 1) * cellH) - (int)(r * cellH);
            p.fillRect(cx, cy, qMax(cw, 1), qMax(ch, 1), col);
        }
    }

    // Thin border around heatmap
    p.setPen(QPen(QColor(80, 100, 140, 80), 0.5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(hx, hy, mapW, mapH);

    // Remember the heatmap area for mouse hit-testing (cursor sync, #22)
    m_miniRect = QRect(hx, hy, mapW, mapH);

    // Selected-cell outline
    {
        int col = qBound(0, (int)(m_crossX * (m_cols - 1) + 0.5), m_cols - 1);
        int row = qBound(0, (int)(m_crossY * (m_rows - 1) + 0.5), m_rows - 1);
        QRectF cell(hx + col * cellW, hy + row * cellH,
                    qMax(cellW, 2.0), qMax(cellH, 2.0));
        p.setPen(QPen(QColor(255, 220, 50, 230), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(cell);
    }

    // Hover-cell outline (transient)
    if (m_hoverRow >= 0 && m_hoverCol >= 0
        && (m_hoverRow != qBound(0, (int)(m_crossY * (m_rows - 1) + 0.5), m_rows - 1)
            || m_hoverCol != qBound(0, (int)(m_crossX * (m_cols - 1) + 0.5), m_cols - 1))) {
        QRectF cell(hx + m_hoverCol * cellW, hy + m_hoverRow * cellH,
                    qMax(cellW, 2.0), qMax(cellH, 2.0));
        p.setPen(QPen(QColor(255, 255, 255, 150), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(cell);
    }

    // Crosshair lines on minimap
    double dotX = hx + m_crossX * mapW;
    double dotY = hy + m_crossY * mapH;

    p.setPen(QPen(QColor(255, 255, 255, 100), 0.5));
    p.drawLine(QPointF(dotX, hy), QPointF(dotX, hy + mapH));
    p.drawLine(QPointF(hx, dotY), QPointF(hx + mapW, dotY));

    // Position dot
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 220, 50, 140));
    p.drawEllipse(QPointF(dotX, dotY), 5, 5);
    p.setBrush(QColor(255, 255, 255));
    p.drawEllipse(QPointF(dotX, dotY), 2, 2);

    // ── X unit label (bottom of heatmap, centered) ─────────────────────
    {
        QString xu = m_map.xAxis.scaling.unit;
        if (xu.isEmpty()) xu = "X";
        QFont f("Segoe UI", 7);
        p.setFont(f);
        p.setPen(QColor(90, 160, 220, 200));
        // Arrow hints at ends
        int textY = hy + mapH + 2;
        p.drawText(QRect(hx, textY, mapW, labelH), Qt::AlignCenter, xu);
        // Small arrows
        p.drawText(QRect(hx - 2, textY, 12, labelH), Qt::AlignCenter,
                   QString::fromUtf8("\xe2\x86\x90")); // ←
        p.drawText(QRect(hx + mapW - 10, textY, 12, labelH), Qt::AlignCenter,
                   QString::fromUtf8("\xe2\x86\x92")); // →
    }

    // ── Y unit label (left of heatmap, vertical text) ──────────────────
    {
        QString yu = m_map.yAxis.scaling.unit;
        if (yu.isEmpty()) yu = "Y";
        QFont f("Segoe UI", 7);
        p.setFont(f);
        p.setPen(QColor(160, 120, 230, 200));
        p.save();
        p.translate(px + 6, hy + mapH / 2);
        p.rotate(-90);
        QFontMetrics fm(f);
        int tw = fm.horizontalAdvance(yu);
        p.drawText(-tw / 2, 4, yu);
        p.restore();
    }

    // ── Value readout (below heatmap + unit labels) ────────────────────
    {
        int col = qBound(0, (int)(m_crossX * (m_cols - 1) + 0.5), m_cols - 1);
        int row = qBound(0, (int)(m_crossY * (m_rows - 1) + 0.5), m_rows - 1);
        double val = m_grid[row][col];

        QString xu = m_map.xAxis.scaling.unit;
        QString yu = m_map.yAxis.scaling.unit;
        QString zu = m_map.scaling.unit;

        QString xVal = col < m_xAxis.size() ? QString::number(m_xAxis[col], 'f', 1) : QString::number(col);
        QString yVal = row < m_yAxis.size() ? QString::number(m_yAxis[row], 'f', 1) : QString::number(row);
        QString zVal = QString::number(val, 'f', 1);

        int ry = hy + mapH + labelH + 4; // readout Y
        QFont fLabel("Segoe UI", 7);
        QFont fValue("Consolas", 8, QFont::Bold);

        // Three rows: X, Y, Z — each with colored label and value
        auto drawRow = [&](int y, const QString &label, const QString &value,
                           const QString &unit, QColor labelCol) {
            p.setFont(fLabel);
            p.setPen(labelCol);
            p.drawText(hx, y, 20, 13, Qt::AlignLeft | Qt::AlignVCenter, label);
            p.setFont(fValue);
            p.setPen(QColor(230, 235, 245));
            QString txt = value;
            if (!unit.isEmpty()) txt += " " + unit;
            p.drawText(hx + 18, y, mapW - 18, 13, Qt::AlignLeft | Qt::AlignVCenter, txt);
        };

        drawRow(ry,      "X:", xVal, xu, QColor(80, 170, 240));
        drawRow(ry + 13, "Y:", yVal, yu, QColor(160, 120, 240));
        drawRow(ry + 26, "Z:", zVal, zu, QColor(255, 220, 50));
    }
}

// ── Mouse ──────────────────────────────────────────────────────────────────

void Map3DSimWidget::mousePressEvent(QMouseEvent *e)
{
    m_lastMouse = e->pos();
    m_pressPos  = e->pos();
    if (e->button() == Qt::LeftButton) {
        // Click/drag on the mini heatmap selects cells, never rotates.
        if (m_hasData && m_miniRect.contains(e->pos())) {
            m_miniDragging = true;
            selectFromMini(e->pos());
            return;
        }
        // Grab the yellow crosshair dot: dragging it slides the selection
        // along the surface instead of rotating the view.
        if (m_hasData && m_cols >= 2 && m_rows >= 2) {
            const QPointF d = QPointF(e->pos()) - crosshairScreenPos();
            if (d.x() * d.x() + d.y() * d.y() <= 14.0 * 14.0) {
                m_crossDragging = true;
                setCursor(Qt::ClosedHandCursor);
                return;
            }
        }
        m_rotating   = true;
        m_maybeClick = true;   // becomes a cell-pick if released without dragging
        setCursor(Qt::ClosedHandCursor);
    }
}

void Map3DSimWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_miniDragging) {
        selectFromMini(e->pos());
        return;
    }
    if (m_crossDragging) {
        double nx = 0, ny = 0;
        if (pickSurfacePos(e->pos(), &nx, &ny))
            setCursorNorm(nx, ny);   // off-surface moves keep the last position
        return;
    }
    if (m_rotating) {
        if ((e->pos() - m_pressPos).manhattanLength() > 4)
            m_maybeClick = false;
        QPoint delta = e->pos() - m_lastMouse;
        m_rotZ += delta.x() * 0.4;
        m_rotX += delta.y() * 0.3;
        m_rotX = qBound(-85.0, m_rotX, -5.0);
        m_lastMouse = e->pos();
        m_projDirty = true;
        update();
        return;
    }

    // Hover (no buttons). Only minimap hover shows the transient highlight —
    // it implies a repaint of the whole scene, which is fine for a deliberate
    // visit to the minimap but would cause a repaint storm (and janky drags)
    // if it fired while sweeping across the 3D surface. Surface hover just
    // updates the cursor, using the cached projection.
    if (m_hasData && m_cols >= 2 && m_rows >= 2) {
        if (m_miniRect.contains(e->pos())) {
            double tx = (e->pos().x() - m_miniRect.x()) / (double)m_miniRect.width();
            double ty = (e->pos().y() - m_miniRect.y()) / (double)m_miniRect.height();
            int hc = qBound(0, (int)(tx * m_cols), m_cols - 1);
            int hr = qBound(0, (int)(ty * m_rows), m_rows - 1);
            setCursor(Qt::PointingHandCursor);
            if (hr != m_hoverRow || hc != m_hoverCol) {
                m_hoverRow = hr;
                m_hoverCol = hc;
                update();
            }
        } else {
            const QPointF d = QPointF(e->pos()) - crosshairScreenPos();
            if (d.x() * d.x() + d.y() * d.y() <= 14.0 * 14.0) {
                setCursor(Qt::OpenHandCursor);   // the dot is grabbable
            } else {
                int hr = -1, hc = -1;
                pickSurfaceCell(e->pos(), hr, hc);
                setCursor(hr >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            }
            if (m_hoverRow != -1 || m_hoverCol != -1) {
                m_hoverRow = m_hoverCol = -1;   // leaving the minimap clears its highlight
                update();
            }
        }
    }
}

void Map3DSimWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_miniDragging) {
        m_miniDragging = false;
        return;
    }
    if (m_crossDragging) {
        m_crossDragging = false;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (m_rotating) {
        m_rotating = false;
        setCursor(Qt::ArrowCursor);
        // A press that never moved: pick the cell under the click on the
        // 3D surface and sync both panes to it (#22).
        if (m_maybeClick
            && (e->pos() - m_pressPos).manhattanLength() <= 4) {
            int row = 0, col = 0;
            if (pickSurfaceCell(e->pos(), row, col))
                setCursorCell(row, col);
        }
        m_maybeClick = false;
    }
}

void Map3DSimWidget::keyPressEvent(QKeyEvent *e)
{
    if (!m_hasData || m_cols < 1 || m_rows < 1) {
        QWidget::keyPressEvent(e);
        return;
    }
    int row = cursorRow(), col = cursorCol();
    switch (e->key()) {
    case Qt::Key_Left:  setCursorCell(row, col - 1); break;
    case Qt::Key_Right: setCursorCell(row, col + 1); break;
    case Qt::Key_Up:    setCursorCell(row - 1, col); break;
    case Qt::Key_Down:  setCursorCell(row + 1, col); break;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
    e->accept();
}

void Map3DSimWidget::wheelEvent(QWheelEvent *e)
{
    double delta = e->angleDelta().y() / 120.0;
    m_zoom *= (1.0 + delta * 0.1);
    m_zoom = qBound(0.3, m_zoom, 5.0);
    m_projDirty = true;
    update();
}

void Map3DSimWidget::resizeEvent(QResizeEvent *)
{
    auto *cb = findChild<QWidget *>("controlBar");
    if (cb) cb->setGeometry(0, height() - 70, width(), 70);
    m_projDirty = true;
}

void Map3DSimWidget::leaveEvent(QEvent *)
{
    if (m_hoverRow >= 0 || m_hoverCol >= 0) {
        m_hoverRow = m_hoverCol = -1;
        update();
    }
}

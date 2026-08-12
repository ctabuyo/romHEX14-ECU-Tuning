/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mappropertiesdlg.h"
#include "appconstants.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFrame>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFont>
#include <QSettings>
#include <QCloseEvent>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static const QString kStyle =
    "QDialog          { background:#1c2128; color:#e6edf3; }"
    "QTabWidget::pane { border:1px solid #30363d; background:#1c2128; }"
    "QTabBar::tab     { background:#161b22; color:#8b949e; padding:5px 14px; "
    "                   border:1px solid #30363d; border-bottom:none; }"
    "QTabBar::tab:selected { background:#1c2128; color:#e6edf3; }"
    "QLineEdit, QPlainTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {"
    "  background:#0d1117; color:#e6edf3; border:1px solid #30363d; "
    "  border-radius:4px; padding:2px 4px; }"
    "QComboBox::drop-down { border:none; }"
    "QLabel  { color:#8b949e; }"
    "QLabel[class='id'] { color:#58a6ff; font-family:Consolas; }"
    "QGroupBox { border:1px solid #30363d; border-radius:4px; "
    "            margin-top:8px; color:#8b949e; }"
    "QGroupBox::title { subcontrol-origin:margin; left:8px; padding:0 4px; }"
    "QCheckBox { color:#e6edf3; spacing:6px; }"
    "QCheckBox::indicator { width:14px; height:14px; border:1px solid #30363d; "
    "  border-radius:3px; background:#0d1117; }"
    "QCheckBox::indicator:checked { background:#388bfd; border-color:#388bfd; }"
    "QPushButton { background:#21262d; color:#e6edf3; border:1px solid #30363d; "
    "              border-radius:4px; padding:4px 12px; }"
    "QPushButton:hover { background:#2d333b; }"
    "QPushButton:pressed { background:#161b22; }"
    "QPushButton[default='true'] { border-color:#388bfd; }";

static QComboBox *dataOrgCombo()
{
    auto *c = new QComboBox();
    c->addItem("8 Bit",              8);
    c->addItem("16 Bit (LoHi)",     16);
    c->addItem("16 Bit (HiLo)",    -16);
    c->addItem("32 Bit (LoHi)",     32);
    c->addItem("32 Bit (HiLo)",    -32);
    return c;
}

static void setDataOrg(QComboBox *c, int dataSize, ByteOrder bo)
{
    int bits = dataSize * 8;
    int code = (bo == ByteOrder::LittleEndian) ? bits : -bits;
    if (dataSize == 1) code = 8;
    for (int i = 0; i < c->count(); ++i)
        if (c->itemData(i).toInt() == code) { c->setCurrentIndex(i); return; }
    c->setCurrentIndex(1); // fallback 16-bit LoHi
}

static QPair<int,ByteOrder> fromDataOrg(QComboBox *c)
{
    int code = c->currentData().toInt();
    if (code ==  8)  return {1, ByteOrder::LittleEndian};
    if (code ==  16) return {2, ByteOrder::LittleEndian};
    if (code == -16) return {2, ByteOrder::BigEndian};
    if (code ==  32) return {4, ByteOrder::LittleEndian};
    if (code == -32) return {4, ByteOrder::BigEndian};
    return {2, ByteOrder::LittleEndian};
}

QWidget *MapPropertiesDialog::makeSeparator()
{
    auto *f = new QFrame();
    f->setFrameShape(QFrame::HLine);
    f->setStyleSheet("color:#30363d;");
    return f;
}

int MapPropertiesDialog::precisionFromFormat(const CompuMethod &cm) const
{
    if (!cm.format.isEmpty()) {
        int dot = cm.format.indexOf('.');
        if (dot < 0) return 2;
        int end = dot + 1;
        while (end < cm.format.size() && cm.format[end].isDigit()) ++end;
        return cm.format.mid(dot + 1, end - dot - 1).toInt();
    }
    if (cm.type == CompuMethod::Type::Linear && cm.linA != 0.0 && cm.linA != 1.0) {
        double a = std::abs(cm.linA);
        int p = int(std::floor(-std::log10(a)));
        return qBound(0, p, 3);
    }
    return 2;
}

QString MapPropertiesDialog::formatFromPrecision(int prec) const
{
    return QString("%1.%2f").arg(1).arg(prec);
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

MapPropertiesDialog::MapPropertiesDialog(const MapInfo &map, ByteOrder byteOrder,
                                         QWidget *parent)
    : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
    , m_result(map)
    , m_byteOrder(byteOrder)
{
    setWindowTitle(tr("Properties of…  %1").arg(map.name));
    setMinimumSize(520, 580);
    setStyleSheet(kStyle);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto *tabs = new QTabWidget();
    tabs->addTab(buildMapTab(),     tr("Map"));
    tabs->addTab(buildAxisTab(true),  tr("X-Axis"));
    tabs->addTab(buildAxisTab(false), tr("Y-Axis"));
    tabs->addTab(buildCommentTab(), tr("Comment"));
    tabs->addTab(buildToolsTab(),   tr("Tools"));
    root->addWidget(tabs, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok |
                                    QDialogButtonBox::Cancel |
                                    QDialogButtonBox::Help);
    bb->button(QDialogButtonBox::Ok)->setDefault(true);
    root->addWidget(bb);

    connect(bb, &QDialogButtonBox::accepted, this, &MapPropertiesDialog::apply);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populateMap();
    populateAxis(true);
    populateAxis(false);
    populateComment();

    // Live preview: update map display as spinboxes/settings change
    auto notify = [this]() {
        collectMap();
        collectAxis(true);
        collectAxis(false);
        collectComment();
        emit previewChanged(m_result);
        if (m_previewCallback) m_previewCallback(m_result);
    };

    connect(m_nameEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    connect(m_descEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    connect(m_unitEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    if (m_folderCombo) connect(m_folderCombo, &QComboBox::currentTextChanged, this, [notify](const QString&) { notify(); });
    connect(m_addrEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
    connect(m_colsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [notify](int) { notify(); });
    connect(m_rowsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [notify](int) { notify(); });
    connect(m_dataOrgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
    connect(m_skipBytesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [notify](int) { notify(); });
    if (m_lineSkipSpin) connect(m_lineSkipSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [notify](int) { notify(); });
    connect(m_numFmtCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
    connect(m_signCheck, &QCheckBox::toggled, this, [notify](bool) { notify(); });
    if (m_recipCheck) connect(m_recipCheck, &QCheckBox::toggled, this, [notify](bool) { notify(); });
    connect(m_diffCheck, &QCheckBox::toggled, this, [notify](bool) { notify(); });
    connect(m_oriCheck, &QCheckBox::toggled, this, [notify](bool) { notify(); });
    connect(m_pctCheck, &QCheckBox::toggled, this, [notify](bool) { notify(); });
    connect(m_factorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [notify](double) { notify(); });
    connect(m_offsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [notify](double) { notify(); });
    connect(m_precSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [notify](int) { notify(); });

    // X-Axis
    if (m_xDescEdit)     connect(m_xDescEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    if (m_xUnitEdit)     connect(m_xUnitEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    if (m_xDataSrcCombo) connect(m_xDataSrcCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
    if (m_xAddrEdit)     connect(m_xAddrEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    if (m_xDataOrgCombo) connect(m_xDataOrgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
    if (m_xSkipBytesSpin)connect(m_xSkipBytesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [notify](int) { notify(); });
    if (m_xSigByteEdit)  connect(m_xSigByteEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    if (m_xSignCheck)    connect(m_xSignCheck, &QCheckBox::toggled, this, [notify](bool) { notify(); });
    if (m_xRecipCheck)   connect(m_xRecipCheck, &QCheckBox::toggled, this, [notify](bool) { notify(); });
    if (m_xReverseCheck) connect(m_xReverseCheck, &QCheckBox::toggled, this, [notify](bool) { notify(); });
    if (m_xFactorSpin)   connect(m_xFactorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [notify](double) { notify(); });
    if (m_xOffsetSpin)   connect(m_xOffsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [notify](double) { notify(); });
    if (m_xPrecSpin)     connect(m_xPrecSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [notify](int) { notify(); });

    // Y-Axis
    if (m_yDescEdit)     connect(m_yDescEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    if (m_yUnitEdit)     connect(m_yUnitEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    if (m_yDataSrcCombo) connect(m_yDataSrcCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
    if (m_yAddrEdit)     connect(m_yAddrEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    if (m_yDataOrgCombo) connect(m_yDataOrgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
    if (m_ySkipBytesSpin)connect(m_ySkipBytesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [notify](int) { notify(); });
    if (m_ySigByteEdit)  connect(m_ySigByteEdit, &QLineEdit::textChanged, this, [notify]() { notify(); });
    if (m_ySignCheck)    connect(m_ySignCheck, &QCheckBox::toggled, this, [notify](bool) { notify(); });
    if (m_yRecipCheck)   connect(m_yRecipCheck, &QCheckBox::toggled, this, [notify](bool) { notify(); });
    if (m_yReverseCheck) connect(m_yReverseCheck, &QCheckBox::toggled, this, [notify](bool) { notify(); });
    if (m_yFactorSpin)   connect(m_yFactorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [notify](double) { notify(); });
    if (m_yOffsetSpin)   connect(m_yOffsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [notify](double) { notify(); });
    if (m_yPrecSpin)     connect(m_yPrecSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [notify](int) { notify(); });

    if (m_commentEdit) connect(m_commentEdit, &QPlainTextEdit::textChanged, this, [notify]() { notify(); });

    restoreGeometry(rx14::appSettings()
                    .value("dialogGeometry/MapPropertiesDlg").toByteArray());
}

void MapPropertiesDialog::closeEvent(QCloseEvent *event)
{
    rx14::appSettings()
        .setValue("dialogGeometry/MapPropertiesDlg", saveGeometry());
    QDialog::closeEvent(event);
}

// ─────────────────────────────────────────────────────────────────────────────
// Build tabs
// ─────────────────────────────────────────────────────────────────────────────

QWidget *MapPropertiesDialog::buildMapTab()
{
    auto *w  = new QWidget();
    auto *fl = new QFormLayout(w);
    fl->setLabelAlignment(Qt::AlignRight);
    fl->setSpacing(6);
    fl->setContentsMargins(12, 12, 12, 12);

    // Name row with ">" button
    auto *nameRow = new QHBoxLayout();
    m_nameEdit = new QLineEdit();
    auto *nameFwd = new QPushButton(">");
    nameFwd->setFixedWidth(26); nameFwd->setToolTip(tr("Copy to all linked maps"));
    nameRow->addWidget(m_nameEdit);
    nameRow->addWidget(nameFwd);
    fl->addRow(tr("Name:"), nameRow);

    // Description
    auto *descRow = new QHBoxLayout();
    m_descEdit = new QLineEdit();
    auto *descFwd = new QPushButton(">");
    descFwd->setFixedWidth(26);
    descRow->addWidget(m_descEdit);
    descRow->addWidget(descFwd);
    fl->addRow(tr("Description:"), descRow);

    // Folder
    m_folderCombo = new QComboBox();
    m_folderCombo->setEditable(true);
    fl->addRow(tr("Folder:"), m_folderCombo);

    // Unit + Id on same row
    auto *unitIdRow = new QHBoxLayout();
    m_unitEdit = new QLineEdit(); m_unitEdit->setFixedWidth(80);
    auto *unitFwd = new QPushButton(">"); unitFwd->setFixedWidth(26);
    auto *idLbl   = new QLabel(tr("Id:"));
    idLbl->setStyleSheet("color:#8b949e;");
    m_idLabel = new QLabel();
    m_idLabel->setStyleSheet("color:#58a6ff; font-family:Consolas;");
    auto *idFwd = new QPushButton(">"); idFwd->setFixedWidth(26);
    unitIdRow->addWidget(m_unitEdit);
    unitIdRow->addWidget(unitFwd);
    unitIdRow->addSpacing(8);
    unitIdRow->addWidget(idLbl);
    unitIdRow->addWidget(m_idLabel, 1);
    unitIdRow->addWidget(idFwd);
    fl->addRow(tr("Unit:"), unitIdRow);

    fl->addRow(makeSeparator());

    // Start address
    auto *addrRow = new QHBoxLayout();
    m_addrEdit = new QLineEdit();
    m_addrEdit->setFixedWidth(120);
    m_addrEdit->setFont(QFont("Consolas", 9));
    auto *addrFwd = new QPushButton(tr("From hexdump cursor"));
    addrRow->addWidget(m_addrEdit);
    addrRow->addWidget(addrFwd);
    addrRow->addStretch();
    fl->addRow(tr("Start address:"), addrRow);

    // Type
    m_typeCombo = new QComboBox();
    m_typeCombo->addItems({"MAP", "CURVE", "VALUE", "VAL_BLK",
                           "2D Inverse", "3D", "3D Inverse"});
    fl->addRow(tr("Type:"), m_typeCombo);

    // Columns × rows
    auto *dimRow = new QHBoxLayout();
    m_colsSpin = new QSpinBox(); m_colsSpin->setRange(1, 256); m_colsSpin->setFixedWidth(70);
    m_rowsSpin = new QSpinBox(); m_rowsSpin->setRange(1, 256); m_rowsSpin->setFixedWidth(70);
    auto *xLbl = new QLabel("x"); xLbl->setStyleSheet("color:#e6edf3;");
    dimRow->addWidget(m_colsSpin);
    dimRow->addWidget(xLbl);
    dimRow->addWidget(m_rowsSpin);
    dimRow->addStretch();
    fl->addRow(tr("Columns × rows:"), dimRow);

    // Data organization + skip bytes + line skip bytes
    auto *dataOrgRow = new QHBoxLayout();
    m_dataOrgCombo  = dataOrgCombo(); m_dataOrgCombo->setMinimumWidth(140);
    auto *skipLbl   = new QLabel(tr("Skip bytes:"));
    skipLbl->setStyleSheet("color:#8b949e;");
    m_skipBytesSpin = new QSpinBox(); m_skipBytesSpin->setRange(0, 1024); m_skipBytesSpin->setFixedWidth(55);
    auto *lineSkipLbl = new QLabel(tr("Line skip:"));
    lineSkipLbl->setStyleSheet("color:#8b949e;");
    m_lineSkipSpin = new QSpinBox(); m_lineSkipSpin->setRange(0, 1024); m_lineSkipSpin->setFixedWidth(55);
    dataOrgRow->addWidget(m_dataOrgCombo);
    dataOrgRow->addSpacing(4);
    dataOrgRow->addWidget(skipLbl);
    dataOrgRow->addWidget(m_skipBytesSpin);
    dataOrgRow->addSpacing(4);
    dataOrgRow->addWidget(lineSkipLbl);
    dataOrgRow->addWidget(m_lineSkipSpin);
    dataOrgRow->addStretch();
    fl->addRow(tr("Data organization:"), dataOrgRow);

    // Number format
    m_numFmtCombo = new QComboBox();
    m_numFmtCombo->addItem(tr("Decimal   (Base 10 System)"), 0);
    m_numFmtCombo->addItem(tr("Hex       (Base 16 System)"), 1);
    m_numFmtCombo->addItem(tr("Binary    (Base 2 System)"),  2);
    fl->addRow(tr("Number format:"), m_numFmtCombo);

    fl->addRow(makeSeparator());

    // Checkboxes row
    auto *chkRow = new QHBoxLayout();
    m_signCheck  = new QCheckBox(tr("Sign"));
    m_recipCheck = new QCheckBox(tr("Reciprocal (1/x)"));
    m_diffCheck  = new QCheckBox(tr("Difference"));
    m_oriCheck   = new QCheckBox(tr("Original values"));
    m_pctCheck   = new QCheckBox(tr("Percent"));
    chkRow->addWidget(m_signCheck);
    chkRow->addSpacing(12);
    chkRow->addWidget(m_recipCheck);
    chkRow->addSpacing(12);
    chkRow->addWidget(m_diffCheck);
    chkRow->addStretch();
    fl->addRow(QString(), chkRow);
    auto *chkRow2 = new QHBoxLayout();
    chkRow2->addWidget(m_oriCheck);
    chkRow2->addSpacing(16);
    chkRow2->addWidget(m_pctCheck);
    chkRow2->addStretch();
    fl->addRow(QString(), chkRow2);

    fl->addRow(makeSeparator());

    // Factor / offset group
    auto *fctGroup = new QGroupBox(tr("Factor, offset"));
    fctGroup->setCheckable(true);
    auto *fgl = new QVBoxLayout(fctGroup);
    fgl->setSpacing(4);

    auto *fctRow = new QHBoxLayout();
    m_factorSpin = new QDoubleSpinBox();
    m_factorSpin->setRange(-1e9, 1e9); m_factorSpin->setDecimals(12);
    m_factorSpin->setSingleStep(0.001); m_factorSpin->setFixedWidth(140);
    auto *mulLbl = new QLabel("× EPROM");
    mulLbl->setStyleSheet("color:#8b949e;");
    fctRow->addWidget(m_factorSpin);
    fctRow->addWidget(mulLbl);
    fctRow->addStretch();
    fgl->addLayout(fctRow);

    auto *offRow = new QHBoxLayout();
    auto *valLbl = new QLabel(tr("Value ="));
    valLbl->setStyleSheet("color:#8b949e;");
    m_offsetSpin = new QDoubleSpinBox();
    m_offsetSpin->setRange(-1e9, 1e9); m_offsetSpin->setDecimals(12);
    m_offsetSpin->setSingleStep(0.001); m_offsetSpin->setFixedWidth(140);
    auto *plusLbl = new QLabel("+");
    plusLbl->setStyleSheet("color:#8b949e;");
    offRow->addWidget(valLbl);
    offRow->addSpacing(8);
    offRow->addWidget(plusLbl);
    offRow->addWidget(m_offsetSpin);
    offRow->addStretch();
    fgl->addLayout(offRow);
    fl->addRow(fctGroup);

    fl->addRow(makeSeparator());

    // Precision
    auto *precRow = new QHBoxLayout();
    m_precSpin = new QSpinBox(); m_precSpin->setRange(0, 100); m_precSpin->setFixedWidth(60);
    precRow->addWidget(new QLabel(tr("Decimals:")));
    precRow->addWidget(m_precSpin);
    precRow->addStretch();
    fl->addRow(tr("Precision:"), precRow);

    return w;
}

QWidget *MapPropertiesDialog::buildAxisTab(bool isX)
{
    auto *w  = new QWidget();
    auto *fl = new QFormLayout(w);
    fl->setLabelAlignment(Qt::AlignRight);
    fl->setSpacing(6);
    fl->setContentsMargins(12, 12, 12, 12);

    QLineEdit       *&descEdit     = isX ? m_xDescEdit     : m_yDescEdit;
    QLineEdit       *&unitEdit     = isX ? m_xUnitEdit     : m_yUnitEdit;
    QLabel          *&idLabel      = isX ? m_xIdLabel      : m_yIdLabel;
    QComboBox       *&dataSrcCb    = isX ? m_xDataSrcCombo : m_yDataSrcCombo;
    QLineEdit       *&addrEdit     = isX ? m_xAddrEdit     : m_yAddrEdit;
    QComboBox       *&dataOrgCb    = isX ? m_xDataOrgCombo : m_yDataOrgCombo;
    QSpinBox        *&skipBytesSpin= isX ? m_xSkipBytesSpin: m_ySkipBytesSpin;
    QLineEdit       *&sigByteEdit  = isX ? m_xSigByteEdit  : m_ySigByteEdit;
    QCheckBox       *&signCheck    = isX ? m_xSignCheck    : m_ySignCheck;
    QCheckBox       *&recipCheck   = isX ? m_xRecipCheck   : m_yRecipCheck;
    QCheckBox       *&reverseCheck = isX ? m_xReverseCheck : m_yReverseCheck;
    QDoubleSpinBox  *&factorSpin   = isX ? m_xFactorSpin   : m_yFactorSpin;
    QDoubleSpinBox  *&offsetSpin   = isX ? m_xOffsetSpin   : m_yOffsetSpin;
    QSpinBox        *&precSpin     = isX ? m_xPrecSpin     : m_yPrecSpin;

    // Description
    auto *descRow = new QHBoxLayout();
    descEdit = new QLineEdit();
    descRow->addWidget(descEdit);
    descRow->addWidget(new QPushButton(">"));
    fl->addRow(tr("Description:"), descRow);

    // Unit + Id
    auto *unitIdRow = new QHBoxLayout();
    unitEdit = new QLineEdit(); unitEdit->setFixedWidth(80);
    idLabel  = new QLabel();
    idLabel->setStyleSheet("color:#58a6ff; font-family:Consolas;");
    unitIdRow->addWidget(unitEdit);
    unitIdRow->addWidget(new QPushButton(">"));
    unitIdRow->addSpacing(8);
    unitIdRow->addWidget(new QLabel(tr("Id:")));
    unitIdRow->addWidget(idLabel, 1);
    unitIdRow->addWidget(new QPushButton(">"));
    fl->addRow(tr("Unit:"), unitIdRow);

    // Data source
    auto *srcRow = new QHBoxLayout();
    dataSrcCb = new QComboBox();
    dataSrcCb->addItems({"Data Address", "Header-based", "Graphic", "EEPROM", "Fixed / Index"});
    dataSrcCb->setMinimumWidth(140);
    srcRow->addWidget(dataSrcCb);
    srcRow->addWidget(new QPushButton("…"));
    srcRow->addStretch();
    fl->addRow(tr("Data source:"), srcRow);

    fl->addRow(makeSeparator());

    // Start address
    auto *addrRow = new QHBoxLayout();
    addrEdit = new QLineEdit(); addrEdit->setFixedWidth(120);
    addrEdit->setFont(QFont("Consolas", 9));
    addrRow->addWidget(addrEdit);
    addrRow->addWidget(new QPushButton(tr("From hexdump cursor")));
    addrRow->addStretch();
    fl->addRow(tr("Start address:"), addrRow);

    // Skip bytes & Signature Byte
    auto *extraRow = new QHBoxLayout();
    skipBytesSpin = new QSpinBox(); skipBytesSpin->setRange(0, 1024); skipBytesSpin->setFixedWidth(55);
    sigByteEdit = new QLineEdit(); sigByteEdit->setFixedWidth(55); sigByteEdit->setFont(QFont("Consolas", 9));
    extraRow->addWidget(new QLabel(tr("Skip bytes:")));
    extraRow->addWidget(skipBytesSpin);
    extraRow->addSpacing(12);
    extraRow->addWidget(new QLabel(tr("Sig byte (hex):")));
    extraRow->addWidget(sigByteEdit);
    extraRow->addStretch();
    fl->addRow(QString(), extraRow);

    // Mirror map + search axis
    auto *mirrorRow = new QHBoxLayout();
    auto *mirrorCheck = new QCheckBox(tr("Mirror map"));
    auto *searchBtn   = new QPushButton(tr("Search axis…"));
    mirrorRow->addWidget(mirrorCheck);
    mirrorRow->addStretch();
    mirrorRow->addWidget(searchBtn);
    fl->addRow(QString(), mirrorRow);

    fl->addRow(makeSeparator());

    // Data organization
    dataOrgCb = dataOrgCombo();
    fl->addRow(tr("Data organization:"), dataOrgCb);

    // Checkboxes row
    auto *chkRow = new QHBoxLayout();
    signCheck = new QCheckBox(tr("Sign"));
    recipCheck = new QCheckBox(tr("Reciprocal (1/x)"));
    reverseCheck = new QCheckBox(tr("Reverse direction"));
    chkRow->addWidget(signCheck);
    chkRow->addSpacing(12);
    chkRow->addWidget(recipCheck);
    chkRow->addSpacing(12);
    chkRow->addWidget(reverseCheck);
    chkRow->addStretch();
    fl->addRow(QString(), chkRow);

    fl->addRow(makeSeparator());

    // Factor / offset
    auto *fctGroup = new QGroupBox(tr("Factor, offset"));
    auto *fgl = new QVBoxLayout(fctGroup);
    fgl->setSpacing(4);

    auto *fctRow = new QHBoxLayout();
    factorSpin = new QDoubleSpinBox();
    factorSpin->setRange(-1e9, 1e9); factorSpin->setDecimals(12);
    factorSpin->setSingleStep(0.001); factorSpin->setFixedWidth(140);
    fctRow->addWidget(factorSpin);
    fctRow->addWidget(new QLabel("× EPROM"));
    fctRow->addStretch();
    fgl->addLayout(fctRow);

    auto *offRow = new QHBoxLayout();
    offsetSpin = new QDoubleSpinBox();
    offsetSpin->setRange(-1e9, 1e9); offsetSpin->setDecimals(12);
    offsetSpin->setSingleStep(0.001); offsetSpin->setFixedWidth(140);
    offRow->addWidget(new QLabel(tr("Value =")));
    offRow->addSpacing(8);
    offRow->addWidget(new QLabel("+"));
    offRow->addWidget(offsetSpin);
    offRow->addStretch();
    fgl->addLayout(offRow);
    fl->addRow(fctGroup);

    // Precision
    auto *precRow = new QHBoxLayout();
    precSpin = new QSpinBox(); precSpin->setRange(0, 100); precSpin->setFixedWidth(60);
    precRow->addWidget(precSpin);
    precRow->addStretch();
    fl->addRow(tr("Precision:"), precRow);

    return w;
}

QWidget *MapPropertiesDialog::buildCommentTab()
{
    auto *w  = new QWidget();
    auto *vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(6);

    auto *lbl = new QLabel(tr("User notes / comments:"));
    lbl->setStyleSheet("color:#8b949e;");
    m_commentEdit = new QPlainTextEdit();
    m_commentEdit->setFont(QFont("Segoe UI", 9));
    vl->addWidget(lbl);
    vl->addWidget(m_commentEdit, 1);
    return w;
}

QWidget *MapPropertiesDialog::buildToolsTab()
{
    auto *w  = new QWidget();
    auto *vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(6);

    auto *exportBtn = new QPushButton(tr("Export map data to CSV…"));
    auto *copyBtn   = new QPushButton(tr("Copy raw values to clipboard"));
    auto *findBtn   = new QPushButton(tr("Search axis in ROM…"));

    vl->addWidget(exportBtn);
    vl->addWidget(copyBtn);
    vl->addWidget(findBtn);
    vl->addStretch();
    return w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Populate from m_result
// ─────────────────────────────────────────────────────────────────────────────

void MapPropertiesDialog::populateMap()
{
    const MapInfo &m = m_result;
    m_nameEdit->setText(m.name);
    m_descEdit->setText(m.description);
    m_unitEdit->setText(m.scaling.unit);
    if (m_folderCombo) {
        if (m_folderCombo->findText(m.folderPath) < 0 && !m.folderPath.isEmpty())
            m_folderCombo->addItem(m.folderPath);
        m_folderCombo->setCurrentText(m.folderPath);
    }
    m_idLabel->setText(m.id.isEmpty() ? m.name : m.id);

    // OLS convention: show file offset of where map DATA starts
    m_addrEdit->setText(QString("%1").arg(m.address + m.mapDataOffset, 0, 16).toUpper());

    // Type combo
    int typeIdx = m_typeCombo->findText(m.type);
    m_typeCombo->setCurrentIndex(typeIdx >= 0 ? typeIdx : 0);

    m_colsSpin->setValue(m.dimensions.x);
    m_rowsSpin->setValue(m.dimensions.y);

    setDataOrg(m_dataOrgCombo, m.dataSize, m_byteOrder);
    m_skipBytesSpin->setValue((int)m.mapDataOffset);
    if (m_lineSkipSpin) m_lineSkipSpin->setValue(m.getSideProp("LineSkipBytes").toInt());

    m_signCheck->setChecked(m.dataSigned);
    if (m_recipCheck) m_recipCheck->setChecked(m.getSideProp("bKehrwert").toBool());

    m_factorSpin->setValue(m.scaling.linA);
    m_offsetSpin->setValue(m.scaling.linB);
    m_precSpin->setValue(precisionFromFormat(m.scaling));
}

void MapPropertiesDialog::populateAxis(bool isX)
{
    const AxisInfo &ax = isX ? m_result.xAxis : m_result.yAxis;

    QLineEdit      *descEdit   = isX ? m_xDescEdit   : m_yDescEdit;
    QLineEdit      *unitEdit   = isX ? m_xUnitEdit   : m_yUnitEdit;
    QLabel         *idLabel    = isX ? m_xIdLabel    : m_yIdLabel;
    QComboBox      *dataSrcCb  = isX ? m_xDataSrcCombo : m_yDataSrcCombo;
    QLineEdit      *addrEdit   = isX ? m_xAddrEdit   : m_yAddrEdit;
    QComboBox      *dataOrgCb  = isX ? m_xDataOrgCombo : m_yDataOrgCombo;
    QSpinBox       *skipBytesSpin = isX ? m_xSkipBytesSpin : m_ySkipBytesSpin;
    QLineEdit      *sigByteEdit= isX ? m_xSigByteEdit: m_ySigByteEdit;
    QCheckBox      *signCheck  = isX ? m_xSignCheck  : m_ySignCheck;
    QCheckBox      *recipCheck = isX ? m_xRecipCheck : m_yRecipCheck;
    QCheckBox      *reverseCheck= isX ? m_xReverseCheck: m_yReverseCheck;
    QDoubleSpinBox *factorSpin = isX ? m_xFactorSpin : m_yFactorSpin;
    QDoubleSpinBox *offsetSpin = isX ? m_xOffsetSpin : m_yOffsetSpin;
    QSpinBox       *precSpin   = isX ? m_xPrecSpin   : m_yPrecSpin;

    descEdit->setText(ax.inputName);
    unitEdit->setText(ax.scaling.unit);
    idLabel->setText(ax.inputName);
    if (dataSrcCb) {
        QString srcStr = m_result.getSideProp(isX ? "StuetzX.DataSrc" : "StuetzY.DataSrc").toString();
        int idx = dataSrcCb->findText(srcStr);
        dataSrcCb->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addrEdit->setText(ax.hasPtsAddress
        ? QString("0x%1").arg(ax.ptsAddress, 0, 16).toUpper()
        : QString());
    setDataOrg(dataOrgCb, ax.ptsDataSize, m_byteOrder);
    if (skipBytesSpin) skipBytesSpin->setValue(m_result.getSideProp(isX ? "StuetzX.SkipBytes" : "StuetzY.SkipBytes").toInt());
    if (sigByteEdit) {
        uint32_t sig = m_result.getSideProp(isX ? "StuetzX.SignaturByte" : "StuetzY.SignaturByte").toUInt();
        sigByteEdit->setText(sig > 0 ? QString("0x%1").arg(sig, 2, 16, QChar('0')).toUpper() : QString());
    }
    signCheck->setChecked(ax.ptsSigned);
    if (recipCheck) recipCheck->setChecked(m_result.getSideProp(isX ? "StuetzX.bKehrwert" : "StuetzY.bKehrwert").toBool());
    if (reverseCheck) reverseCheck->setChecked(m_result.getSideProp(isX ? "StuetzX.bRueckwaerts" : "StuetzY.bRueckwaerts").toBool());

    factorSpin->setValue(ax.scaling.linA);
    offsetSpin->setValue(ax.scaling.linB);
    precSpin->setValue(precisionFromFormat(ax.scaling));
}

void MapPropertiesDialog::populateComment()
{
    m_commentEdit->setPlainText(m_result.userNotes);
}

// ─────────────────────────────────────────────────────────────────────────────
// Collect → apply → accept
// ─────────────────────────────────────────────────────────────────────────────

void MapPropertiesDialog::collectMap()
{
    m_result.name        = m_nameEdit->text().trimmed();
    m_result.description = m_descEdit->text().trimmed();
    m_result.scaling.unit = m_unitEdit->text().trimmed();
    if (m_folderCombo) m_result.folderPath = m_folderCombo->currentText().trimmed();

    // Address: user sees address+mapDataOffset (OLS convention), reverse to get address
    bool ok;
    uint32_t displayAddr = m_addrEdit->text().trimmed().toUInt(&ok, 16);
    if (ok) {
        uint32_t oldOffset = m_result.mapDataOffset; // read before overwrite below
        uint32_t newAddress = (displayAddr >= oldOffset) ? displayAddr - oldOffset : displayAddr;
        int32_t delta = (int32_t)newAddress - (int32_t)m_result.address;
        m_result.rawAddress = (uint32_t)((int32_t)m_result.rawAddress + delta);
        m_result.address    = newAddress;
    }

    // Type
    const QString t = m_typeCombo->currentText();
    if (t == "MAP" || t == "CURVE" || t == "VALUE" || t == "VAL_BLK")
        m_result.type = t;

    m_result.dimensions.x = m_colsSpin->value();
    m_result.dimensions.y = m_rowsSpin->value();
    m_result.xAxis.ptsCount = m_result.dimensions.x;
    m_result.yAxis.ptsCount = m_result.dimensions.y;

    auto [ds, bo] = fromDataOrg(m_dataOrgCombo);
    m_result.dataSize  = ds;
    m_byteOrder        = bo;
    m_result.mapDataOffset = (uint32_t)m_skipBytesSpin->value();
    if (m_lineSkipSpin) m_result.setSideProp("LineSkipBytes", m_lineSkipSpin->value());

    m_result.dataSigned = m_signCheck->isChecked();
    m_result.scaling.linA  = m_factorSpin->value();
    m_result.scaling.linB  = m_offsetSpin->value();
    m_result.scaling.format = formatFromPrecision(m_precSpin->value());
    if (m_recipCheck) m_result.setSideProp("bKehrwert", m_recipCheck->isChecked() ? 1 : 0);
    if (m_result.scaling.linA != 1.0 || m_result.scaling.linB != 0.0) {
        m_result.scaling.type = CompuMethod::Type::Linear;
    } else {
        m_result.scaling.type = CompuMethod::Type::Identical;
    }
    m_result.hasScaling = (m_result.scaling.type != CompuMethod::Type::Identical);
}

void MapPropertiesDialog::collectAxis(bool isX)
{
    AxisInfo &ax = isX ? m_result.xAxis : m_result.yAxis;

    QLineEdit      *descEdit   = isX ? m_xDescEdit   : m_yDescEdit;
    QLineEdit      *unitEdit   = isX ? m_xUnitEdit   : m_yUnitEdit;
    QComboBox      *dataSrcCb  = isX ? m_xDataSrcCombo : m_yDataSrcCombo;
    QLineEdit      *addrEdit   = isX ? m_xAddrEdit   : m_yAddrEdit;
    QComboBox      *dataOrgCb  = isX ? m_xDataOrgCombo : m_yDataOrgCombo;
    QSpinBox       *skipBytesSpin = isX ? m_xSkipBytesSpin : m_ySkipBytesSpin;
    QLineEdit      *sigByteEdit= isX ? m_xSigByteEdit: m_ySigByteEdit;
    QCheckBox      *signCheck  = isX ? m_xSignCheck  : m_ySignCheck;
    QCheckBox      *recipCheck = isX ? m_xRecipCheck : m_yRecipCheck;
    QCheckBox      *reverseCheck= isX ? m_xReverseCheck: m_yReverseCheck;
    QDoubleSpinBox *factorSpin = isX ? m_xFactorSpin : m_yFactorSpin;
    QDoubleSpinBox *offsetSpin = isX ? m_xOffsetSpin : m_yOffsetSpin;
    QSpinBox       *precSpin   = isX ? m_xPrecSpin   : m_yPrecSpin;

    ax.inputName     = descEdit->text().trimmed();
    ax.scaling.unit  = unitEdit->text().trimmed();
    if (dataSrcCb) m_result.setSideProp(isX ? "StuetzX.DataSrc" : "StuetzY.DataSrc", dataSrcCb->currentText());

    bool ok;
    uint32_t addr = addrEdit->text().trimmed().toUInt(&ok, 16);
    if (ok) { ax.ptsAddress = addr; ax.hasPtsAddress = true; }

    auto [ds, bo] = fromDataOrg(dataOrgCb);
    ax.ptsDataSize = ds;

    if (skipBytesSpin) m_result.setSideProp(isX ? "StuetzX.SkipBytes" : "StuetzY.SkipBytes", skipBytesSpin->value());
    if (sigByteEdit) {
        bool okHex;
        uint32_t sig = sigByteEdit->text().trimmed().toUInt(&okHex, 16);
        if (okHex) m_result.setSideProp(isX ? "StuetzX.SignaturByte" : "StuetzY.SignaturByte", sig);
    }
    ax.ptsSigned = signCheck->isChecked();
    if (recipCheck) m_result.setSideProp(isX ? "StuetzX.bKehrwert" : "StuetzY.bKehrwert", recipCheck->isChecked() ? 1 : 0);
    if (reverseCheck) m_result.setSideProp(isX ? "StuetzX.bRueckwaerts" : "StuetzY.bRueckwaerts", reverseCheck->isChecked() ? 1 : 0);

    ax.scaling.linA   = factorSpin->value();
    ax.scaling.linB   = offsetSpin->value();
    ax.scaling.format = formatFromPrecision(precSpin->value());
    if (ax.scaling.linA != 1.0 || ax.scaling.linB != 0.0) {
        ax.scaling.type = CompuMethod::Type::Linear;
    } else {
        ax.scaling.type = CompuMethod::Type::Identical;
    }
    ax.hasScaling = (ax.scaling.type != CompuMethod::Type::Identical);
}

void MapPropertiesDialog::collectComment()
{
    m_result.userNotes = m_commentEdit->toPlainText();
}

void MapPropertiesDialog::apply()
{
    collectMap();
    collectAxis(true);
    collectAxis(false);
    collectComment();
    accept();
}

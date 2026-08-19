/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "Med17Descriptor.h"

#include <QByteArray>

#include <cstdint>
#include <vector>

namespace Checksum::MED17 {

/**
 * MED17 FUN_10030f10: parse the 0x30 customer-block checksum descriptor
 * records.  In verify mode (correct=false) it only scans; in correct mode it
 * patches the block's descriptor magic.  When flagOut is non-null it receives
 * the parser's status flag (the DLL's local_50).
 */
void processCustomerBlock(QByteArray& rom, const std::vector<Descriptor>& descriptors,
                          bool correct, int* flagOut);

} // namespace Checksum::MED17

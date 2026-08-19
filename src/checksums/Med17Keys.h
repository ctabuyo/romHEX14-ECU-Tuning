/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace Checksum::MED17 {

/** Return MED17's selected 1024-bit public modulus in GMP's little-endian byte order. */
std::optional<std::array<uint8_t, 128>> publicKeyForIndex(uint32_t keyIndex);

} // namespace Checksum::MED17

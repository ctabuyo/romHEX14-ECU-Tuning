/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "RsaMath1024.h"
#include <chrono>
#include <array>
#include <cstdint>

namespace Checksum::Common {

enum class ForgeStatus {
    Uncorrected,
    Corrected,
    TimedOut,
    AttemptLimitReached,
    InvalidParameters,
};

struct ForgeLimits {
    std::chrono::milliseconds timeout{10000};
    uint64_t maximumAttempts{0};
    unsigned int workerCount{0}; // 0 = hardware concurrency
};

struct ForgeResult {
    ForgeStatus status = ForgeStatus::InvalidParameters;
    std::array<uint8_t, 128> signature{};
    uint64_t attempts = 0;
};

class BleichenbacherForge {
public:
    /**
     * Forge a valid 1024-bit RSA signature block for exponent e=3
     * using cube-root modular root search.
     *
     * @param targetTemplate 128-byte template containing PKCS#1 padding + target SHA1
     * @param initialTailCrc Precomputed CRC-32 seed for tail alignment
     * @param limits Execution timeout and worker threads configuration
     */
    static ForgeResult forgeSignature(
        const std::array<uint8_t, 128>& targetTemplate,
        ForgeLimits limits = ForgeLimits{});
};

} // namespace Checksum::Common

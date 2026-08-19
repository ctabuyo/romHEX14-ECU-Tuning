/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "Med17Descriptor.h"
#include "Med17Rsa.h"

#include <QByteArray>

#include <chrono>
#include <cstdint>

namespace Checksum::MED17 {

/** Limits for MED17's e=3 forged-signature search. */
struct CorrectionLimits {
    /// MED17 uses min(processorCount, 16) workers.
    unsigned int workerCount = 0;
    /// The original DLL waits for at most one hour.
    std::chrono::milliseconds timeout = std::chrono::hours(1);
    /// Test/diagnostic guard. Zero retains the time-bound-only DLL behavior.
    uint64_t maximumAttempts = 0;
};

enum class CorrectionStatus {
    Corrected,
    TimedOut,
    AttemptLimitReached,
    InvalidDescriptor,
    VerificationFailed,
};

struct CorrectionResult {
    CorrectionStatus status = CorrectionStatus::InvalidDescriptor;
    QByteArray signature;
    uint64_t attempts = 0;
};

/**
 * Reproduce MED17's descriptor-signature correction path.
 *
 * The routine retains the original 16-worker maximum, 0x10001-candidate work
 * blocks, reflected tail-CRC predicate, and final ceiling-cube-root write. It
 * never mutates the supplied ROM; callers must verify and apply the returned
 * 128-byte signature atomically.
 */
CorrectionResult forgeCorrectedSignature(const QByteArray& rom,
                                         const Descriptor& descriptor,
                                         const RsaSignatureResult& existingSignature,
                                         CorrectionLimits limits = {});

} // namespace Checksum::MED17

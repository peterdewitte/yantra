// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's sound through miniaudio (spec 16.2; REC-06 voice notes):
// capture from the default microphone and playback on the default output,
// 16-bit mono. miniaudio loads its back-ends (PulseAudio, ALSA, JACK) at
// run time; without any, make_miniaudio_io() says so through
// capture_problem() / playback_problem(). `null_backend` uses miniaudio's
// null device (silence at the real rate): the tests' choice.
#pragma once

#include <memory>

#include "yantra/app/audio.h"

namespace yantra::app {

std::shared_ptr<AudioIo> make_miniaudio_io(bool null_backend = false);

}  // namespace yantra::app

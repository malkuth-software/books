#pragma once
#include "gui_forms/audio/audio.hpp"
#include "gui_forms/threading.hpp"
#include <future>
#include <memory>
#include <string>

namespace games {
// One process-wide worker, at most 32 queued reads. Requesting `cancellation` is
// observed during Vorbis decoding and before publishing a completed WAV read; a
// flag stays requested, so give each new load its own.
std::future<gui_forms::AudioClipResult> load_audio_clip(
    const std::string& name, std::shared_ptr<gui_forms::CancellationFlag> cancellation);
}

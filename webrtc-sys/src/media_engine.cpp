// Copyright 2023 LiveKit, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "livekit/media_engine.h"
#include "rtc_base/stream.h"  // Add proper header for Stream interfaces
#include "livekit/rtc_error.h"
#include "livekit-ffi/include/rust_types.h"

#include "api/media_stream_interface.h"
#include "api/array_view.h"
#include "media/base/media_engine.h"
#include "media/engine/webrtc_voice_engine.h"
#include "media/engine/webrtc_video_engine.h"
#include "rtc_base/stream.h"
#include "rtc_base/ref_counted_object.h"

namespace livekit {

namespace {

class CompositeMediaEngine final : public cricket::CompositeMediaEngine {
public:
    CompositeMediaEngine()
        : cricket::CompositeMediaEngine(
            std::make_unique<cricket::WebRtcVoiceEngine>(),
            std::make_unique<cricket::WebRtcVideoEngine>()
        ) {}
};

} // namespace

MediaEngineWrapper::MediaEngineWrapper(std::shared_ptr<cricket::CompositeMediaEngine> engine) {
    if (!engine) {
        engine = std::make_shared<CompositeMediaEngine>();
    }
    engine_ = engine;
}

rust::Result<bool, std::string> MediaEngineWrapper::Init() {
    if (!engine_) {
        return rust::Result<bool, std::string>(std::string("Media engine not initialized"));
    }
    engine_->Init();  // void return type
    return rust::Result<bool, std::string>(true);  // Return success
}

rust::Result<std::shared_ptr<VoiceEngineWrapper>, std::string> MediaEngineWrapper::voice() {
    if (!engine_) {
        return rust::Result<std::shared_ptr<VoiceEngineWrapper>, std::string>(std::string("Media engine not initialized"));
    }
    auto voice_wrapper = std::make_shared<VoiceEngineWrapper>(&engine_->voice());
    return rust::Result<std::shared_ptr<VoiceEngineWrapper>, std::string>(voice_wrapper);
}

rust::Result<std::shared_ptr<VideoEngineWrapper>, std::string> MediaEngineWrapper::video() {
    if (!engine_) {
        return rust::Result<std::shared_ptr<VideoEngineWrapper>, std::string>(std::string("Media engine not initialized"));
    }
    auto video_wrapper = std::make_shared<VideoEngineWrapper>(&engine_->video());
    return rust::Result<std::shared_ptr<VideoEngineWrapper>, std::string>(video_wrapper);
}

VoiceEngineWrapper::VoiceEngineWrapper(cricket::VoiceEngineInterface* engine) : engine_(engine) {}

rust::Result<bool, std::string> VoiceEngineWrapper::Init() {
    if (!engine_) {
        return rust::Result<bool, std::string>(std::string("Voice engine not initialized"));
    }
    engine_->Init();  // void return type
    return rust::Result<bool, std::string>(true);  // Return success
}

VideoEngineWrapper::VideoEngineWrapper(cricket::VideoEngineInterface* engine) : engine_(engine) {}

rust::Result<bool, std::string> VideoEngineWrapper::Init() {
    if (!engine_) {
        return rust::Result<bool, std::string>(std::string("Video engine not initialized"));
    }
    // VideoEngine doesn't have Init, return true
    return rust::Result<bool, std::string>(true);
}

rust::Result<std::shared_ptr<MediaEngineWrapper>, std::string> create_media_engine() {
    try {
        auto wrapper = std::make_shared<MediaEngineWrapper>(nullptr);
        return rust::Result<std::shared_ptr<MediaEngineWrapper>, std::string>(wrapper);
    } catch (const std::exception& e) {
        return rust::Result<std::shared_ptr<MediaEngineWrapper>, std::string>(std::string("Failed to create media engine: ") + e.what());
    }
}

} // namespace livekit

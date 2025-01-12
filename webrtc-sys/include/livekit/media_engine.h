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

#pragma once

#include <memory>
#include <string>
#include "rust/cxx.h"
#include "media/base/media_engine.h"

namespace livekit {

class MediaEngineWrapper;
class VoiceEngineWrapper;
class VideoEngineWrapper;

using MediaEngineInterface = MediaEngineWrapper;
using VoiceEngineInterface = VoiceEngineWrapper;
using VideoEngineInterface = VideoEngineWrapper;

rust::Result<std::shared_ptr<MediaEngineInterface>> create_media_engine();

class MediaEngineWrapper final {
public:
    explicit MediaEngineWrapper(std::shared_ptr<cricket::CompositeMediaEngine> engine = nullptr);
    rust::Result<bool> Init();
    rust::Result<std::shared_ptr<VoiceEngineWrapper>> voice();
    rust::Result<std::shared_ptr<VideoEngineWrapper>> video();

private:
    std::shared_ptr<cricket::CompositeMediaEngine> engine_;
};

class VoiceEngineWrapper final {
public:
    explicit VoiceEngineWrapper(cricket::VoiceEngineInterface* engine);
    rust::Result<bool> Init();

private:
    cricket::VoiceEngineInterface* const engine_;
};

class VideoEngineWrapper final {
public:
    explicit VideoEngineWrapper(cricket::VideoEngineInterface* engine);
    rust::Result<bool> Init();

private:
    cricket::VideoEngineInterface* const engine_;
};

} // namespace livekit

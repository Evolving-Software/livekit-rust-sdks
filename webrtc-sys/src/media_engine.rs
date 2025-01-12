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

use cxx::{SharedPtr, UniquePtr};

#[cxx::bridge(namespace = "livekit")]
pub mod ffi {
    unsafe extern "C++" {
        include!("livekit/call.h");
        type VideoBitrateAllocatorFactory;
    }

    #[derive(Debug)]
    pub struct MediaConfig {
        pub audio: bool,
        pub video: bool,
        pub data: bool,
    }

    #[derive(Debug)]
    pub struct AudioOptions {
        pub echo_cancellation: bool,
        pub auto_gain_control: bool,
        pub noise_suppression: bool,
        pub highpass_filter: bool,
        pub stereo_swapping: bool,
        pub typing_detection: bool,
    }

    #[derive(Debug)]
    pub struct VideoOptions {
        pub width: u32,
        pub height: u32,
        pub frame_rate: u32,
    }

    #[derive(Debug)]
    pub struct CryptoOptions {
        pub srtp: bool,
        pub sframe: bool,
    }

    unsafe extern "C++" {
        include!("livekit/media_engine.h");

        type MediaEngineWrapper;
        type VoiceEngineWrapper;
        type VideoEngineWrapper;
        type Call;

        fn create_media_engine() -> Result<SharedPtr<MediaEngineWrapper>>;
        
        fn Init(self: &MediaEngineWrapper) -> Result<bool>;
        fn voice(self: &MediaEngineWrapper) -> Result<SharedPtr<VoiceEngineWrapper>>;
        fn video(self: &MediaEngineWrapper) -> Result<SharedPtr<VideoEngineWrapper>>;

        fn Init(self: &VoiceEngineWrapper) -> Result<bool>;
        fn create_send_channel(
            self: &VoiceEngineWrapper,
            call: SharedPtr<Call>,
            config: MediaConfig,
            options: AudioOptions,
            crypto_options: CryptoOptions,
            codec_pair_id: u32,
        ) -> Result<()>;
        fn create_receive_channel(
            self: &VoiceEngineWrapper,
            call: SharedPtr<Call>,
            config: MediaConfig,
            options: AudioOptions,
            crypto_options: CryptoOptions,
            codec_pair_id: u32,
        ) -> Result<()>;

        fn Init(self: &VideoEngineWrapper) -> Result<bool>;
        fn create_send_channel(
            self: &VideoEngineWrapper,
            call: SharedPtr<Call>,
            config: MediaConfig,
            options: VideoOptions,
            crypto_options: CryptoOptions,
            bitrate_allocator_factory: SharedPtr<VideoBitrateAllocatorFactory>,
        ) -> Result<()>;
        fn create_receive_channel(
            self: &VideoEngineWrapper,
            call: SharedPtr<Call>,
            config: MediaConfig,
            options: VideoOptions,
            crypto_options: CryptoOptions,
        ) -> Result<()>;
    }
}

// Re-export for other modules to use
pub use ffi::*;

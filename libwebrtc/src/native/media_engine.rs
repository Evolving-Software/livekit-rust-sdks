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

use std::sync::Arc;
use cxx::SharedPtr;
use webrtc_sys::media_engine::ffi;

use crate::{RtcError, RtcErrorType};

#[derive(Clone)]
pub struct MediaEngine {
    pub(crate) sys_handle: SharedPtr<ffi::MediaEngineWrapper>,
    voice_engine: Arc<VoiceEngine>,
    video_engine: Arc<VideoEngine>,
}

impl MediaEngine {
    pub fn new() -> Result<Self, RtcError> {
        let sys_handle = ffi::create_media_engine()
            .map_err(|e| RtcError {
                error_type: RtcErrorType::Internal,
                message: format!("Failed to create media engine: {}", e),
            })?;

        // Initialize the media engine first
        if !sys_handle.Init()
            .map_err(|e| RtcError {
                error_type: RtcErrorType::Internal,
                message: format!("Failed to initialize media engine: {}", e),
            })? {
            return Err(RtcError {
                error_type: RtcErrorType::Internal,
                message: "Media engine initialization returned false".to_string(),
            });
        }

        // Then get voice and video engines
        let voice_handle = sys_handle.voice()
            .map_err(|e| RtcError {
                error_type: RtcErrorType::Internal,
                message: format!("Failed to get voice engine: {}", e),
            })?;

        let video_handle = sys_handle.video()
            .map_err(|e| RtcError {
                error_type: RtcErrorType::Internal,
                message: format!("Failed to get video engine: {}", e),
            })?;

        let voice_engine = Arc::new(VoiceEngine::new(voice_handle)?);
        let video_engine = Arc::new(VideoEngine::new(video_handle)?);
        
        Ok(Self {
            sys_handle,
            voice_engine,
            video_engine,
        })
    }
}

impl crate::media_engine::MediaEngineInterface for MediaEngine {
    fn init(&self) -> Result<(), RtcError> {
        self.sys_handle.Init()
            .map_err(|e| RtcError {
                error_type: RtcErrorType::Internal,
                message: format!("Failed to initialize media engine: {}", e),
            })
            .map(|_| ())
    }

    fn voice(&self) -> Arc<dyn crate::media_engine::VoiceEngineInterface> {
        self.voice_engine.clone()
    }

    fn video(&self) -> Arc<dyn crate::media_engine::VideoEngineInterface> {
        self.video_engine.clone()
    }
}

#[derive(Clone)]
pub struct VoiceEngine {
    pub(crate) sys_handle: SharedPtr<ffi::VoiceEngineWrapper>,
}

impl VoiceEngine {
    pub fn new(sys_handle: SharedPtr<ffi::VoiceEngineWrapper>) -> Result<Self, RtcError> {
        Ok(Self { sys_handle })
    }
}

impl crate::media_engine::VoiceEngineInterface for VoiceEngine {
    fn init(&self) -> Result<(), RtcError> {
        self.sys_handle.Init()
            .map_err(|e| RtcError {
                error_type: RtcErrorType::Internal,
                message: format!("Failed to initialize voice engine: {}", e),
            })
            .map(|_| ())
    }

    fn create_send_channel(&self) -> Result<(), RtcError> {
        // TODO: Need to implement with proper parameters
        unimplemented!("Need to implement create_send_channel with proper parameters");
    }

    fn create_receive_channel(&self) -> Result<(), RtcError> {
        // TODO: Need to implement with proper parameters
        unimplemented!("Need to implement create_receive_channel with proper parameters");
    }
}

#[derive(Clone)]
pub struct VideoEngine {
    pub(crate) sys_handle: SharedPtr<ffi::VideoEngineWrapper>,
}

impl VideoEngine {
    pub fn new(sys_handle: SharedPtr<ffi::VideoEngineWrapper>) -> Result<Self, RtcError> {
        Ok(Self { sys_handle })
    }
}

impl crate::media_engine::VideoEngineInterface for VideoEngine {
    fn init(&self) -> Result<(), RtcError> {
        self.sys_handle.Init()
            .map_err(|e| RtcError {
                error_type: RtcErrorType::Internal,
                message: format!("Failed to initialize video engine: {}", e),
            })
            .map(|_| ())
    }

    fn create_send_channel(&self) -> Result<(), RtcError> {
        // TODO: Need to implement with proper parameters
        unimplemented!("Need to implement create_send_channel with proper parameters");
    }

    fn create_receive_channel(&self) -> Result<(), RtcError> {
        // TODO: Need to implement with proper parameters
        unimplemented!("Need to implement create_receive_channel with proper parameters");
    }
}

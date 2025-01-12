// Copyright 2023 Evolvere Techne
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

use crate::RtcError;
use std::sync::Arc;

/// Main interface for Evolvere Techne's media engine
pub trait MediaEngineInterface {
    /// Initialize the media engine
    fn init(&self) -> Result<(), RtcError>;

    /// Get the voice engine interface
    fn voice(&self) -> Arc<dyn VoiceEngineInterface>;

    /// Get the video engine interface
    fn video(&self) -> Arc<dyn VideoEngineInterface>;
}

/// Interface for Evolvere Techne's voice engine
pub trait VoiceEngineInterface {
    /// Initialize the voice engine
    fn init(&self) -> Result<(), RtcError>;

    /// Create a new audio send channel
    fn create_send_channel(&self) -> Result<(), RtcError>;

    /// Create a new audio receive channel
    fn create_receive_channel(&self) -> Result<(), RtcError>;
}

/// Interface for Evolvere Techne's video engine
pub trait VideoEngineInterface {
    /// Initialize the video engine
    fn init(&self) -> Result<(), RtcError>;

    /// Create a new video send channel
    fn create_send_channel(&self) -> Result<(), RtcError>;

    /// Create a new video receive channel
    fn create_receive_channel(&self) -> Result<(), RtcError>;
}

/// Concrete implementation of MediaEngine
pub struct MediaEngine {
    inner: Arc<dyn MediaEngineInterface>,
}

impl MediaEngine {
    /// Create a new MediaEngine instance
    pub fn new() -> Result<Self, RtcError> {
        let inner = crate::imp::MediaEngine::new()?;
        Ok(Self {
            inner: Arc::new(inner),
        })
    }

    /// Initialize the media engine
    pub fn init(&self) -> Result<(), RtcError> {
        self.inner.init()
    }

    /// Get the voice engine interface
    pub fn voice(&self) -> Arc<dyn VoiceEngineInterface> {
        self.inner.voice()
    }

    /// Get the video engine interface
    pub fn video(&self) -> Arc<dyn VideoEngineInterface> {
        self.inner.video()
    }
}

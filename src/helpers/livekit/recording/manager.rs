use std::sync::Arc;
use livekit::webrtc::PeerConnection;

pub struct RecordingState {
    pub peer_connection: Option<Arc<PeerConnection>>,
    // Add other fields as needed
}

impl RecordingState {
    pub fn new() -> Self {
        Self {
            peer_connection: None,
        }
    }
}

#[cxx::bridge(namespace = "livekit")]
pub mod ffi {
    #[repr(i32)]
    pub enum TrackState {
        Live,
        Ended,
    }

    unsafe extern "C++" {
        include!("livekit/media_stream_track.h");

        type MediaStreamTrack;

        fn kind(self: &MediaStreamTrack) -> String;
        fn id(self: &MediaStreamTrack) -> String;
        fn enabled(self: &MediaStreamTrack) -> bool;
        fn set_enabled(self: &MediaStreamTrack, enable: bool) -> bool;
        fn state(self: &MediaStreamTrack) -> TrackState;

        fn read_samples(self: &MediaStreamTrack, buffer: &mut [u8]) -> Pin<Box<dyn Future<Output = Result<usize>> + Send>>;

        fn _shared_media_stream_track() -> SharedPtr<MediaStreamTrack>;
    }
}

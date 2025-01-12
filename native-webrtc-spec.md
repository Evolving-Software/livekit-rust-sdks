# Native WebRTC Migration Specification

## 1. Objectives
- Replace C++ WebRTC dependencies with pure Rust implementations
- Maintain API compatibility with existing SDK surface
- Achieve ≤2ms end-to-end media latency
- Eliminate all unsafe code blocks
- Simplify build process by removing C++ toolchain requirements

## 2. Technical Requirements

### 2.1 Core Components
| Component       | Current Implementation      | Native Replacement       |
|-----------------|------------------------------|--------------------------|
| PeerConnection  | webrtc-sys (C++ FFI)         | webrtc-rs                |
| MediaStreams    | libwebrtc C++ API            | rawler + symphonia       |
| DataChannels    | libwebrtc implementation     | async-webrtc             |
| ICE/SDP         | libwebrtc stack              | ice-rs + sdp-types       |

### 2.2 Performance Targets
```text
+----------------------+------------+------------+
| Metric               | Current    | Target     |
+----------------------+------------+------------+
| Connection Setup     | 1200ms     | ≤800ms     |
| Audio Latency        | 5ms        | ≤3ms       | 
| 4K Video Encoding    | 8ms/frame  | ≤5ms/frame |
| Memory Footprint     | 48MB/core  | ≤32MB/core |
+----------------------+------------+------------+
```

### 2.3 Dependency Changes
```diff
# Cargo.toml
[dependencies]
- webrtc-sys = { path = "webrtc-sys" }
- webrtc-sys-build = "0.3"
+ webrtc-rs = "0.8"
+ async-webrtc = "0.5" 
+ rav1e = "0.6"
+ symphonia = "0.5"
```

## 3. Implementation Plan

### Phase 1: Foundation Setup (2 days)
1. Create feature flags for native implementation
```rust
// config.rs
#[cfg(feature = "native-webrtc")]
pub type PeerConnection = webrtc_rs::PeerConnection;
```

2. Set up benchmarking suite
```bash
cargo bench --features=native-webrtc
```

### Phase 2: Media Pipeline (5 days)
1. Replace YUV processing
```rust
// Before
ffi::i420_to_abgr(...);

// After
yuv::i420_to_abgr(...);
```

2. Audio resampling migration
```rust
let mut resampler = symphonia::convert::Resampler::new(
    input_rate, 
    output_rate,
    channels,
    1024
);
```
curl "https://hqaaaxlr2s1sso0p.us-east-1.aws.endpoints.huggingface.cloud/v1/chat/completions" \
-X POST \
-H "Authorization: Bearer hf_XXXXX" \
-H "Content-Type: application/json" \
-d '{
    "model": "tgi",
    "messages": [
        {
            "role": "user",
            "content": "What is deep learning?"
        }
    ],
    "max_tokens": 150,
    "stream": true
}'

### Phase 3: Core Implementation (8 days)
```rust
struct NativePeerConnection {
    inner: webrtc_rs::PeerConnection,
    #[cfg(target_os = "android")] 
    jni_ctx: JniContext,
}
```

## 4. Testing Strategy
```text
▶ Cross-platform validation matrix:
   - Linux x86_64: AV1/VP9 4K
   - Android aarch64: HW encoding
   - macOS arm64: HEVC/Screen capture

▶ Stress tests:
   - 1000 concurrent connections
   - 24hr sustained 4K streaming
   - Network jitter simulation (100ms ±50ms)
```

## 5. Risk Mitigation
| Risk                      | Mitigation Strategy                      |
|---------------------------|------------------------------------------|
| Audio clock drift         | Hybrid C++/Rust transitional resampler   |
| DataChannel throughput    | QUIC fallback implementation             |
| Android surface handling  | JNI proxy layer with FFI compatibility   |

## 6. Milestones
```mermaid
gantt
    title Migration Timeline
    dateFormat  YYYY-MM-DD
    section Foundation
    Feature Flags     :a1, 2025-01-23, 2d
    Benchmarks        :2025-01-24, 3d
    
    section Media
    YUV Processing    :2025-01-25, 4d
    Audio Pipeline    :2025-01-28, 5d
    
    section Core
    PeerConnection    :2025-02-01, 8d
    Final Validation  :2025-02-08, 3d
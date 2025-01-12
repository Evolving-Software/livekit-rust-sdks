/*
 *  Copyright 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/sctp/sctp_transport_factory.h"

#include "api/environment/environment.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "rtc_base/system/unused.h"

#ifdef WEBRTC_HAVE_DCSCTP
#include "media/sctp/dcsctp_transport.h"  // nogncheck
#endif

namespace cricket {

SctpTransportFactory::SctpTransportFactory(rtc::Thread* network_thread)
    : network_thread_(network_thread) {
  RTC_UNUSED(network_thread_);
}

std::unique_ptr<SctpTransportInternal>
SctpTransportFactory::CreateSctpTransport(const webrtc::Environment& env,
                                          DtlsTransportInternal* transport) {
  std::unique_ptr<SctpTransportInternal> result;
#ifdef WEBRTC_HAVE_DCSCTP
  result = std::unique_ptr<SctpTransportInternal>(
      new webrtc::DcSctpTransport(env, network_thread_, transport));
#endif
  return result;
}

}  // namespace cricket

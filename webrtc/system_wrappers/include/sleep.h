/*
 *  Copyright (c) 2012 The devzhaoyou@dii_media project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// An OS-independent sleep function.

#ifndef WEBRTC_SYSTEM_WRAPPERS_INCLUDE_SLEEP_H_
#define WEBRTC_SYSTEM_WRAPPERS_INCLUDE_SLEEP_H_

namespace dii_media_kit {

// This function sleeps for the specified number of milliseconds.
// It may return early if the thread is woken by some other event,
// such as the delivery of a signal on Unix.
void SleepMs(int msecs);

}  // namespace dii_media_kit

#endif  // WEBRTC_SYSTEM_WRAPPERS_INCLUDE_SLEEP_H_

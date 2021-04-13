/*
Copyright (C) 2019 Nokia Corporation.
This material, including documentation and any related
computer programs, is protected by copyright controlled by
Nokia Corporation. All rights are reserved. Copying,
including reproducing, storing, adapting or translating, any
or all of this material requires the prior written consent of
Nokia Corporation. This material also contains confidential
information which may not be disclosed to others without the
prior written consent of Nokia Corporation.
*/

#ifndef MEDIA_CODEC_EVENT_H_
#define MEDIA_CODEC_EVENT_H_

#include <utils/RefBase.h>

namespace android {

class MediaBufferBase;
class MediaCodecBuffer;

class IMediaCodecEventListener
{
public:
    IMediaCodecEventListener() {}
    virtual ~IMediaCodecEventListener() {}

    virtual bool notify(sp<MediaCodecBuffer> &src, MediaBufferBase *dest) = 0;
    virtual bool notify(MediaBufferBase *buf) = 0;
};

}  // namespace android

#endif  // MEDIA_CODEC_EVENT_H_

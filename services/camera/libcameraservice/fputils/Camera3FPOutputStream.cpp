/*
 * Copyright (C) 2016-2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstddef>
#define LOG_TAG "Camera3-FPOutStrm"
#define ATRACE_TAG ATRACE_TAG_CAMERA
// #define LOG_NDEBUG 0

#include "Camera3FPOutputStream.h"
#include <aidl/vendor/tcl/camera/algoservice/ITctCameraAlgoServiceCb.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <ctime>
#include <fstream>
#include <gui/BufferItemConsumer.h>
#include <gui/CpuConsumer.h>
#include <gui/Surface.h>
#include "fputils/FPCameraHelper.h"

#ifndef container_of
#define container_of(ptr, type, member) \
    (type *)((char*)(ptr) - offsetof(type, member))
#endif


using aidl::vendor::tcl::camera::algoservice::HandleParams;
using aidl::vendor::tcl::camera::algoservice::TctStreamConfiguration;
using aidl::vendor::tcl::camera::algoservice::TctAlgoStreamNegotiateResult;
using android::hardware::hidl_death_recipient;
using android::hardware::hidl_handle;
using android::hardware::hidl_vec;
using android::hardware::Return;
using CameraMetadatas = aidl::vendor::tcl::camera::algoservice::CameraMetadata;
namespace android {

namespace camera3 {

int32_t Camera3FPOutputStream::mHasStreamFeature = 0;

Camera3FPOutputStream::Camera3FPOutputStream(
        const std::string cameraId, int id, sp<Surface> consumer,
        uint32_t width, uint32_t height, int format,
        android_dataspace dataSpace, camera_stream_rotation_t rotation,
        nsecs_t timestampOffset, const std::string& physicalCameraId,
        const std::unordered_set<int32_t> &sensorPixelModesUsed, IPCTransport transport, int32_t extraBufferCnt,
        int32_t scenetype, CameraMetadata* characteristics, std::map<int32_t, CameraMetadata>& subDevicesInfo, int setId, bool isMultiResolution)
        : Camera3OutputStream (id, consumer,
        width, height, format,
        dataSpace, rotation,
        timestampOffset, physicalCameraId,
        sensorPixelModesUsed, transport,
        setId, isMultiResolution)
        , mAlgoSession (NULL) {

    mStreamExtraBufferCount = extraBufferCnt;
    mTctCameraAlgoService   = FPCameraHelper::getAlgoService();

    if (mTctCameraAlgoService != NULL) {
        status_t res;
        uint64_t usage = 0;

        res = native_window_get_consumer_usage(static_cast<ANativeWindow*>(consumer.get()), &usage);
        if (res != OK) {
            ALOGE("%s: getting end point usage failed: %s (%d).", __FUNCTION__, strerror(-res), res);
        }
        if (subDevicesInfo.size()) {
            ALOGI("%s: subDevicesInfo got size:%d", __FUNCTION__, subDevicesInfo.size());
            for (auto  iter = subDevicesInfo.begin(); iter != subDevicesInfo.end(); iter++) {
                const camera_metadata_t* meta = iter->second.getAndLock();
                CameraMetadatas subDeviceInfo;
                uint8_t*  aidlCharsP = reinterpret_cast<uint8_t*>(const_cast<camera_metadata_t*>(meta));
                subDeviceInfo.metadata.assign(aidlCharsP, aidlCharsP + get_camera_metadata_size(meta));
                int32_t ret = 0;
                mTctCameraAlgoService->addSubDeviceInfo(iter->first, subDeviceInfo, &ret);
                iter->second.unlock(meta);
            }
        } else {
            ALOGI("%s: no subDevicesInfo got", __FUNCTION__);
        }
        const camera_metadata_t* meta = characteristics->getAndLock();
        CameraMetadatas settings;
        uint8_t*  aidlCharsP = reinterpret_cast<uint8_t*>(const_cast<camera_metadata_t*>(meta));
        settings.metadata.assign(aidlCharsP, aidlCharsP + get_camera_metadata_size(meta));
        ALOGI("%s: %u %u %u %" PRIu64 "scenetype %d", __FUNCTION__,  width, height, format, usage, scenetype);
        TctStreamConfiguration streamCfg = {
            atoi(cameraId.c_str()),         id,   static_cast<int32_t>(width), static_cast<int32_t>(height), static_cast<int32_t>(format), static_cast<int64_t>(usage), dataSpace, rotation,
            atoi(physicalCameraId.c_str()), setId, static_cast<int32_t>(scenetype)
        };
        mTctCameraAlgoService->createStreamProcessor_V2(streamCfg, settings, &mAlgoSession);
        characteristics->unlock(meta);
    }

    addBufferListener(this);

    mThread = new FPOutputStreamThread(this);
    status_t res = mThread->run(String8::format("FPOutputStream-%d", mId).c_str());
    if (res != OK) {
        ALOGE("%s: Unable to start FPOutputStream thread", __FUNCTION__);
    }
}

Camera3FPOutputStream::Camera3FPOutputStream(
        const std::string cameraId, int id,
        uint32_t width, uint32_t height, int format, uint64_t consumerUsage,
        android_dataspace dataSpace, camera_stream_rotation_t rotation,
        nsecs_t timestampOffset, const std::string& physicalCameraId,
        const std::unordered_set<int32_t> &sensorPixelModesUsed, IPCTransport transport, int32_t extraBufferCnt,
        int32_t scenetype, CameraMetadata* characteristics, std::map<int32_t, CameraMetadata>& subDevicesInfo, int setId, bool isMultiResolution)
        : Camera3OutputStream (id,
        width, height, format, consumerUsage,
        dataSpace, rotation,
        timestampOffset, physicalCameraId,
        sensorPixelModesUsed, transport,
        setId, isMultiResolution)
        , mAlgoSession (NULL) {

    mStreamExtraBufferCount = extraBufferCnt;
    mTctCameraAlgoService   = FPCameraHelper::getAlgoService();

    if (mTctCameraAlgoService != NULL) {
        if (subDevicesInfo.size()) {
            ALOGI("%s: subDevicesInfo got size:%d", __FUNCTION__, subDevicesInfo.size());
            for (auto  iter = subDevicesInfo.begin(); iter != subDevicesInfo.end(); iter++) {
                const camera_metadata_t* meta = iter->second.getAndLock();
                CameraMetadatas subDeviceInfo;
                uint8_t*  aidlCharsP = reinterpret_cast<uint8_t*>(const_cast<camera_metadata_t*>(meta));
                subDeviceInfo.metadata.assign(aidlCharsP, aidlCharsP + get_camera_metadata_size(meta));
                int32_t ret = 0;
                mTctCameraAlgoService->addSubDeviceInfo(iter->first, subDeviceInfo, &ret);
                iter->second.unlock(meta);
            }
        } else {
            ALOGI("%s: no subDevicesInfo got", __FUNCTION__);
        }
        const camera_metadata_t* meta = characteristics->getAndLock();
        CameraMetadatas settings;
        uint8_t*  aidlCharsP = reinterpret_cast<uint8_t*>(const_cast<camera_metadata_t*>(meta));
        settings.metadata.assign(aidlCharsP, aidlCharsP + get_camera_metadata_size(meta));
        ALOGI("%s: %u %u %u %" PRIu64 "scenetype %d", __FUNCTION__,  width, height, format, consumerUsage, scenetype);
        TctStreamConfiguration streamCfg = {
            atoi(cameraId.c_str()),         id,   static_cast<int32_t>(width), static_cast<int32_t>(height), static_cast<int32_t>(format), static_cast<int64_t>(consumerUsage), dataSpace, rotation,
            atoi(physicalCameraId.c_str()), setId, static_cast<int32_t>(scenetype)
        };
        mTctCameraAlgoService->createStreamProcessor_V2(streamCfg, settings, &mAlgoSession);
        characteristics->unlock(meta);
        ALOGI("%s mAlgoSession:%p", __FUNCTION__,  mAlgoSession.get());
    }

    addBufferListener(this);

    mThread = new FPOutputStreamThread(this);
    status_t res = mThread->run(String8::format("TctOutputStream-%d", mId).c_str());
    if (res != OK) {
        ALOGE("%s: Unable to start TctOutputStream thread", __FUNCTION__);
    }
}

Camera3FPOutputStream::~Camera3FPOutputStream() {
    ALOGI("%s mAlgoSession:%p", __FUNCTION__,  mAlgoSession.get());
    stopThread();
    mThread.clear();
    mAlgoSession = nullptr;
    mTctCameraAlgoService = nullptr;
    mHasStreamFeature -= 1;
    ALOGI("%s featureStreams:%d", __FUNCTION__,  mHasStreamFeature);
}

void Camera3FPOutputStream::onBufferAcquired(const BufferInfo&) {

}

int32_t Camera3FPOutputStream::CheckStreamFeature() {
     return mHasStreamFeature;
}

void Camera3FPOutputStream::onBufferRequestForFrameNumber(uint64_t frameNumber, int streamId,
        const CameraMetadata& settings) {
    (void)frameNumber;
    (void)streamId;
    (void)settings;
/*    Mutex::Autolock l(mMutex);
    if (!mErrorState && (streamId == getStreamId())) {
        mPendingCaptureResults.emplace(frameNumber, CameraMetadata());
    }
    Mutex::Autolock l(mMutex);
    if (mErrorState || (streamId != getStreamId())) {
        return;
    }*/

//    mPendingCaptureResults.emplace(frameNumber, CameraMetadata(settings));

/*    camera_metadata_ro_entry entry;

    int32_t orientation = 0;
    entry = settings.find(ANDROID_JPEG_ORIENTATION);
    if (entry.count == 1) {
        orientation = entry.data.i32[0];
    }

    int32_t quality = kDefaultJpegQuality;
    entry = settings.find(ANDROID_JPEG_QUALITY);
    if (entry.count == 1) {
        quality = entry.data.i32[0];
    }

    mSettingsByFrameNumber[frameNumber] = {orientation, quality};*/
}

void Camera3FPOutputStream::onBufferReleased(const BufferInfo&) {
/*    Mutex::Autolock l(mMutex);
    if (!mErrorState && !bufferInfo.mError) {
        mFrameNumberMap.emplace(bufferInfo.mFrameNumber, bufferInfo.mTimestamp);
        mInputReadyCondition.signal();
    }*/
}

bool Camera3FPOutputStream::isVideoStream() {
    uint64_t usage = 0;
    status_t res = getEndpointUsage(&usage);
    if (res != OK) {
        ALOGE("%s: getting end point usage failed: %s (%d).", __FUNCTION__, strerror(-res), res);
        return false;
    }

    return (usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) != 0;
}

status_t Camera3FPOutputStream::getEndpointUsage(uint64_t *usage) {

    status_t res;

    if (mConsumer == nullptr) {
        // mConsumerUsage was sanitized before the Camera3OutputStream was constructed.
        *usage = getPresetConsumerUsage();
        return OK;
    }
    res = getEndpointUsageForSurface(usage, mConsumer);

    return res;
}

status_t Camera3FPOutputStream::returnBuffer(const camera_stream_buffer &buffer,
            nsecs_t timestamp, nsecs_t readoutTimestamp, bool timestampIncreasing,
            const std::vector<size_t>& surface_ids,
            uint64_t frameNumber, int32_t transform) {
    {
        Mutex::Autolock l(mLock);
        ANativeWindowBuffer *anwBuffer = container_of(buffer.buffer, ANativeWindowBuffer, handle);
        mBufferFrameNum.add(anwBuffer, frameNumber);
        ALOGV("%s set frameNum %" PRIu64 " for buffer %p", __FUNCTION__, frameNumber, anwBuffer);
    }
    status_t res = Camera3Stream::returnBuffer(buffer,
        timestamp, readoutTimestamp,timestampIncreasing,
        surface_ids, frameNumber,transform);

    return res;
}
#if 0
status_t Camera3FPOutputStream::returnBufferCheckedLocked(
            const camera_stream_buffer &buffer,
            nsecs_t timestamp,
            bool output,
            const std::vector<size_t>& surface_ids,
            /*out*/
            sp<Fence> *releaseFenceOut) {

    (void)output;
    ALOG_ASSERT(output, "Expected output to be true");

    status_t res;

    res = Camera3OutputStream::returnBufferCheckedLocked(
                    buffer,
                    timestamp,
                    output,
                    surface_ids,
                    /*out*/
                    releaseFenceOut);

    return res;
}
#endif
int32_t Camera3FPOutputStream::getscenetype(const CameraMetadata& sessionParams, metadata_vendor_id_t VendorTagId)
{
    uint32_t tag;
    uint32_t scenetype = -1;

    camera_metadata_t *meta = const_cast<camera_metadata_t *>(sessionParams.getAndLock());
    sp<VendorTagDescriptor> vTags = VendorTagDescriptor::getGlobalVendorTagDescriptor();
    if ((nullptr == vTags.get()) || (0 >= vTags->getTagCount())) {
        sp<VendorTagDescriptorCache> cache = VendorTagDescriptorCache::getGlobalVendorTagCache();
        if (cache.get()) {
            cache->getVendorTagDescriptor(VendorTagId, &vTags);
        }
    }
    sessionParams.unlock(meta);
    status_t res = sessionParams.getTagFromName("com.fp.feature.session.scenetype", vTags.get(), &tag);
    if (res != OK) {
        ALOGE("%s: getting com.fp.feature.session.scenetype failed: %s (%d).", __FUNCTION__, strerror(-res), res);
    } else {
        auto scene = sessionParams.find(tag);
        if (scene.count > 0) {
            scenetype = scene.data.i32[0];
        }
    }

    ALOGI("%s: getting com.fp.feature.session.scenetype %d", __FUNCTION__, scenetype);
    return scenetype;
}

bool Camera3FPOutputStream::negotiate(
        const std::string cameraId, int streamId, const std::vector<SurfaceHolder>& consumers, bool hasDeferredConsumer,
        uint32_t width, uint32_t height, uint32_t format, uint64_t consumerUsage,
        android_dataspace dataSpace, camera_stream_rotation_t rotation,
        const std::string& physicalCameraId, int streamSetId, int32_t &extraBufferCnt, int32_t scenetype) {
    extraBufferCnt = kStreamExtraBuffer;
    std::shared_ptr<ITctCameraAlgoService> tctCameraAlgoService = FPCameraHelper::getAlgoService();  // ITctCameraAlgoService::tryGetService();

    if (consumers.size() != 0){
        auto pFrameSync = FrameSynchronizer::getInstance();
        if (pFrameSync != nullptr && pFrameSync->isRegisteredSurface(consumers[0].mSurface.get())) {
            return true;
        }
    }
    if (tctCameraAlgoService != NULL) {
        uint64_t usage = 0;

        if (consumers.size() == 0 &&  hasDeferredConsumer) {
            usage = consumerUsage;
        }
        else {
            status_t res = native_window_get_consumer_usage(static_cast<ANativeWindow*>(consumers[0].mSurface.get()), &usage);
            if (res != OK) {
                ALOGE("%s: getting end point usage failed: %s (%d).", __FUNCTION__, strerror(-res), res);
            }
        }
        ALOGI("%s: hasDeferredConsumer:%d w:%u h:%u f:%u u:%" PRIu64, __FUNCTION__,  hasDeferredConsumer, width, height, format, usage);
        TctStreamConfiguration streamCfg = {
            atoi(cameraId.c_str()),         streamId,   static_cast<int32_t>(width), static_cast<int32_t>(height), static_cast<int32_t>(format), static_cast<int64_t>(usage), dataSpace, rotation,
            atoi(physicalCameraId.c_str()), streamSetId, scenetype
        };
        //Begin added by juting.huang for custom extra buffer count config
        TctAlgoStreamNegotiateResult streamNegotiateRet = {false, 0};
        tctCameraAlgoService->streamNegotiate_V2(streamCfg, &streamNegotiateRet);
        ALOGI("%s enable:%d, extraBufferCnt:%d", __FUNCTION__, streamNegotiateRet.enabled, streamNegotiateRet.extraBufferCnt);
        if (streamNegotiateRet.enabled && (format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED)) {
            mHasStreamFeature += 1;
            extraBufferCnt = streamNegotiateRet.extraBufferCnt;
        } else if (!streamNegotiateRet.enabled && (format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) && !(usage & GRALLOC_USAGE_HW_VIDEO_ENCODER)) {
            // mHasStreamFeature = false;
        }
        ALOGI("%s enable:%d, extraBufferCnt:%d featureStreams:%d", __FUNCTION__, streamNegotiateRet.enabled, streamNegotiateRet.extraBufferCnt, mHasStreamFeature);
        return streamNegotiateRet.enabled;
        //End added by juting.huang for custom extra buffer count config

       /*
        bool enable = false;
        tctCameraAlgoService->streamNegotiate(streamCfg, &enable);
        ALOGI("%s enable:%d extraBufferCnt:%d", __FUNCTION__, enable, extraBufferCnt);
        if (enable && (format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED)) {
            mHasStreamFeature = true;
        } else if (!enable && (format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) && !(usage & GRALLOC_USAGE_HW_VIDEO_ENCODER)) {
            mHasStreamFeature = false;
        }

        return enable;
        */
    }
    else {
        ALOGW("%s connect to tct camera algo service failed", __FUNCTION__);
        mHasStreamFeature = 0;
    }

    return false;
}

#define MIN(a,b)                   (((a)<(b))?(a):(b))
void dumpImage(ANativeWindowBuffer* anwBuffer, int fence, uint64_t frameNumber) {
    // Lock the image for CPU read
    sp<GraphicBuffer> graphicBuffer = GraphicBuffer::from(anwBuffer);
    base::unique_fd fenceFd(dup(fence));
    android_ycbcr mapped;

    int stride = graphicBuffer->getStride();
    int height = graphicBuffer->getHeight();

    status_t res = graphicBuffer->lockAsyncYCbCr(GraphicBuffer::USAGE_SW_READ_OFTEN | GraphicBuffer::USAGE_SW_WRITE_OFTEN, &mapped,
            fenceFd.get());
    if (res != OK) {
        ALOGE("%s: Failed to lock the buffer: %s (%d)", __FUNCTION__, strerror(-res), res);
        return;
    }
    ALOGE("%s: mapped: %p %p %p, chroma_step:%zu,ystride:%zu,cstride:%zu", __FUNCTION__, mapped.y, mapped.cb, mapped.cr, mapped.chroma_step, mapped.ystride, mapped.cstride);
    uint8_t *yBuffer = (uint8_t *)mapped.y;

    std::string fileExtension = "NV21";
    if (mapped.chroma_step == 1)
        fileExtension = "YV12";
    char imageFileName[64];
    time_t now = time(0);
    tm *localTime = localtime(&now);
    snprintf(imageFileName, sizeof(imageFileName), "IMG_%4d%02d%02d_%02d%02d%02d_%dx%d_%" PRId64 ".%s",
            1900 + localTime->tm_year, localTime->tm_mon + 1, localTime->tm_mday,
            localTime->tm_hour, localTime->tm_min, localTime->tm_sec, stride, height,
            frameNumber, fileExtension.c_str());
    // Output image data to file
    std::string filePath = "/data/misc/cameraserver/";
    filePath += imageFileName;
    std::ofstream imageFile(filePath.c_str(), std::ofstream::binary);
    if (!imageFile.is_open()) {
        ALOGE("%s: Unable to create file %s", __FUNCTION__, filePath.c_str());
        graphicBuffer->unlock();
        return;
    }
    imageFile.write((const char*)yBuffer, stride * height);
    if (mapped.chroma_step == 1) {
        uint8_t *uBuffer = (uint8_t *)mapped.cb;
        uint8_t *vBuffer = (uint8_t *)mapped.cr;
        imageFile.write((const char*)uBuffer, stride * height / 4);
        imageFile.write((const char*)vBuffer, stride * height / 4);
    }
    else {
        uint8_t *uvBuffer = (uint8_t *)MIN(mapped.cb, mapped.cr);
        imageFile.write((const char*)uvBuffer, stride * height / 2);
    }


    graphicBuffer->unlock();
}

bool Camera3FPOutputStream::doThreadLoop() {
    BufferHolder buffer(0, 0, 0, 0);
    {
        Mutex::Autolock l(mLock);
        if (mPendingBuffers.size() == 0) {
            if (mThread->needStop()) {
                ALOGI("%s exiting...", __FUNCTION__);
                return false;
            }
            mBufferCond.waitRelative(mLock, kWaitDuration);

            if (mThread->needStop()) {
                ALOGI("%s exiting...", __FUNCTION__);
                return false;
            } else {
                return true;
            }
        }

        buffer = mPendingBuffers.front();
        mPendingBuffers.pop();
    }

    if (mAlgoSession && buffer.frameNumber > DUMMY_FRAME) {
        CameraMetadatas settings;
        /*auto it = mPendingCaptureResults.find(frameNumber);
        if (it != mPendingCaptureResults.end()) {
            const camera_metadata_t* meta = it->second.getAndLock();
            uint32_t size = get_camera_metadata_size(meta);
            ALOGI("%s: result meta size:%" PRIu32, __FUNCTION__, size);
            settings.setToExternal((uint8_t*)meta, size);
            mPendingCaptureResults.erase(it);
        }*/

        HandleParams inHandle;
        int          res                = 0;
        auto createNativeHandle = [](int dup_fd) -> native_handle_t* {
            if (-1 != dup_fd) {
                auto handle = ::native_handle_create(/*numFds*/ 1, /*numInts*/ 0);
                if (CC_LIKELY(handle)) {
                    handle->data[0] = dup_fd;
                    return handle;
                }
            }
            return nullptr;
        };

        auto pFrameSync = FrameSynchronizer::getInstance();
        if (pFrameSync != nullptr && pFrameSync->needFrameSync(mId)) {
            pFrameSync->pushFrame(buffer.frameNumber, mId, std::move(buffer));
            auto mainStreamId = pFrameSync->getMainStreamId();
            if (mainStreamId == mId) {
                auto frameMap = pFrameSync->popFrames(buffer.frameNumber);
                if (frameMap.size() != 0) {
                    ALOGV("%s pop frameNum %" PRIu64" success" , __FUNCTION__, buffer.frameNumber);
                    std::vector<HandleParams> inputFrames ( frameMap.size() );
                    for (size_t i = 0; i != frameMap.size(); ++i) {
                        auto& frame = frameMap.at(mainStreamId + i);
                        inputFrames[i].width = frame.anwBuffer->width;
                        inputFrames[i].height = frame.anwBuffer->height;
                        inputFrames[i].stride = frame.anwBuffer->stride;
                        inputFrames[i].format = frame.anwBuffer->format;
                        inputFrames[i].usage  = frame.anwBuffer->usage;
                        native_handle_t* data = createNativeHandle(frame.anwReleaseFence);
                        if (data != nullptr) {
                            inputFrames[i].releaseFence = dupToAidl(data);
                            // native_handle_close(data); //Remove this for fixing double-close fd issue
                            native_handle_delete(data);
                        }
                        if (frame.anwBuffer->handle != nullptr) {
                            inputFrames[i].bufHandle = dupToAidl(frame.anwBuffer->handle);
                        }
                    }
                    mAlgoSession->multiStreamProcess(buffer.frameNumber, inputFrames.size(), inputFrames, settings, &res);
                    pFrameSync->releaseFrames(std::move(frameMap));
                }
            }
            return true;
        }
        inHandle.width      = buffer.anwBuffer->width;
        inHandle.height     = buffer.anwBuffer->height;
        inHandle.stride     = buffer.anwBuffer->stride;
        inHandle.format     = buffer.anwBuffer->format;
        inHandle.usage      = buffer.anwBuffer->usage;
        native_handle_t* data = createNativeHandle(buffer.anwReleaseFence);
        if(data != nullptr) {
            inHandle.releaseFence = dupToAidl(data);
            // native_handle_close(data); //Remove this for fixing double-close fd issue
            native_handle_delete(data);
        }
        if(buffer.anwBuffer->handle != nullptr) {
            inHandle.bufHandle = dupToAidl(buffer.anwBuffer->handle);
        }

        mAlgoSession->streamProcess(buffer.frameNumber, inHandle, settings, &res);
    }

    ALOGV("%s queueBuffer frameNum %" PRIu64 "usage 0x%x anwBufferHandle:%p anwReleaseFence:%d", __FUNCTION__, buffer.frameNumber, (uint32_t)buffer.anwBuffer->usage, buffer.anwBuffer->handle, buffer.anwReleaseFence);
    static_cast<ANativeWindow*>(mConsumer.get())->queueBuffer(mConsumer.get(), buffer.anwBuffer.get(), buffer.anwReleaseFence);
    return true;
}

void Camera3FPOutputStream::stopThread() {
    mThread->requestExit();
    mBufferCond.signal();
    mThread->requestExitAndWait();
}

// metadata
// thread queue
// algo judgement
status_t Camera3FPOutputStream::queueBufferToConsumer(sp<ANativeWindow>& consumer,
            ANativeWindowBuffer* buffer, int anwReleaseFence,
            const std::vector<size_t>&) {
    (void) consumer;
    status_t res = OK;
    uint64_t frameNumber = 0;
    {
        Mutex::Autolock l(mLock);
        int idx;
        if ((idx = mBufferFrameNum.indexOfKey(buffer)) != NAME_NOT_FOUND) {
            frameNumber = mBufferFrameNum.valueAt(idx);
            ALOGV("%s get frameNum %" PRIu64 " for buffer %p format %d", __FUNCTION__, frameNumber, buffer, buffer->format);
            mBufferFrameNum.removeItem(buffer);
        }
    }
    ALOGV("%s queueBuffer frameNum %" PRIu64 " for buffer %p", __FUNCTION__, frameNumber, buffer);

    Mutex::Autolock l(mLock);
    uint64_t usage = 0;
    getEndpointUsage(&usage);
    mPendingBuffers.emplace(frameNumber, buffer, anwReleaseFence, usage);

    mBufferCond.signal();

    return res;
}

} // namespace camera3

} // namespace android

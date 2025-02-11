#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#define LOG_TAG "FPCameraHelper"
#include "FPCameraHelper.h"
#include <aidl/vendor/tcl/camera/algoservice/ITctCameraAlgoService.h>
#include <aidl/vendor/tcl/camera/algoservice/ITctCameraAlgoServiceCb.h>
#include <android/binder_auto_utils.h>
#include <android/binder_manager.h>
#include <system/camera_metadata.h>
#include <libxml/tree.h>
#include <libxml/parser.h>

#define DUMMY_FRAME 1

using aidl::vendor::tcl::camera::algoservice::HandleParams;
using aidl::vendor::tcl::camera::algoservice::ITctCameraAlgoService;
using CameraMetadatas = aidl::vendor::tcl::camera::algoservice::CameraMetadata;
using android::hardware::hidl_death_recipient;
using android::hardware::hidl_handle;
using android::hardware::hidl_vec;
using android::hardware::Return;
using namespace android::hardware::camera;
namespace android{

std::shared_ptr<ITctCameraAlgoService> FPCameraHelper::getAlgoService() {
    static std::string kInstance = std::string() + ITctCameraAlgoService::descriptor + "/default";
    ::ndk::SpAIBinder  binder(AServiceManager_checkService(kInstance.c_str()));
    return ITctCameraAlgoService::fromBinder(binder);
}
int FPCameraHelper::MergeMetaData(
    const camera_metadata_t* pMetadata1,
    CameraMetadata&  dstMetadata)
{
    int    status       = 0;
    size_t totalEntries = 0;

    if (NULL == pMetadata1)
    {
        return status;
    }

    totalEntries = get_camera_metadata_entry_count(const_cast<const camera_metadata_t *>(pMetadata1));

    for (size_t i = 0; i < totalEntries; i++)
    {
        camera_metadata_ro_entry_t srcEntry;
        //camera_metadata_entry_t dstEntry;
        //camera_metadata_entry_t updatedEntry;

        get_camera_metadata_ro_entry(const_cast<camera_metadata*>(pMetadata1), i, &srcEntry);
        status = dstMetadata.update(srcEntry);
        /*status = find_camera_metadata_entry(reinterpret_cast<camera_metadata*>(pMetadata2), srcEntry.tag, &dstEntry);
        if (0 != status)
        {
            status = add_camera_metadata_entry(reinterpret_cast<camera_metadata*>(pMetadata2),
                         srcEntry.tag,
                         srcEntry.data.i32,
                         srcEntry.count);
        }
        else
        {
            if (0 == srcEntry.count)
            {
                status = delete_camera_metadata_entry(pMetadata2, srcEntry.tag);
                if (0 == status)
                {
                    status = add_camera_metadata_entry(pMetadata2,
                                                       srcEntry.tag,
                                                       srcEntry.data.i32,
                                                       srcEntry.count);
                }
            }
            else if ((0 != memcmp(srcEntry.data.u8,
                             dstEntry.data.u8,
                             (camera_metadata_type_size[srcEntry.type] * srcEntry.count))) ||
                (srcEntry.count != dstEntry.count)                                         ||
                (srcEntry.type  != dstEntry.type))
            {
                status = update_camera_metadata_entry(reinterpret_cast<camera_metadata*>(pMetadata2),
                             dstEntry.index,
                             srcEntry.data.i32,
                             srcEntry.count,
                             &updatedEntry);
            }
        }

        if (0 != status)
        {
            break;
        }*/
    }

    return status;
}

int FPCameraHelper::overrideResult(uint32_t frameNumber, CameraMetadata& result) {
    std::shared_ptr<ITctCameraAlgoService> tctCameraAlgoService = getAlgoService();
    if (tctCameraAlgoService == nullptr) {
        ALOGW("%s: Failed to access TctCameraAlgoService, in frameNumber:%u", __FUNCTION__, frameNumber);
        return 0;
    }
    if (frameNumber <= DUMMY_FRAME) {
        ALOGW("%s: TctCameraAlgoService dummy frameNumber:%u", __FUNCTION__, frameNumber);
        return 0;
    }
    camera_metadata_t* meta = const_cast<camera_metadata_t*>(result.getAndLock());

    CameraMetadatas settings;
    CameraMetadatas metaOut;
    uint32_t size = get_camera_metadata_size(meta);
    ALOGD("%s: frameNumber:%u, result meta size:%" PRIu32, __FUNCTION__, frameNumber, size);
    uint8_t* aidlCharsP = reinterpret_cast<uint8_t*>(const_cast<camera_metadata_t*>(meta));
    settings.metadata.assign(aidlCharsP, aidlCharsP + get_camera_metadata_size(meta));
    result.unlock(meta);
    auto status = tctCameraAlgoService->updateAndQueryResultMetadata(frameNumber, settings, &metaOut);
    if (!status.isOk()) {
        ALOGE("%s: updateAndQueryResultMetadata failed, frameNumber:%u size:%" PRIu32, __FUNCTION__, frameNumber, size);
        return -1;
    }
    int32_t err = 0;
    err = MergeMetaData(reinterpret_cast<const camera_metadata_t*>(metaOut.metadata.data()), result);
    if (err != OK) {
        ALOGE("%s: __merge_metadata failed, err:%d frameNumber:%u size:%" PRIu32, __FUNCTION__, err, frameNumber, size);
    }

    return 0;
}

// FrameSynchronizer
//
auto FrameSynchronizer::getInstance() -> std::shared_ptr<FrameSynchronizer> {
    static std::weak_ptr<FrameSynchronizer> wpInstance;
    static std::mutex gInstanceMutex;
    std::lock_guard lock { gInstanceMutex };
    auto instance = wpInstance.lock();
    if (instance == nullptr) {
        instance = std::make_shared<FrameSynchronizer>();
        if (instance != nullptr)
            wpInstance = instance;
        else
            ALOGE("%s: Failed to create instance of FrameSynchronizer!", __FUNCTION__);
    }
    return instance;
}

FrameSynchronizer::FrameSynchronizer() :mIsInit(false), mMainStreamId(-1) {}

FrameSynchronizer::~FrameSynchronizer() { uninitialize(); };

void FrameSynchronizer::initialize(const std::initializer_list<StreamInfo>& configs) {
    std::lock_guard lock { mMutex };
    mStreamCount = configs.size();
    mMainStreamId = configs.begin()->first;
    auto it = configs.begin();
    for (size_t i = 0; i != configs.size(); ++i) {
        // stream id list must be a incremental sequence like { mMainStreamId, mMainStreamId + 1, mMainStreamId + 2... }
        if ((it->first - mMainStreamId) != static_cast<int32_t>(i)) {
            ALOGE("%s: stream id not correct", __FUNCTION__);
            mRegisteredSurfaces.clear();
            mIsInit = false;
            return ;
        }
        mRegisteredSurfaces[it->first] = it->second;
        ++it;
    }
    mIsInit = true;
}

void FrameSynchronizer::uninitialize() {
    std::lock_guard lock { mMutex };
    for (auto it = mCachedFrames.begin(); it != mCachedFrames.end();) {
        releaseFrames(std::move(it->second));
        it = mCachedFrames.erase(it);
    }
}

bool FrameSynchronizer::needFrameSync(int32_t streamId) {
    std::lock_guard lock { mMutex };
    return mIsInit && (mMainStreamId >= 0) && (mRegisteredSurfaces.count(streamId) >= 1);
}

bool FrameSynchronizer::pushFrame(uint64_t frameNumber, int32_t streamId, BufferHolder&& frame) {
    ALOGV("%s : frameNum %" PRIu64 ", streamId %d", __FUNCTION__, frameNumber, streamId);
    if (mRegisteredSurfaces.count(streamId) == 0) {
        ALOGE("%s: unregistered stream %u !", __FUNCTION__, streamId);
        return false;
    }
    if (std::lock_guard lock { mMutex }; mCachedFrames.count(frameNumber) != 0) {
        if (mCachedFrames[frameNumber].count(streamId) != 0) {
            releaseFrames( {
                { streamId, frame }
            } );
            ALOGE("%s: Some error happen! Frame %" PRIu64 " has been cached for stream %u", __FUNCTION__, frameNumber, streamId);
            return false;
        }
        mCachedFrames[frameNumber][streamId] = frame;
    } else {
        if (mDroppedFrameCounts.count(frameNumber) != 0) {
            ALOGI("%s: Drop frame %" PRIu64 " for stream %u", __FUNCTION__, frameNumber, streamId);
            releaseFrames( {
                { streamId, frame }
            } );
            if (--mDroppedFrameCounts[frameNumber] == 0)
                mDroppedFrameCounts.erase(frameNumber);
            return false;
        } else {
            mCachedFrames[frameNumber] = {};
            mCachedFrames[frameNumber][streamId] = frame;
        }
    }
    mFrameReadyCond.notify_all();
    return true;
}

auto FrameSynchronizer::popFrames(uint64_t frameNumber) -> FrameMap {
    static constexpr auto FRAME_WAIT_TIMEOUT = std::chrono::milliseconds(5);
    FrameMap ready_frames;
    // found enough frames for frameNumber
    if (std::unique_lock lock { mMutex }; mCachedFrames.count(frameNumber) != 0 && mCachedFrames[frameNumber].size() == mStreamCount) {
        ALOGV("%s : successful pop frameNum %" PRIu64 "", __FUNCTION__, frameNumber);
        ready_frames = std::move(mCachedFrames[frameNumber]);
        mCachedFrames.erase(frameNumber);
    } else {
        // wait for frames available
        auto is_frame_ready = mFrameReadyCond.wait_for(lock, FRAME_WAIT_TIMEOUT, [&] {
            return mCachedFrames.count(frameNumber) != 0 && mCachedFrames[frameNumber].size() == mStreamCount;
        });
        if (is_frame_ready) {
            ALOGV("%s : successful pop frameNum %" PRIu64 "", __FUNCTION__, frameNumber);
            ready_frames = std::move(mCachedFrames[frameNumber]);
            mCachedFrames.erase(frameNumber);
        } else {
            ALOGW("%s : Time out! Can't get enough frames for frameNum %" PRIu64 ", drop it", __FUNCTION__, frameNumber);
            // wait time out, drop all cached frames
            auto drop_frames = (mCachedFrames.count(frameNumber) != 0) ? std::move(mCachedFrames[frameNumber]) : FrameMap {};
            auto missing_frame_count = static_cast<uint32_t>(mStreamCount - drop_frames.size());
            mDroppedFrameCounts.insert({ frameNumber, missing_frame_count });
            releaseFrames(std::move(drop_frames));
        }
    }
    return ready_frames;
}

// unlock operation!!
void FrameSynchronizer::releaseFrames(FrameMap&& frameMap) {
    for (auto& elem : frameMap) {
        if (mRegisteredSurfaces.count(elem.first) == 0) {
            ALOGE("%s: surface not found for stream %u!", __FUNCTION__, elem.first);
            continue;
        }
        auto& surface = mRegisteredSurfaces[elem.first];
        auto& frame = elem.second;
        static_cast<ANativeWindow* >(surface.get())->queueBuffer(surface.get(), frame.anwBuffer.get(), frame.anwReleaseFence);
    }
}

}

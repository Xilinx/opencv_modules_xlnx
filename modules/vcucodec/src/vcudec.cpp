/*
   Copyright (c) 2025  Advanced Micro Devices, Inc. (AMD)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "vcudec.hpp"

extern "C" {
#include "config.h"
#include "lib_common/PicFormat.h"
#include "lib_common/PixMapBuffer.h"

#include "lib_decode/lib_decode.h"
}

#include "opencv2/core/utils/logger.hpp"
#include "opencv2/imgproc.hpp"
#include "private/vcuutils.hpp"


#include <thread>
namespace cv {
namespace vcucodec {
namespace { // anonymous

const int fourcc_BGR = VideoWriter::fourcc('B', 'G', 'R', ' ');
const int fourcc_BGRA = VideoWriter::fourcc('B', 'G', 'R', 'A');

} // anonymous namespace

VCUDecoder::VCUDecoder(const String& filename, const DecoderInitParams& params)
    : filename_(filename), params_(params), rawOutput_(RawOutput::create())
{
    // VCU2 initialization will be implemented when VCU2 Control Software is available
    CV_LOG_INFO(NULL, "VCU2 Decoder initialized");
    vcu2_available_ = true;

    std::shared_ptr<Config> pDecConfig = std::shared_ptr<Config>(new Config());
    pDecConfig->sIn = (std::string)filename;
    if (params_.szReturnQueue > 0) {
        pDecConfig->uNumBuffersHeldByNextComponent = params_.szReturnQueue;
    }

    switch(params_.codec)
    {
    // note pixel FOURCC is returned in property CAP_PROP_CODEC_PIXEL_FORMAT (not CAP_PROP_FOURCC)
    case Codec::AVC:
        pDecConfig->tDecSettings.eCodec = AL_CODEC_AVC;
        setCaptureProperty(CAP_PROP_FOURCC, FOURCC(H264), false);
        break;
    case Codec::HEVC:
        pDecConfig->tDecSettings.eCodec = AL_CODEC_HEVC;
        setCaptureProperty(CAP_PROP_FOURCC, FOURCC(HEVC), false);
        break;
    case Codec::JPEG:
        pDecConfig->tDecSettings.eCodec = AL_CODEC_JPEG;
        setCaptureProperty(CAP_PROP_FOURCC, FOURCC(MJPG), false);
        break;
    default:
        CV_Error(cv::Error::StsBadArg, "Unsupported codec type");
    }

    if (params.fourcc == 0 || params.fourcc == FOURCC(AUTO))
    {
        pDecConfig->tOutputFourCC = FOURCC(NULL);
    }
    else
    {
        pDecConfig->tOutputFourCC = params.fourcc;
    }

    if (params_.maxFrames > 0)
        pDecConfig->iMaxFrames = params_.maxFrames;

    pDecConfig->iOutputBitDepth = static_cast<int>(params_.bitDepth);

    rawInfo_.eos = true; // use as uninitialized indicator
    decodeCtx_ = DecContext::create(pDecConfig, rawOutput_, wCfg);
    initialized_ = decodeCtx_ != nullptr;
    if (!initialized_)
    {
        CV_Error(cv::Error::StsError, "VCU2 decoder initialization failed");
    }
    setCaptureProperty(CAP_PROP_POS_FRAMES, frameIndex_, true);
}

VCUDecoder::~VCUDecoder()
{
    CV_LOG_DEBUG(NULL, "VCUDecoder destructor called");
    cleanup();
}

bool VCUDecoder::nextFrame(OutputArray frame, RawInfo& frame_info) /* override */
{
    Ptr<Frame> pFrame = nullptr;

    if (!vcu2_available_ || !initialized_)
    {
        CV_LOG_DEBUG(NULL, "VCU2 not available or not initialized");
        return false;
    }

    if(!decodeCtx_)
        return false;

    if(!decodeCtx_->running())
    {
        decodeCtx_->start(wCfg);
    }

    if(decodeCtx_->eos())
    {
        pFrame = rawOutput_->dequeue(std::chrono::milliseconds::zero());
    } else {
        pFrame = rawOutput_->dequeue(std::chrono::milliseconds(100));
    }

    frame_info.eos = false;
    if(pFrame)
    {
        retrieveVideoFrame(frame, pFrame, frame_info, false, false);
        setCaptureProperty(CAP_PROP_POS_FRAMES, ++frameIndex_, true);
    } else  {
        if(decodeCtx_->eos())
        {
            frame_info.eos = true;
            decodeCtx_->finish();
        }
        return false;
    }

    return true;
}

bool VCUDecoder::nextFramePlanes(OutputArrayOfArrays planes, RawInfo& frame_info, bool byRef)
{
    Ptr<Frame> pFrame = nullptr;

    if (!vcu2_available_ || !initialized_)
    {
        CV_LOG_WARNING(NULL, "VCU2 not available or not initialized");
        return false;
    }

    if(!decodeCtx_)
        return false;

    if (byRef && params_.szReturnQueue < 1)
    {
        CV_LOG_WARNING(NULL, "szReturnQueue must be >= 1 when byRef is true");
        byRef = false;
    }

    if(!decodeCtx_->running())
    {
        decodeCtx_->start(wCfg);
    }

    CV_LOG_DEBUG(NULL, "VCU2 nextFramePlanes called (placeholder implementation)");
    if(decodeCtx_->eos())
    {
        pFrame = rawOutput_->dequeue(std::chrono::milliseconds::zero());
    } else {
        pFrame = rawOutput_->dequeue(std::chrono::milliseconds(100));
    }

    frame_info.eos = false;
    if(pFrame)
    {
        retrieveVideoFrame(planes, pFrame, frame_info, true, byRef);
        setCaptureProperty(CAP_PROP_POS_FRAMES, ++frameIndex_, true);
    } else  {
        if(decodeCtx_->eos())
        {
            frame_info.eos = true;
            decodeCtx_->finish();
        }
        return false;
    }

    return true;

}

bool VCUDecoder::set(int propId, double value)
{
    bool result = false;
    if (propId < CV__CAP_PROP_LATEST)
    {
        result = setCaptureProperty(propId, value, true);
    }
    return result;
}

double VCUDecoder::get(int propId) const {
    double result = 0.0;
    if (propId < CV__CAP_PROP_LATEST)
    {
        result = getCaptureProperty(propId);
    }
    return result; // Placeholder implementation
}

String VCUDecoder::streamInfo() const {
    return decodeCtx_ ? decodeCtx_->streamInfo() : String();
}

String VCUDecoder::statistics() const {
    return decodeCtx_ ? decodeCtx_->statistics() : String();
}


void VCUDecoder::cleanup()
{
    if (vcu2_available_ && initialized_)
    {
        decodeCtx_->finish();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        AL_Lib_Decoder_DeInit();
    }
    initialized_ = false;
}

void VCUDecoder::copyToDestination(OutputArray dst, std::vector<Mat>& src,
    int fourccConvert, bool vector_output, bool single_output_buffer, bool by_reference)
{
    int nr_components = src.size();
    std::vector<Mat> planes;
    planes.resize(nr_components);
    Mat& planeY = planes[0];
    Mat& planeU = planes[1];
    Mat& planeV = planes[2];
    Mat& planeUV = planes[1];
    Mat& planeRGB = planes[0];
    Mat& planeYUV = planes[0];

    if (nr_components == 1) {
        Mat& srcY = src[0];
        Size szY = src[0].size();

        if (fourccConvert == fourcc_BGR)
        {
            planeRGB.create(szY, CV_8UC3);
            cvtColor(srcY, planeRGB, COLOR_GRAY2BGR);
        }
        else if (fourccConvert == fourcc_BGRA)
        {
            planeRGB.create(szY, CV_8UC4);
            cvtColor(srcY, planeRGB, COLOR_GRAY2BGRA);
        }
        else
        {
            if (by_reference)
            {
                planeY = srcY;
            }
            else
            {
                srcY.copyTo(dst);
            }
        }
    }
    else if (nr_components == 2)
    {
        Mat& srcY = src[0];
        Mat& srcUV = src[1];
        Size szY = src[0].size();
        Size szUV = src[1].size();
        if (fourccConvert == fourcc_BGR)
        {
            planeRGB.create(szY, CV_8UC3);
            cvtColorTwoPlane(srcY, srcUV, planeRGB, COLOR_YUV2BGR_NV12);
        }
        else if (fourccConvert == fourcc_BGRA)
        {
            planeRGB.create(szY, CV_8UC4);
            cvtColorTwoPlane(srcY, srcUV, planeRGB, COLOR_YUV2BGRA_NV12);
        }
        else
        {
            if (by_reference)
            {
                planeY = srcY;
                planeUV = srcUV;
            }
            else if (single_output_buffer)
            {
                planeYUV.create(Size(szY.width, szY.height + szUV.height), CV_8UC1);
                srcY.copyTo(planeYUV(Rect(0, 0, szY.width, szY.height)));
                srcUV.reshape(1, srcUV.rows).copyTo(planeYUV(Rect(0, szY.height, szUV.width*2,
                    szUV.height)));
            } else {
                srcY.copyTo(planeY);
                srcUV.copyTo(planeUV);
            }
        }
    }
    else if (nr_components == 3)
    {
        Mat& srcY = src[0];
        Mat& srcU = src[1];
        Mat& srcV = src[2];
        Size szY = src[0].size();
        Size szU = src[1].size();
        Size szV = src[2].size();
        if (fourccConvert == fourcc_BGR)
        {
            planeRGB.create(szY, CV_8UC3);
            Mat imgYUVpacked;
            merge(src, imgYUVpacked); // expensive extra copy
            cvtColor(imgYUVpacked, planeRGB, COLOR_YUV2BGR);
        }
        else if (fourccConvert == fourcc_BGRA)
        {
            planeRGB.create(szY, CV_8UC4);
            Mat imgYUVpacked;
            merge(src, imgYUVpacked); // expensive extra copy
            Mat imgBGR;
            imgBGR.create(szY, CV_8UC3);
            cvtColor(imgYUVpacked, imgBGR, COLOR_YUV2BGR); // another expensive extra copy
            cvtColor(imgBGR, planeRGB, COLOR_BGR2BGRA);
        }
        else
        {
            if (by_reference)
            {
                planeY = srcY;
                planeU = srcU;
                planeV = srcV;
            }
            else if (single_output_buffer)
            {
                planeYUV.create(Size(szY.width, szY.height * 3), CV_8UC1);
                srcY.copyTo(planeYUV(Rect(0, 0, szY.width, szY.height)));
                srcU.copyTo(planeYUV(Rect(0, szY.height, szU.width, szU.height)));
                srcV.copyTo(planeYUV(Rect(0, szY.height + szU.height, szV.width, szV.height)));
            }
            else
            {
                srcY.copyTo(planeY);
                srcU.copyTo(planeU);
                srcV.copyTo(planeV);
            }
        }
    }

    if (vector_output)
    {
        int sizes[] = {nr_components};  // n planes
        dst.create(1, sizes, CV_8UC1);
        dst.assign(planes);
    }
    else
    {
        dst.assign(planes[0]);
    }

}

void VCUDecoder::retrieveVideoFrame(OutputArray dst, Ptr<Frame> frame, RawInfo& frame_info,
                                    bool vector_output, bool by_reference)
{
    AL_TBuffer* pFrame = frame->getBuffer();
    frame->rawInfo(frame_info);
    //for 1080p HEVC decode, output height is 1080 with zero crop numbers
    //for 1080p AVC decode, output height is 1088 with crop numbers, update width/height
    frame_info.width -= frame_info.cropLeft + frame_info.cropRight;
    frame_info.height -= frame_info.cropTop + frame_info.cropBottom;
    updateRawInfo(frame_info);
    switch(frame_info.fourcc)
    {
    case FOURCC(Y800):
    {
        Size sz = Size(frame_info.width, frame_info.height);
        size_t step = frame_info.width;
        std::vector<Mat> src =
            { Mat(sz, CV_8UC1, AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), step) };
        bool single_output_buffer = true;
        copyToDestination(dst, src, params_.fourccConvert, vector_output, single_output_buffer,
                          by_reference);
        break;
    }
    case (FOURCC(NV12)):
    {
        Size szY = Size(frame_info.width, frame_info.height);
        Size szUV = Size(frame_info.width / 2, frame_info.height / 2);
        size_t stepY = frame_info.stride;
        size_t stepUV = frame_info.stride;
        std::vector<Mat> src =
            { Mat(szY,  CV_8UC1, AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), stepY),
              Mat(szUV, CV_8UC2, AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_UV), stepUV) };
        bool single_output_buffer = !vector_output;
        copyToDestination(dst, src, params_.fourccConvert, vector_output, single_output_buffer,
                          by_reference);
        break;
    }
    case (FOURCC(NV16)):
    {
        Size szY = Size(frame_info.width, frame_info.height);
        Size szUV = Size(frame_info.width / 2, frame_info.height);
        size_t stepY = frame_info.stride;
        size_t stepUV = frame_info.stride;
        std::vector<Mat> src =
            { Mat(szY,  CV_8UC1, AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_Y), stepY),
              Mat(szUV, CV_8UC2, AL_PixMapBuffer_GetPlaneAddress(pFrame, AL_PLANE_UV), stepUV) };
        bool single_output_buffer = !vector_output;
        copyToDestination(dst, src, params_.fourccConvert, vector_output, single_output_buffer,
                          by_reference);
        break;
    }
    default:
        CV_Error(Error::StsUnsupportedFormat, "Unsupported pixel format");
        break;
    } // end switch
}

/*
CAP_PROP_FRAME_TYPE
CAP_PROP_SAR_NUM
CAP_PROP_SAR_DEN
CAP_PROP_BITRATE
CAP_PROP_FRAME_TYPE (I,P,B)
*/


void VCUDecoder::updateRawInfo(RawInfo& frame_info)
{
    bool changed = frame_info != rawInfo_; // false if any has .eos member set
    if (changed)
    {
        std::lock_guard<std::mutex> lock(rawInfoMutex_);
        if (rawInfo_.eos || rawInfo_.fourcc != frame_info.fourcc)
        {
            setCaptureProperty(CAP_PROP_CODEC_PIXEL_FORMAT, (double)frame_info.fourcc, false);
        }
        if (rawInfo_.eos || rawInfo_.width != frame_info.width) {
            setCaptureProperty(CAP_PROP_FRAME_WIDTH, (double)frame_info.width, false);
        }
        if (rawInfo_.eos || rawInfo_.height != frame_info.height) {
            setCaptureProperty(CAP_PROP_FRAME_HEIGHT, (double)frame_info.height, false);
        }
        rawInfo_ = frame_info;
    }
}

bool VCUDecoder::setCaptureProperty(int propId, double value, bool external)
{
    std::lock_guard<std::mutex> lock(capturePropertiesMutex_);

    bool result = true;

    if (!external)
    {
        captureProperties_[propId] = value;
    }
    else
    {
        switch (propId)
        {
            // supported to be set by external client:

        case CAP_PROP_FPS:
            captureProperties_[propId] = value;
            break;

        default:
            result = false;
            break;
        }
    }
    return result;
}

double VCUDecoder::getCaptureProperty(int propId) const
{
    double result = 0.0;
    std::lock_guard<std::mutex> lock(capturePropertiesMutex_);
    auto it = captureProperties_.find(propId);
    if (it != captureProperties_.end())
    {
        result = it->second;
    }
    return result;
}


} // namespace vcucodec
} // namespace cv

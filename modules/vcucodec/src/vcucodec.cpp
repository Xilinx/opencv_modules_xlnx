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
#include "opencv2/vcucodec.hpp"

#include "vcudec.hpp"
#include "vcuenc.hpp"

#include "opencv2/core/utils/logger.hpp"

#include <map>

namespace cv {
namespace vcucodec {

Ptr<Decoder> createDecoder(const String& filename, const DecoderInitParams& params)
{
    Ptr<Decoder> decoder;
    try {
        decoder = makePtr<VCUDecoder>(filename, params);
    }
    catch (const cv::Exception& e) {
        throw;
    }
    catch (const std::exception& e) {
        CV_Error(cv::Error::StsError, "Error creating VCUDecoder");
    }
    return decoder;
}

Ptr<Encoder> createEncoder(const String& filename, const EncoderInitParams& params,
    Ptr<EncoderCallback> callback)
{
    try {
        Ptr<Encoder> encoder = makePtr<VCUEncoder>(filename, params, callback);
        return encoder;
    } catch (const std::exception& e) {
        CV_LOG_ERROR(NULL, "Error creating VCUEncoder: ");
        return {};
    }
}

}  // namespace vcucodec
}  // namespace cv
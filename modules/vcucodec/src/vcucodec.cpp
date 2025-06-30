#include "opencv2/vcucodec.hpp"

#include "vcudec.hpp"
#include "vcuenc.hpp"

#include <map>



namespace cv {
namespace vcucodec {

Ptr<Decoder> createDecoder(const String& filename, const DecoderInitParams& params)
{
    return makePtr<VCUDecoder>(filename, params);
}

Ptr<Encoder> createEncoder(const String& filename, const EncoderInitParams& params)
{
    Ptr<VCUEncoder> encoder = makePtr<VCUEncoder>(filename, params);
    return encoder;
}

}  // namespace vcucodec
}  // namespace cv
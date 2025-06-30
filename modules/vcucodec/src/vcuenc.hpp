#include "opencv2/vcucodec.hpp"

namespace cv {
namespace vcucodec {

class VCUEncoder : public Encoder
{
public:
    virtual ~VCUEncoder();
    VCUEncoder(const String& filename, const EncoderInitParams& params);

    virtual void write(InputArray frame) override;
    virtual bool set(int propId, double value) override;
    virtual double get(int propId) const override;

private:
    String filename_;
    EncoderInitParams params_;
};

}  // namespace vcucodec
}  // namespace cv
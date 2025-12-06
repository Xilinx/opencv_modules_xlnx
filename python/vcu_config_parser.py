import re

# Try to import VCU codec
try:
    import cv2
    import cv2.vcucodec as vcu
    VCU_AVAILABLE = True
except ImportError:
    cv2 = None
    vcu = None
    VCU_AVAILABLE = False

class ParameterParser:
    units = {
        'K': 1000, 'Ki': 1024, 'M': 1000000, 'Mi': 1048576, 'G': 1000000000, 'Gi': 1073741824,
    }

    @staticmethod
    def parse_value(value_str):
        value_str = value_str.strip()
        if not value_str:
            return ""

        # Try simple number with unit
        match = re.match(r'^([+-]?)([0-9]*\.?[0-9]+)([KMG]i?)?$', value_str)
        if match:
            sign, number_str, unit = match.groups()
            value = float(number_str) if '.' in number_str else int(number_str)
            if sign == '-':
                value = -value
            if unit:
                value *= ParameterParser.units[unit]
                if isinstance(value, float) and value.is_integer():
                    value = int(value)
            return value

        # Try boolean
        lower_val = value_str.lower()
        if lower_val in ['true', 'false', 'enable', 'disable', 'yes', 'no']:
            return lower_val in ['true', 'enable', 'yes', '1']

        return value_str

class VCUConfigParser:
    def __init__(self):
        self.sections = {}
        self.current_section = None

    def parse(self, config_file_path):
        """Parse configuration from a file"""
        with open(config_file_path, 'r') as f:
            config_string = f.read()
        return self.parse_string(config_string)

    def parse_string(self, config_string):
        lines = config_string.strip().split('\n')
        self.sections = {}
        self.current_section = None

        for line_num, line in enumerate(lines, 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            section_match = re.match(r'^\[([^\]]+)\]', line)
            if section_match:
                self.current_section = section_match.group(1).upper()
                if self.current_section not in self.sections:
                    self.sections[self.current_section] = {}
                continue

            if '=' in line:
                line = line.split('#')[0].strip()
                if '=' in line:
                    key, value = line.split('=', 1)
                    key = key.strip()
                    value = value.strip()

                    if self.current_section is None:
                        raise ValueError("Parameter found outside of section")

                    parsed_value = ParameterParser.parse_value(value)
                    self.sections[self.current_section][key] = parsed_value

        return self.sections

    def create_encoder_params(self):
        """Create VCU encoder parameters from parsed configuration"""
        if not VCU_AVAILABLE:
            raise RuntimeError("cv2.vcucodec is not available")

        # Create VCU parameter objects
        picture_settings = vcu.PictureEncSettings()
        rc_settings = vcu.RCSettings()
        gop_settings = vcu.GOPSettings()
        profile_settings = vcu.ProfileSettings()
        motion_vector = vcu.GlobalMotionVector()

        # Configure INPUT section parameters
        if 'INPUT' in self.sections:
            input_data = self.sections['INPUT']
            if 'Width' in input_data:
                picture_settings.width = int(input_data['Width'])
            if 'Height' in input_data:
                picture_settings.height = int(input_data['Height'])
            if 'FrameRate' in input_data:
                picture_settings.framerate = int(input_data['FrameRate'])
            if 'Format' in input_data:
                picture_settings.fourcc = self._parse_format(input_data['Format'])

        # Configure RATE_CONTROL section parameters
        if 'RATE_CONTROL' in self.sections:
            rc_data = self.sections['RATE_CONTROL']
            if 'RateCtrlMode' in rc_data:
                rc_settings.mode = self._parse_rc_mode(rc_data['RateCtrlMode'])
            elif 'Mode' in rc_data:
                rc_settings.mode = self._parse_rc_mode(rc_data['Mode'])

            if 'BitRate' in rc_data:
                rc_settings.bitrate = int(rc_data['BitRate'])
            elif 'Bitrate' in rc_data:
                rc_settings.bitrate = int(rc_data['Bitrate'])

            if 'MaxBitRate' in rc_data:
                rc_settings.maxBitrate = int(rc_data['MaxBitRate'])
            elif 'MaxBitrate' in rc_data:
                rc_settings.maxBitrate = int(rc_data['MaxBitrate'])

            if 'CPBSize' in rc_data:
                rc_settings.cpbSize = int(rc_data['CPBSize'])
            if 'InitialDelay' in rc_data:
                rc_settings.initialDelay = int(rc_data['InitialDelay'])

            # Additional RC parameters
            if 'Entropy' in rc_data:
                rc_settings.entropy = self._parse_entropy(rc_data['Entropy'])
            if 'FillerData' in rc_data:
                rc_settings.fillerData = bool(rc_data['FillerData'])
            if 'MaxQualityTarget' in rc_data:
                rc_settings.maxQualityTarget = int(rc_data['MaxQualityTarget'])
            if 'MaxPictureSizeI' in rc_data:
                rc_settings.maxPictureSizeI = int(rc_data['MaxPictureSizeI'])
            if 'MaxPictureSizeP' in rc_data:
                rc_settings.maxPictureSizeP = int(rc_data['MaxPictureSizeP'])
            if 'MaxPictureSizeB' in rc_data:
                rc_settings.maxPictureSizeB = int(rc_data['MaxPictureSizeB'])
            if 'SkipFrame' in rc_data:
                rc_settings.skipFrame = bool(rc_data['SkipFrame'])
            if 'MaxSkip' in rc_data:
                rc_settings.maxSkip = int(rc_data['MaxSkip'])

        # Configure GOP section parameters
        if 'GOP' in self.sections:
            gop_data = self.sections['GOP']
            if 'GopCtrlMode' in gop_data:
                gop_settings.mode = self._parse_gop_mode(gop_data['GopCtrlMode'])
            elif 'Mode' in gop_data:
                gop_settings.mode = self._parse_gop_mode(gop_data['Mode'])

            if 'Gop.Length' in gop_data:
                gop_settings.gopLength = int(gop_data['Gop.Length'])

            if 'Gop.NumB' in gop_data:
                gop_settings.nrBFrames = int(gop_data['Gop.NumB'])

            # Additional GOP parameters
            if 'GDRMode' in gop_data:
                gop_settings.gdrMode = self._parse_gdr_mode(gop_data['GDRMode'])
            if 'LongTermRef' in gop_data:
                gop_settings.longTermRef = bool(gop_data['LongTermRef'])
            if 'LongTermFreq' in gop_data:
                gop_settings.longTermFreq = int(gop_data['LongTermFreq'])
            if 'PeriodIDR' in gop_data:
                gop_settings.periodIDR = int(gop_data['PeriodIDR'])

        # Configure SETTINGS section parameters
        if 'SETTINGS' in self.sections:
            settings_data = self.sections['SETTINGS']
            if 'Codec' in settings_data:
                picture_settings.codec = self._parse_codec(settings_data['Codec'])
            if 'Profile' in settings_data:
                profile_settings.profile = self._parse_profile(settings_data['Profile'])
            if 'Level' in settings_data:
                profile_settings.level = str(settings_data['Level'])
            if 'Tier' in settings_data:
                profile_settings.tier = self._parse_tier(settings_data['Tier'])

        # Configure MOTION_VECTOR section parameters
        if 'MOTION_VECTOR' in self.sections:
            mv_data = self.sections['MOTION_VECTOR']
            if 'FrameIndex' in mv_data:
                motion_vector.frameIndex = int(mv_data['FrameIndex'])
            if 'GMVectorX' in mv_data:
                motion_vector.gmVectorX = int(mv_data['GMVectorX'])
            if 'GMVectorY' in mv_data:
                motion_vector.gmVectorY = int(mv_data['GMVectorY'])

        # Create and return EncoderInitParams
        # In Python, we can assign the settings objects directly
        encoder_params = vcu.EncoderInitParams()
        encoder_params.pictureEncSettings = picture_settings
        encoder_params.rcSettings = rc_settings
        encoder_params.gopSettings = gop_settings
        encoder_params.profileSettings = profile_settings
        encoder_params.globalMotionVector = motion_vector

        return encoder_params

    def _parse_codec(self, codec_str):
        """Parse codec string to VCU codec enum"""
        codec_str = codec_str.upper()
        if 'AVC' in codec_str or 'H264' in codec_str or 'H.264' in codec_str:
            return vcu.CODEC_AVC
        elif 'HEVC' in codec_str or 'H265' in codec_str or 'H.265' in codec_str:
            return vcu.CODEC_HEVC
        else:
            return vcu.CODEC_AVC  # Default to AVC

    def _parse_format(self, format_str):
        """Parse video format string to VCU format"""
        format_str = format_str.upper()
        # Map common formats to VCU constants
        format_map = {
            'Y800': 0x30303859,  # fourcc('Y', '8', '0', '0')
            'Y010': 0x30313059,  # fourcc('Y', '0', '1', '0')
            'Y012': 0x32313059,  # fourcc('Y', '0', '1', '2')
            'I420': 0x30323449,  # fourcc('I', '4', '2', '0')
            'NV12': 0x3231564E,  # fourcc('N', 'V', '1', '2')
            'P010': 0x30313050,  # fourcc('P', '0', '1', '0')
            'P012': 0x32313050,  # fourcc('P', '0', '1', '2')
            'NV16': 0x3631564E,  # fourcc('N', 'V', '1', '6')
            'P210': 0x30313250,  # fourcc('P', '2', '1', '0')
            'P212': 0x32313250,  # fourcc('P', '2', '1', '2')
            'I444': 0x34343449,  # fourcc('I', '4', '4', '4')
            'I4AL': 0x4C413449,  # fourcc('I', '4', 'A', 'L')
            'I4CL': 0x4C433449,  # fourcc('I', '4', 'C', 'L')
        }
        return format_map.get(format_str, 0x30323449)  # Default to I420

    def _parse_rc_mode(self, mode_str):
        """Parse rate control mode string to VCU enum"""
        mode_str = mode_str.upper()
        if 'CBR' in mode_str:
            return vcu.RCMode_CBR
        elif 'VBR' in mode_str:
            return vcu.RCMode_VBR
        elif 'CONST_QP' in mode_str:
            return vcu.RCMode_CONST_QP
        elif 'LOW_LATENCY' in mode_str:
            return vcu.RCMode_LOW_LATENCY
        elif 'CAPPED_VBR' in mode_str:
            return vcu.RCMode_CAPPED_VBR
        else:
            return vcu.RCMode_CBR  # Default

    def _parse_gop_mode(self, mode_str):
        """Parse GOP mode string to VCU enum"""
        mode_str = mode_str.upper()
        if 'DEFAULT_GOP' in mode_str:
            return vcu.GOPMode_BASIC
        elif 'DEFAULT_GOP_B' in mode_str:
            return vcu.GOPMode_BASIC_B
        elif 'PYRAMIDAL_GOP' in mode_str:
            return vcu.GOPMode_PYRAMIDAL
        elif 'PYRAMIDAL_GOP_B' in mode_str:
            return vcu.GOPMode_PYRAMIDAL_B
        elif 'LOW_DELAY_P' in mode_str:
            return vcu.GOPMode_LOW_DELAY_P
        elif 'LOW_DELAY_B' in mode_str:
            return vcu.GOPMode_LOW_DELAY_B
        elif 'ADAPTIVE_GOP' in mode_str:
            return vcu.GOPMode_ADAPTIVE
        else:
            return vcu.GOPMode_BASIC  # Default

    def _parse_profile(self, profile_str):
        """Return profile string as-is for firmware parsing"""
        # Profile is passed as a string to the firmware and parsed in C++ code
        return str(profile_str)

    def _parse_entropy(self, entropy_str):
        """Parse entropy string to VCU enum"""
        entropy_str = entropy_str.upper()
        if 'CAVLC' in entropy_str:
            return vcu.Entropy_CAVLC
        elif 'CABAC' in entropy_str:
            return vcu.Entropy_CABAC
        else:
            return vcu.Entropy_CABAC  # Default

    def _parse_gdr_mode(self, gdr_str):
        """Parse GDR mode string to VCU enum"""
        gdr_str = gdr_str.upper()
        if 'DISABLE' in gdr_str:
            return vcu.GDRMode_DISABLE
        elif 'VERTICAL' in gdr_str:
            return vcu.GDRMode_VERTICAL
        elif 'HORIZONTAL' in gdr_str:
            return vcu.GDRMode_HORIZONTAL
        else:
            return vcu.GDRMode_DISABLE  # Default

    def _parse_tier(self, tier_str):
        """Parse tier string to VCU enum"""
        tier_str = tier_str.upper()
        if 'MAIN' in tier_str:
            return vcu.Tier_MAIN
        elif 'HIGH' in tier_str:
            return vcu.Tier_HIGH
        else:
            return vcu.Tier_MAIN  # Default

    @staticmethod
    def get_supported_sections():
        return {
            'INPUT': ['YUVFile', 'Width', 'Height', 'Format', 'FrameRate'],
            'OUTPUT': ['BitstreamFile', 'RecFile', 'Width', 'Height'],
            'GOP': ['GopCtrlMode', 'Gop.Length', 'Gop.NumB', 'GDRMode', 'LongTermRef', 'LongTermFreq', 'PeriodIDR'],
            'RATE_CONTROL': ['RateCtrlMode', 'BitRate', 'MaxBitRate', 'FrameRate', 'CPBSize', 'InitialDelay',
                           'Entropy', 'FillerData', 'MaxQualityTarget', 'MaxPictureSizeI', 'MaxPictureSizeP',
                           'MaxPictureSizeB', 'SkipFrame', 'MaxSkip'],
            'SETTINGS': ['Profile', 'Level', 'ChromaMode', 'BitDepth', 'Tier'],
            'MOTION_VECTOR': ['FrameIndex', 'GMVectorX', 'GMVectorY'],
            'HARDWARE': ['LookAheadDepth', 'NumSlices', 'EnableConstrainedIntraPrediction'],
            'QUALITY': ['QP', 'MinQP', 'MaxQP', 'QualityLevel'],
            'RUN': ['Loop', 'MaxPicture', 'RateCtrlStats']
        }

    @staticmethod
    def get_supported_formats():
        return ['Y800', 'Y010', 'Y012', 'I420', 'NV12', 'P010', 'P012', 'NV16', 'P210', 'P212', 'I444', 'I4AL', 'I4CL', 'HEVC_MAIN', 'AVC_MAIN']

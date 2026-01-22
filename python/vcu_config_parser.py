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
                    key = key.strip().lower()
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
            if 'width' in input_data:
                picture_settings.width = int(input_data['width'])
            if 'height' in input_data:
                picture_settings.height = int(input_data['height'])
            if 'framerate' in input_data:
                picture_settings.framerate = int(input_data['framerate'])
            if 'format' in input_data:
                picture_settings.fourcc = self._parse_format(input_data['format'])

        # Configure RATE_CONTROL section parameters
        if 'RATE_CONTROL' in self.sections:
            rc_data = self.sections['RATE_CONTROL']
            if 'ratectrlmode' in rc_data:
                rc_settings.mode = self._parse_rc_mode(rc_data['ratectrlmode'])

            if 'bitrate' in rc_data:
                rc_settings.bitrate = int(rc_data['bitrate'])

            if 'maxbitrate' in rc_data:
                rc_settings.maxBitrate = int(rc_data['maxbitrate'])

            if 'cpbsize' in rc_data:
                rc_settings.cpbSize = int(float(rc_data['cpbsize']) * 1000)  # seconds to ms
            if 'initialdelay' in rc_data:
                rc_settings.initialDelay = int(float(rc_data['initialdelay']) * 1000)  # seconds to ms

            # Additional RC parameters
            if 'maxpsnr' in rc_data:
                max_psnr = float(rc_data['maxpsnr'])
                if max_psnr < 20.0 or max_psnr > 50.0:
                    raise ValueError(f"MaxPSNR must be in range [20.0, 50.0], got {max_psnr}")
                rc_settings.maxQualityTarget = int(max_psnr - 28)
            if 'maxpicturesize.i' in rc_data:
                rc_settings.maxPictureSizeI = int(rc_data['maxpicturesize.i'])
            if 'maxpicturesize.p' in rc_data:
                rc_settings.maxPictureSizeP = int(rc_data['maxpicturesize.p'])
            if 'maxpicturesize.b' in rc_data:
                rc_settings.maxPictureSizeB = int(rc_data['maxpicturesize.b'])
            if 'enableskipframe' in rc_data:
                rc_settings.skipFrame = bool(rc_data['enableskipframe'])
            if 'maxconsecutiveskip' in rc_data:
                rc_settings.maxSkip = int(rc_data['maxconsecutiveskip'])

        # Configure GOP section parameters
        if 'GOP' in self.sections:
            gop_data = self.sections['GOP']
            if 'gopctrlmode' in gop_data:
                gop_settings.mode = self._parse_gop_mode(gop_data['gopctrlmode'])

            if 'gop.length' in gop_data:
                gop_settings.gopLength = int(gop_data['gop.length'])

            if 'gop.numb' in gop_data:
                gop_settings.nrBFrames = int(gop_data['gop.numb'])

            # Additional GOP parameters
            if 'gop.gdrmode' in gop_data:
                gop_settings.gdrMode = self._parse_gdr_mode(gop_data['gop.gdrmode'])
            if 'gop.enablelt' in gop_data:
                gop_settings.longTermRef = bool(gop_data['gop.enablelt'])
            if 'gop.freqlt' in gop_data:
                gop_settings.longTermFreq = int(gop_data['gop.freqlt'])
            if 'gop.freqidr' in gop_data:
                gop_settings.periodIDR = int(gop_data['gop.freqidr'])

        # Configure SETTINGS section parameters
        if 'SETTINGS' in self.sections:
            settings_data = self.sections['SETTINGS']
            if 'profile' in settings_data:
                profile_settings.profile = self._parse_profile(settings_data['profile'])
            if 'level' in settings_data:
                profile_settings.level = str(settings_data['level'])
            if 'tier' in settings_data:
                profile_settings.tier = self._parse_tier(settings_data['tier'])
            if 'entropymode' in settings_data:
                rc_settings.entropy = self._parse_entropy(settings_data['entropymode'])
            if 'enablefillerdata' in rc_data:
                rc_settings.fillerData = bool(rc_data['enablefillerdata'])

        # Configure MOTION_VECTOR section parameters
        if 'MOTION_VECTOR' in self.sections:
            mv_data = self.sections['MOTION_VECTOR']
            if 'frameindex' in mv_data:
                motion_vector.frameIndex = int(mv_data['frameindex'])
            if 'gmvectorx' in mv_data:
                motion_vector.gmVectorX = int(mv_data['gmvectorx'])
            if 'gmvectory' in mv_data:
                motion_vector.gmVectorY = int(mv_data['gmvectory'])

        # Create and return EncoderInitParams
        encoder_params = vcu.EncoderInitParams()
        encoder_params.pictureEncSettings = picture_settings
        encoder_params.rcSettings = rc_settings
        encoder_params.gopSettings = gop_settings
        encoder_params.profileSettings = profile_settings
        encoder_params.globalMotionVector = motion_vector

        return encoder_params

        return encoder_params

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
        """Parse rate control mode string to VCU enum.
        Accepts: CONST_QP, CBR, VBR, CAPPED_VBR, LOW_LATENCY, PLUGIN
        """
        mode_str = mode_str.upper()
        if mode_str == 'CONST_QP':
            return vcu.RCMode_CONST_QP
        elif mode_str == 'CBR':
            return vcu.RCMode_CBR
        elif mode_str == 'VBR':
            return vcu.RCMode_VBR
        elif mode_str == 'CAPPED_VBR':
            return vcu.RCMode_CAPPED_VBR
        elif mode_str == 'LOW_LATENCY':
            return vcu.RCMode_LOW_LATENCY
        else:
            raise ValueError(f"Invalid RateCtrlMode '{mode_str}'. Must be CONST_QP, CBR, VBR, CAPPED_VBR, or LOW_LATENCY")

    def _parse_gop_mode(self, mode_str):
        """Parse GOP mode string to VCU enum.
        Accepts: DEFAULT_GOP, DEFAULT_GOP_B, PYRAMIDAL_GOP, PYRAMIDAL_GOP_B,
                 LOW_DELAY_P, LOW_DELAY_B, ADAPTIVE_GOP
        """
        mode_str = mode_str.upper()
        if mode_str == 'DEFAULT_GOP':
            return vcu.GOPMode_BASIC
        elif mode_str == 'DEFAULT_GOP_B':
            return vcu.GOPMode_BASIC_B
        elif mode_str == 'PYRAMIDAL_GOP':
            return vcu.GOPMode_PYRAMIDAL
        elif mode_str == 'PYRAMIDAL_GOP_B':
            return vcu.GOPMode_PYRAMIDAL_B
        elif mode_str == 'LOW_DELAY_P':
            return vcu.GOPMode_LOW_DELAY_P
        elif mode_str == 'LOW_DELAY_B':
            return vcu.GOPMode_LOW_DELAY_B
        elif mode_str == 'ADAPTIVE_GOP':
            return vcu.GOPMode_ADAPTIVE
        else:
            raise ValueError(f"Invalid GopCtrlMode '{mode_str}'. Must be DEFAULT_GOP, DEFAULT_GOP_B, PYRAMIDAL_GOP, PYRAMIDAL_GOP_B, LOW_DELAY_P, LOW_DELAY_B, or ADAPTIVE_GOP")

    def _parse_profile(self, profile_str):
        """Return profile string as-is for firmware parsing"""
        # Profile is passed as a string to the firmware and parsed in C++ code
        return str(profile_str)

    def _parse_entropy(self, entropy_str):
        """Parse entropy mode string to VCU enum.
        Accepts: MODE_CAVLC, MODE_CABAC
        """
        entropy_str = entropy_str.upper()
        if entropy_str == 'MODE_CAVLC':
            return vcu.Entropy_CAVLC
        elif entropy_str == 'MODE_CABAC':
            return vcu.Entropy_CABAC
        else:
            raise ValueError(f"Invalid EntropyMode '{entropy_str}'. Must be MODE_CABAC or MODE_CAVLC")

    def _parse_gdr_mode(self, gdr_str):
        """Parse GDR mode string to VCU enum.
        Accepts: DISABLE, GDR_OFF, GDR_HORIZONTAL, GDR_VERTICAL
        """
        # Handle boolean False (converted from DISABLE by parse_value)
        if gdr_str is False or gdr_str == 0:
            return vcu.GDRMode_DISABLE
        if not isinstance(gdr_str, str):
            raise ValueError(f"Invalid Gop.GdrMode '{gdr_str}'. Must be DISABLE, GDR_OFF, GDR_HORIZONTAL, or GDR_VERTICAL")
        gdr_str = gdr_str.upper()
        if gdr_str == 'DISABLE' or gdr_str == 'GDR_OFF':
            return vcu.GDRMode_DISABLE
        elif gdr_str == 'GDR_VERTICAL':
            return vcu.GDRMode_VERTICAL
        elif gdr_str == 'GDR_HORIZONTAL':
            return vcu.GDRMode_HORIZONTAL
        else:
            raise ValueError(f"Invalid Gop.GdrMode '{gdr_str}'. Must be DISABLE, GDR_OFF, GDR_HORIZONTAL, or GDR_VERTICAL")

    def _parse_tier(self, tier_str):
        """Parse tier string to VCU enum.
        Accepts: MAIN_TIER, HIGH_TIER
        """
        tier_str = tier_str.upper()
        if tier_str == 'MAIN_TIER':
            return vcu.Tier_MAIN
        elif tier_str == 'HIGH_TIER':
            return vcu.Tier_HIGH
        else:
            raise ValueError(f"Invalid Tier '{tier_str}'. Must be MAIN_TIER or HIGH_TIER")

    @staticmethod
    def get_supported_sections():
        return {
            'INPUT': ['YUVFile', 'Width', 'Height', 'Format', 'Framerate'],
            'OUTPUT': ['BitstreamFile', 'RecFile', 'Width', 'Height'],
            'GOP': ['GopCtrlMode', 'Gop.Length', 'Gop.NumB', 'Gop.GdrMode', 'Gop.EnableLT', 'Gop.FreqLT', 'Gop.FreqIDR'],
            'RATE_CONTROL': ['RateCtrlMode', 'Bitrate', 'MaxBitrate', 'Framerate', 'CPBSize', 'InitialDelay',
                           'MaxPSNR', 'MaxPictureSize.I', 'MaxPictureSize.P', 'MaxPictureSize.B',
                           'EnableSkipFrame', 'MaxConsecutiveSkip', 'EnableFillerData'],
            'SETTINGS': ['Profile', 'Level', 'Tier', 'EntropyMode', 'ChromaMode', 'BitDepth'],
            'MOTION_VECTOR': ['FrameIndex', 'GMVectorX', 'GMVectorY'],
            'RUN': ['Loop', 'FirstPicture', 'MaxPicture']
        }

    def validate_keys(self, strict=False):
        """Validate parsed keys against known supported parameters.

        Args:
            strict: If True, raise ValueError on unknown keys. If False, print warnings.

        Returns:
            List of tuples (section, key) for unknown keys.
        """
        supported = self.get_supported_sections()
        unknown_keys = []

        for section, data in self.sections.items():
            if section not in supported:
                unknown_keys.append((section, None))
                msg = f"Unknown section: [{section}]"
                if strict:
                    raise ValueError(msg)
                else:
                    print(f"Warning: {msg}")
                continue

            # Convert supported keys to lowercase for comparison
            supported_lower = {k.lower() for k in supported[section]}

            for key in data.keys():
                if key not in supported_lower:
                    unknown_keys.append((section, key))
                    msg = f"Unknown parameter '{key}' in [{section}]"
                    if strict:
                        raise ValueError(msg)
                    else:
                        print(f"Warning: {msg}")

        return unknown_keys

    @staticmethod
    def get_supported_formats():
        return ['Y800', 'Y010', 'Y012', 'I420', 'NV12', 'P010', 'P012', 'NV16', 'P210', 'P212', 'I444', 'I4AL', 'I4CL', 'HEVC_MAIN', 'AVC_MAIN']

# OpenCV Modules for Xilinx Video Codec Units

This repository contains OpenCV modules that provide hardware-accelerated video codec support for Xilinx VCU (Video Codec Unit) and VDU (Video Decode Unit) platforms.

## Overview

The modules in this repository extend OpenCV with native support for Xilinx video acceleration hardware, enabling high-performance video encoding and decoding in OpenCV applications.

## Supported Platforms

- **VCU2** - Second generation Video Codec Unit (Versal Gen 2)
- **VDU** - Video Decode Unit (Versal)
- **VCU** - Original Video Codec Unit (Zynq UltraScale+)

## Repository Structure

```
modules/
└── vcucodec/                   # Main VCU/VDU codec module
    ├── CMakeLists.txt          # Build configuration
    ├── include/                # Public headers
    │   └── opencv2/
    │       └── vcucodec.hpp    # Main module header
    └── src/                    # Implementation
        ├── vcudecoder.cpp      # Video decoder implementation
        ├── vcuencoder.cpp      # Video encoder implementation
        └── private/            # Internal headers and utilities
            ├── vcuutils.hpp    # VCU utility functions
            └── vcuutils.cpp    # VCU utility implementation
```

## Module: vcucodec

The `vcucodec` module provides hardware-accelerated video encoding and decoding through the OpenCV APIs.

### Features

- **Hardware-accelerated H.264/H.265 encoding and decoding**
- **Multi-platform support** - Conditional compilation for VCU, VCU2, and VDU
- **OpenCV integration** - Standard VideoDecode/VideoEncode interface
- **High performance** - Direct hardware acceleration without software fallback
- **Memory efficient** - Optimized buffer management for hardware pipelines

### Platform-Specific Support

The module uses conditional compilation to support different Xilinx video platforms:

```cpp
#ifdef HAVE_VCU2_CTRLSW
    // VCU2 implementation using vcu2-ctrlsw library
#elif defined(HAVE_VCU_CTRLSW)
    // Original VCU implementation using vcu-ctrlsw library
#elif defined(HAVE_VDU_CTRLSW)
    // VDU implementation using vdu-ctrlsw library
#endif
```

### Dependencies

- **VCU2 platforms**: `vcu2-ctrlsw` control software library
- **VCU platforms**: `vcu-ctrlsw` control software library
- **VDU platforms**: `vdu-ctrlsw` control software library
- **OpenCV 4.x** with contrib modules support

## Building

This module is designed to be built as part of OpenCV using the Yocto build system. The build process works as follows:

1. **OpenCV contrib checkout**: The Yocto recipe first checks out the standard opencv_contrib repository
2. **Module merge**: The BitBake script then fetches this repository and merges the `modules/` directory into the `opencv_contrib/modules/` folder
3. **Unified build**: OpenCV builds with both standard contrib modules and the Xilinx VCU modules together
4. **Platform-specific compilation**: Only the appropriate VCU variant is compiled based on the defines set in the Yocto recipe

### Build Requirements

The module is automatically included when:
- OpenCV is configured with the merged contrib modules path
- The appropriate platform-specific defines are set (`HAVE_VCU2_CTRLSW`, `HAVE_VCU_CTRLSW`, or `HAVE_VDU_CTRLSW`)
- The required control software libraries are available (`vcu2-ctrlsw`, `vcu-ctrlsw`, or `vdu-ctrlsw`)


### Code Organization

- **Public API**: `include/opencv2/vcucodec.hpp` - Main module interface
- **Implementation**: `src/*.cpp` - Core encoding/decoding logic
- **Platform abstraction**: `src/private/vcuutils.*` - Hardware-specific utilities
- **Build configuration**: `CMakeLists.txt` - Module build settings

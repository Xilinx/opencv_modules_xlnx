
**Copyright © 2025-2026  Advanced Micro Devices, Inc. (AMD)**

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


# OpenCV Modules for Video Codec Units

This repository contains OpenCV modules that provide hardware-accelerated video codec support for
VCU (Video Codec Unit) and VDU (Video Decode Unit) platforms.

## Overview

The modules in this repository extend OpenCV with native support for video acceleration hardware,
enabling high-performance video encoding and decoding in OpenCV applications.

## Supported Platforms

- **VCU2** - Second generation Video Codec Unit (Versal Gen 2)
- **VCU** - Original Video Codec Unit (Zynq UltraScale+)

support for VDU will be added in future:
- **VDU** - Video Decode Unit (Versal)

## Repository Structure
The API for the `vcucodec` module is `vcucodec.hpp`, which includes `vcutypes.hpp`.

```
modules
└── vcucodec
    ├── cmake
    │   └── hooks
    │       └── INIT_MODULE_SOURCES_opencv_vcucodec.cmake
    ├── CMakeLists.txt
    ├── doc
    │   ├── ...
    ├── include
    │   └── opencv2
    │       ├── vcucodec.hpp
    │       ├── vcucolorconvert.hpp
    │       └── vcutypes.hpp
    ├── misc
    │   └── python
    │       ├── pyopencv_vcucodec.hpp
    │       └── python_vcucodec.hpp
    └── src
        ├── private
        │   ├── ...
        ├── vcucodec.cpp
        ├── vcucolorconvert.cpp
        ├── vcudec.cpp
        ├── vcudec.hpp
        ├── vcuenc.cpp
        ├── vcuenc.hpp
        ├── vcutypes.cpp
        ├── vcuvideoframe.cpp
        └── vcuvideoframe.hpp
```

## Module: vcucodec

The `vcucodec` module provides hardware-accelerated video encoding and decoding through the OpenCV
APIs.

### Platform-Specific Support

The module uses conditional compilation to support different AMD/Xilinx video platforms:

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

This module is designed to be built as part of OpenCV using the Yocto build system. The build
process works as follows:

1. **OpenCV contrib checkout**: The Yocto recipe checks out the standard `contrib` repository
2. **contrib_xlnx checkout**: This repository (`contrib_xlnx`) is checked out alongside `contrib`
3. **Multiple module paths**: The OpenCV build is configured with multiple extra module paths,
                              passing both `contrib/modules` and `contrib_xlnx/modules`
                              via `OPENCV_EXTRA_MODULES_PATH`
4. **Platform-specific compilation**: Only the appropriate VCU variant is compiled based on the
                                      defines set in the Yocto recipe

### Build Requirements

The module is automatically included when:
- OpenCV is configured with `contrib_xlnx/modules` in `OPENCV_EXTRA_MODULES_PATH`
  (semicolon-separated list alongside `contrib/modules`)
- The appropriate platform-specific defines are set (`HAVE_VCU2_CTRLSW`, `HAVE_VCU_CTRLSW`, or
  `HAVE_VDU_CTRLSW`)
- The required control software libraries are available (`vcu2-ctrlsw`, `vcu-ctrlsw`, or `vdu-ctrlsw`)

### Code Organization

- **Public API**: `include/opencv2/vcucodec.hpp` - Main module interface
- **Implementation**: `src/*.cpp` - Core encoding/decoding logic
- **Platform abstraction**: `src/private/*` - VCU-specific code
- **Build configuration**: `CMakeLists.txt` - Module build settings

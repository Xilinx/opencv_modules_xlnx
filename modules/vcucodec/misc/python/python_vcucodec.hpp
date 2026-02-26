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

// This file is auto-discovered by the OpenCV Python binding generator via the
// misc/python/python_*.hpp naming convention.  It is #include'd via
// pyopencv_custom_headers.h (generated at CMake configure time).
//
// gen2.py emits:
//   static PyMethodDef methods_vcucodec[] = {
//       ...auto-generated entries...
//       #ifdef PYOPENCV_EXTRA_METHODS_VCUCODEC
//           PYOPENCV_EXTRA_METHODS_VCUCODEC   <-- expanded here
//       #endif
//       {NULL, NULL}
//   };
//
// The function implementation (pyvcucodec_VideoFrame_plane_numpy) lives in
// pyopencv_vcucodec.hpp, which is also included before the method table.

#ifdef HAVE_OPENCV_VCUCODEC

#define PYOPENCV_EXTRA_METHODS_VCUCODEC \
    {"plane_numpy", CV_PY_FN_WITH_KW(pyvcucodec_VideoFrame_plane_numpy), \
        "plane_numpy(frame, index) -> numpy.ndarray\n" \
        "Return a zero-copy read-only numpy array view of the specified YUV plane.\n" \
        "The hardware buffer is pinned and will not be recycled while the array exists.\n" \
        "@param frame  VideoFrame returned by Decoder.nextFrame().\n" \
        "@param index  Plane index: 0 = Y, 1 = UV (semi-planar) or U (planar), 2 = V (planar)."},

#endif // HAVE_OPENCV_VCUCODEC

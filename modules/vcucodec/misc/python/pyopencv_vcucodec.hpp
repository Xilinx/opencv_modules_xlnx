/*
   Copyright (c) 2025-2026  Advanced Micro Devices, Inc. (AMD)

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

// This file is auto-included by the OpenCV Python binding generator via the
// misc/python/pyopencv*.hpp convention. It provides zero-copy numpy array
// access to VideoFrame planes (bypasses the deep-copy in OpenCV's
// pyopencv_from(Mat)).
//
// Python usage:
//   status, frame = dec.nextFrame()
//   y  = cv2.vcucodec.plane_numpy(frame, 0)   # zero-copy Y
//   uv = cv2.vcucodec.plane_numpy(frame, 1)   # zero-copy UV

#ifdef HAVE_OPENCV_VCUCODEC

#include <numpy/ndarraytypes.h>
#include "opencv2/vcucodec.hpp"
#include "../../src/vcuvideoframe.hpp"

// -----------------------------------------------------------------------
//  PyCapsule destructor: releases the shared_ptr<void> that pins the HW buffer.
//  When the last numpy array referencing this capsule is garbage-collected,
//  the pin is released and the frame buffer is returned to the decoder pool.
// -----------------------------------------------------------------------
static void _vcucodec_capsule_destructor(PyObject* capsule)
{
    auto* p = static_cast<std::shared_ptr<void>*>(
        PyCapsule_GetPointer(capsule, "vcucodec_pin"));
    delete p;
}

// -----------------------------------------------------------------------
//  Extract a VideoFrameImpl* from a Python Ptr<VideoFrame> argument.
//  Uses dynamic_cast to get the concrete type with zero-copy accessors.
// -----------------------------------------------------------------------
static cv::vcucodec::VideoFrameImpl* _vcucodec_extract_impl(PyObject* pyFrame)
{
    cv::Ptr<cv::vcucodec::VideoFrame> frame;
    if (!pyopencv_to(pyFrame, frame, ArgInfo("frame", false)) || frame.empty())
    {
        PyErr_SetString(PyExc_TypeError, "Expected cv.vcucodec.VideoFrame");
        return NULL;
    }

    auto* impl = dynamic_cast<cv::vcucodec::VideoFrameImpl*>(frame.get());
    if (!impl)
    {
        PyErr_SetString(PyExc_TypeError,
            "VideoFrame is not backed by a hardware buffer");
        return NULL;
    }
    return impl;
}

// -----------------------------------------------------------------------
//  Core helper: create a zero-copy numpy view of a VideoFrameImpl plane.
//  Used by plane_numpy().
// -----------------------------------------------------------------------
static PyObject* _vcucodec_plane_to_numpy(cv::vcucodec::VideoFrameImpl* impl, int index)
{
    int nPlanes = (int)impl->planes().size();
    if (index < 0 || index >= nPlanes)
    {
        PyErr_Format(PyExc_IndexError, "Plane index %d out of range [0, %d)", index, nPlanes);
        return NULL;
    }

    const cv::Mat& m = impl->planeRef(index);

    int depth = CV_MAT_DEPTH(m.type());
    int channels = CV_MAT_CN(m.type());
    int npy_type;
    switch (depth)
    {
    case CV_8U:  npy_type = NPY_UINT8;  break;
    case CV_16U: npy_type = NPY_UINT16; break;
    default:
        PyErr_SetString(PyExc_RuntimeError, "Unsupported plane depth");
        return NULL;
    }

    npy_intp ndim;
    npy_intp shape[3];
    npy_intp strides[3];
    int elemSize = (depth == CV_16U) ? 2 : 1;

    if (channels == 1)
    {
        ndim = 2;
        shape[0] = m.rows;
        shape[1] = m.cols;
        strides[0] = (npy_intp)m.step[0];
        strides[1] = elemSize;
    }
    else
    {
        ndim = 3;
        shape[0] = m.rows;
        shape[1] = m.cols;
        shape[2] = channels;
        strides[0] = (npy_intp)m.step[0];
        strides[1] = channels * elemSize;
        strides[2] = elemSize;
    }

    // Pin the hardware buffer
    std::shared_ptr<void> pin = impl->pin();
    auto* pinPtr = new std::shared_ptr<void>(std::move(pin));
    PyObject* capsule = PyCapsule_New(pinPtr, "vcucodec_pin", _vcucodec_capsule_destructor);
    if (!capsule)
    {
        delete pinPtr;
        return NULL;
    }

    PyObject* arr = PyArray_New(&PyArray_Type, (int)ndim, shape, npy_type,
                                strides, m.data, 0, 0, NULL);
    if (!arr)
    {
        Py_DECREF(capsule);
        return NULL;
    }

    PyArray_CLEARFLAGS(reinterpret_cast<PyArrayObject*>(arr), NPY_ARRAY_WRITEABLE);

    if (PyArray_SetBaseObject(reinterpret_cast<PyArrayObject*>(arr), capsule) < 0)
    {
        Py_DECREF(arr);
        return NULL;
    }

    return arr;
}

// -----------------------------------------------------------------------
//  plane_numpy(frame, index) -> numpy.ndarray
// -----------------------------------------------------------------------
static PyObject* pyvcucodec_VideoFrame_plane_numpy(PyObject*, PyObject* args, PyObject* kw)
{
    PyObject* pyobj_frame = NULL;
    int index = 0;
    const char* keywords[] = {"frame", "index", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kw, "Oi", (char**)keywords, &pyobj_frame, &index))
        return NULL;

    auto* impl = _vcucodec_extract_impl(pyobj_frame);
    if (!impl) return NULL;

    return _vcucodec_plane_to_numpy(impl, index);
}

#endif // HAVE_OPENCV_VCUCODEC

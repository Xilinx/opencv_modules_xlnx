// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "lib_app/Sink.hpp"
#include <functional>

extern "C"
{
#include "lib_common/Error.h"
}

struct IEncoderSink : public IFrameSink
{
  virtual ~IEncoderSink() = default;

  using ChangeSourceCallback = std::function<void(int, int)>;
  virtual void SetChangeSourceCallback(ChangeSourceCallback changeSourceCB) = 0;
  virtual void PreprocessFrame() = 0;

  virtual AL_ERR GetLastError() = 0;
};

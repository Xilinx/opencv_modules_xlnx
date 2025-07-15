// Modifications Copyright(C) [2025] Advanced Micro Devices, Inc. All rights reserved)

// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include "lib_app/Sink.h"
#include "CfgParser.h"

IFrameSink* createBitstreamWriter(std::string path, ConfigFile const& cfg);

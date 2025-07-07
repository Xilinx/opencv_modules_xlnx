// SPDX-FileCopyrightText: © 2025 Allegro DVT <github-ip@allegrodvt.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "lib_app/Sink.h"
#include "CfgParser.h"

IFrameSink* createYuvMd5Calculator(std::string path, ConfigFile& cfg_);

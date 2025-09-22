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
#include "vcucommand.h"

namespace cv {
namespace vcucodec {

void CommandQueue::push(Command cmd)
{
    std::lock_guard lock(mutex_);
    commands_.push(cmd);
}

void CommandQueue::execute(int32_t currentFrame)
{
    std::unique_lock lock(mutex_);
    while (!commands_.empty() && commands_.top().frameIndex <= currentFrame)
    {
        const Command& cmd = commands_.top();
        if (!cmd.skipOnMiss || cmd.frameIndex == currentFrame)
        {
            Command execCmd = cmd; // Copy command to execute
            commands_.pop();
            lock.unlock(); // Unlock while executing to avoid deadlocks
            execCmd.execute();
            lock.lock();
        }
        else
        {
            commands_.pop();
        }
    }
}


} // namespace vcucodec
} // namespace cv
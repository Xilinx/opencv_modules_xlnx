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
#ifndef OPENCV_VCUCODEC_VCUCOMMAND_HPP
#define OPENCV_VCUCODEC_VCUCOMMAND_HPP

#include <opencv2/core.hpp>

#include <mutex>
#include <queue>
#include <vector>
#include <functional>
#include <cstdint>

extern "C" {

}

namespace cv {
namespace vcucodec {

struct Command
{
    using Func = std::function< void ()>;

    bool operator<(const Command& other) const
    {                                         // Used in priority_queue:
        return frameIndex > other.frameIndex; // Lowest frameIndex has highest priority
    }

    void operator()() { execute(); }

    int32_t frameIndex; // Frame index to apply the command
    bool    skipOnMiss; // If true, skip the command if the frame index is missed
    Func    execute;    // Function to execute the command
};

class CommandQueue
{
public:
    CommandQueue() = default;
    ~CommandQueue() = default;
    void push(Command cmd);
    void execute(int32_t currentFrame);
private:
    std::mutex mutex_;
    std::priority_queue<Command> commands_;
};


} // namespace vcucodec
} // namespace cv

#endif // OPENCV_VCUCODEC_VCUCOMMAND_HPP
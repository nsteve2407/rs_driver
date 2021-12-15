/*********************************************************************************************************************
Copyright (c) 2020 RoboSense
All rights reserved

By downloading, copying, installing or using the software you agree to this license. If you do not agree to this
license, do not download, install, copy or use the software.

License Agreement
For RoboSense LiDAR SDK Library
(3-clause BSD License)

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the names of the RoboSense, nor Suteng Innovation Technology, nor the names of other contributors may be used
to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*********************************************************************************************************************/

#pragma once
namespace robosense
{
namespace lidar
{

template <typename T_Packet>
class SingleReturnBlockDiff
{
public:

  virtual float ts(uint16_t blk)
  {
    float ret = 0.0f;
    if (blk > 0)
    {
      ret = this->const_param_.BLOCK_DURATION;
    }

    return ret;
  }

  virtual int16_t azimuth(uint16_t blk)
  {
    int16_t azi= 0;

    if (blk < (this->const_param_.BLOCKS_PER_PKT - 1))
      azi = this->pkt_.blocks[blk+1].azimuth - this->pkt_.blocks[blk].azimuth;
    else
      azi = this->pkt_.blocks[blk].azimuth - this->pkt_.blocks[blk-1].azimuth;

    return azi;
  }

  SingleReturnBlockDiff(const RSDecoderConstParam const_param, const T_Packet& pkt)
    : const_param_(const_param), pkt_(pkt)
  {
  }

protected:
  const RSDecoderConstParam const_param_;
  const T_Packet& pkt_;
};

template <typename T_Packet>
class DualReturnBlockDiff

{
public:

  float ts(uint16_t blk)
  {
    float ret = 0.0f;

    if ((blk % 2 == 0) && (blk != 0))
    {
      ret = this->const_param_.BLOCK_DURATION;
    }

    return ret;
  }

  int16_t azimuth(uint16_t blk)
  {
    int16_t azi = 0;

    if (blk >= (this->const_param_.BLOCKS_PER_PKT - 2))
    {
      azi = this->pkt_.blocks[blk].azimuth - this->pkt_.blocks[blk-2].azimuth;
    }
    else
    {
      azi = this->pkt_.blocks[blk+2].azimuth - this->pkt_.blocks[blk].azimuth;
    }

    return azi;
  }

  DualReturnBlockDiff(const RSDecoderConstParam const_param, const T_Packet& pkt)
    : const_param_(const_param), pkt_(pkt)
  {
  }

protected:
  const RSDecoderConstParam const_param_;
  const T_Packet& pkt_;
};

}  // namespace lidar
}  // namespace robosense
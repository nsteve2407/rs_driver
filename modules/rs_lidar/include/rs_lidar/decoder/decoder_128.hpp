/******************************************************************************
 * Copyright 2017 RoboSense All rights reserved.
 * Suteng Innovation Technology Co., Ltd. www.robosense.ai

 * This software is provided to you directly by RoboSense and might
 * only be used to access RoboSense LiDAR. Any compilation,
 * modification, exploration, reproduction and redistribution are
 * restricted without RoboSense's prior consent.

 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ROBOSENSE BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/
#include "rs_lidar/decoder/decoder_base.hpp"
namespace robosense
{
namespace sensor
{
#define RS128_MSOP_SYNC (0x5A05AA55)
#define RS128_BLOCK_ID (0xFE)
#define RS128_DIFOP_SYNC (0x5A00FFA5)
#define RS128_CHANNELS_PER_BLOCK (128)
#define RS128_BLOCKS_PER_PKT (3)
#define RS128_TEMPERATURE_MIN (31)
#define RS128_TEMPERATURE_RANGE (50)
#define RS128_DSR_TOFFSET (3.0)
#define RS128_BLOCK_TDURATION (55.0)

typedef struct
{
    uint8_t id;
    uint8_t ret_id;
    uint16_t azimuth;
    ST_Channel channels[RS128_CHANNELS_PER_BLOCK];
} __attribute__((packed)) ST128_MsopBlock;

typedef struct
{
    uint32_t sync;
    uint8_t reserved1[3];
    uint8_t wave_mode;
    uint8_t temp_low;
    uint8_t temp_high;
    ST_Timestamp timestamp;
    uint8_t reserved2[60];
} __attribute__((packed)) ST128_MsopHeader;

typedef struct
{
    ST128_MsopHeader header;
    ST128_MsopBlock blocks[RS128_BLOCKS_PER_PKT];
    uint32_t index;
} __attribute__((packed)) ST128_MsopPkt;

typedef struct
{
    uint8_t reserved[240];
    uint8_t coef;
    uint8_t ver;
} __attribute__((packed)) ST128_Intensity;

typedef struct
{
    uint64_t sync;
    uint16_t rpm;
    ST_EthNet eth;
    ST_FOV fov;
    uint16_t reserved0;
    uint16_t lock_phase_angle;
    ST_Version version;
    ST128_Intensity intensity;
    ST_SN sn;
    uint16_t zero_cali;
    uint8_t return_mode;
    uint16_t sw_ver;
    ST_Timestamp timestamp;
    ST_Status status;
    uint8_t reserved1[11];
    ST_Diagno diagno;
    uint8_t gprmc[86];
    uint8_t pitch_cali[96];
    uint8_t yaw_cali[96];
    uint8_t reserved2[586];
    uint16_t tail;
} __attribute__((packed)) ST128_DifopPkt;


template <typename vpoint>
class Decoder128 : public DecoderBase<vpoint>
{
public:
    Decoder128(ST_Param &param);
    int32_t decodeDifopPkt(const uint8_t *pkt);
    int32_t decodeMsopPkt(const uint8_t *pkt, std::vector<vpoint> &vec,int &height);
    double getLidarTime(const uint8_t *pkt);
    void   loadCalibrationFile(std::string cali_path);
    float intensityCalibration(float intensity, int32_t channel, int32_t distance, float temp);
    float computeTemperatue(const uint8_t temp_low, const uint8_t temp_high);
private:
    int   tempPacketNum;
    float last_temp;
};

template <typename vpoint>
Decoder128<vpoint>::Decoder128(ST_Param &param) : DecoderBase<vpoint>(param)
{
    this->Rx_ = 0.03615;
    this->Ry_ = -0.017;
    this->Rz_ = 0;

    this->intensity_coef_ = 51;

    if (param.max_distance > 230.0f || param.max_distance < 3.5f)
    {
        this->max_distance_ = 230.0f;
    }
    else
    {
        this->max_distance_ = param.max_distance;
    }

    if (param.min_distance > 230.0f || param.min_distance > param.max_distance)
    {
        this->min_distance_ = 3.5f;
    }
    else
    {
        this->min_distance_ = param.min_distance;
    }

    int pkt_rate = 6760;
    this->pkts_per_frame_ = ceil(pkt_rate * 60 / this->rpm_);

    tempPacketNum = 0;
    last_temp = 31.0;
}

template <typename vpoint>
double Decoder128<vpoint>::getLidarTime(const uint8_t *pkt)
{
    ST128_MsopPkt *mpkt_ptr = (ST128_MsopPkt *)pkt;
    std::tm stm;
    memset(&stm, 0, sizeof(stm));
    stm.tm_year = mpkt_ptr->header.timestamp.year + 100;
    stm.tm_mon = mpkt_ptr->header.timestamp.month - 1;
    stm.tm_mday = mpkt_ptr->header.timestamp.day;
    stm.tm_hour = mpkt_ptr->header.timestamp.hour;
    stm.tm_min = mpkt_ptr->header.timestamp.minute;
    stm.tm_sec = mpkt_ptr->header.timestamp.second;
    return std::mktime(&stm) + (double)RS_SWAP_SHORT(mpkt_ptr->header.timestamp.ms) / 1000.0 + (double)RS_SWAP_SHORT(mpkt_ptr->header.timestamp.us) / 1000000.0;
}

template <typename vpoint>
float Decoder128<vpoint>::computeTemperatue(const uint8_t temp_low, const uint8_t temp_high)
{
    float neg_flag = temp_low & 0x80;
    float msb = temp_low & 0x7F;
    float lsb = temp_high >> 4;
    float temp;
    if (neg_flag == 0x80)
    {
        temp = -1 * (msb * 16 + lsb) * 0.0625f;
    }
    else
    {
        temp = (msb * 16 + lsb) * 0.0625f;
    }

    return temp;
}

template <typename vpoint>
int Decoder128<vpoint>::decodeMsopPkt(const uint8_t *pkt, std::vector<vpoint> &vec,int &height)
{ 
    height=128;
    ST128_MsopPkt *mpkt_ptr = (ST128_MsopPkt *)pkt;
    if (mpkt_ptr->header.sync != RS128_MSOP_SYNC)
    {
      return -2;
    }

    float azimuth_corrected_float;
    int azimuth_corrected;
    float temperature = last_temp;
    int first_azimuth = RS_SWAP_SHORT(mpkt_ptr->blocks[0].azimuth);

    if(tempPacketNum < 20000 && tempPacketNum > 0)
    {
        tempPacketNum++;
    }
    else
    {
        temperature = computeTemperatue(mpkt_ptr->header.temp_low, mpkt_ptr->header.temp_high);
        last_temp = temperature;
        tempPacketNum = 0;
    }

    for (int blk_idx = 0; blk_idx < RS128_BLOCKS_PER_PKT; blk_idx++)
    {
        if (mpkt_ptr->blocks[blk_idx].id != RS128_BLOCK_ID)
        {
            break;
        }

        int azimuth_blk = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx].azimuth);
        int azi_prev = 0;
        int azi_cur = 0;
        if (this->echo_mode_ == RS_ECHO_DUAL)
        {
            if (blk_idx < (RS128_BLOCKS_PER_PKT - 2)) // 3
            {
                azi_prev = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx + 2].azimuth);
                azi_cur = azimuth_blk;
            }
            else
            {
                azi_prev = azimuth_blk;
                azi_cur = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx - 2].azimuth);
            }
        }
        else
        {
            if (blk_idx < (RS128_BLOCKS_PER_PKT - 1)) // 3
            {
                azi_prev = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx + 1].azimuth);
                azi_cur = azimuth_blk;
            }
            else
            {
                azi_prev = azimuth_blk;
                azi_cur = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx - 1].azimuth);
            }
        }

        float azimuth_diff = (float)((36000 + azi_prev - azi_cur) % 36000);
        // Ingnore the block if the azimuth change abnormal
        if(azimuth_diff <= 0.0 || azimuth_diff > 40.0)
        {
          continue;
        }

        for (int channel_idx = 0; channel_idx < RS128_CHANNELS_PER_BLOCK; channel_idx++)
        {
            int dsr_temp;
            if (channel_idx >= 16)
            {
              dsr_temp = channel_idx % 16;
            }
            else
            {
              dsr_temp = channel_idx;
            }

            azimuth_corrected_float = azimuth_blk + (azimuth_diff * (dsr_temp * RS128_DSR_TOFFSET) / RS128_BLOCK_TDURATION);
            azimuth_corrected = this->azimuthCalibration(azimuth_corrected_float, channel_idx);

            int distance = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx].channels[channel_idx].distance);

            float intensity = mpkt_ptr->blocks[blk_idx].channels[channel_idx].intensity;
            intensity = intensityCalibration(intensity, channel_idx, distance, temperature);

            float distance_cali = this->distanceCalibration(distance, channel_idx, temperature);
            if (this->resolution_type_ == RS_RESOLUTION_5mm)
            {
                distance_cali = distance_cali * RS_RESOLUTION_5mm_DISTANCE_COEF;
            }
            else
            {
                distance_cali = distance_cali * RS_RESOLUTION_10mm_DISTANCE_COEF;
            }

            int angle_horiz_ori = (int)(azimuth_corrected_float + 36000) % 36000;
            int angle_horiz = (azimuth_corrected + 36000) % 36000;
            int angle_vert = (((int)(this->vert_angle_list_[channel_idx] * 100) % 36000) + 36000) % 36000;

            vpoint point;
            if ((distance_cali > this->max_distance_ || distance_cali < this->min_distance_)
              ||(this->angle_flag_ && (angle_horiz < this->start_angle_ || angle_horiz > this->end_angle_))
              ||(!this->angle_flag_ && (angle_horiz > this->start_angle_ && angle_horiz < this->end_angle_)))
            {
                point.x = NAN;
                point.y = NAN;
                point.z = NAN;
                point.intensity = 0;
            }
            else
            {
                const double vert_cos_value = this->cos_lookup_table_[angle_vert];
                const double horiz_cos_value = this->cos_lookup_table_[angle_horiz];
                const double horiz_ori_cos_value =this->cos_lookup_table_[angle_horiz_ori];
                point.x = distance_cali * vert_cos_value * horiz_cos_value + this->Rx_ * horiz_ori_cos_value;

                const double horiz_sin_value = this->sin_lookup_table_[angle_horiz];
                const double horiz_ori_sin_value = this->sin_lookup_table_[angle_horiz_ori];
                point.y = -distance_cali * vert_cos_value * horiz_sin_value - this->Rx_ * horiz_ori_sin_value;

                const double vert_sin_value = this->sin_lookup_table_[angle_vert];
                point.z = distance_cali * vert_sin_value + this->Rz_;

                point.intensity = intensity;
                if (std::isnan(point.intensity))
                {
                  point.intensity = 0;
                }
            }
            vec.push_back(std::move(point));
        }
    }

    return first_azimuth;
}

template <typename vpoint>
float Decoder128<vpoint>::intensityCalibration(float intensity, int32_t channel, int32_t distance, float temp)
{
    if (this->intensity_mode_ == 3)
    {
        return intensity;
    }
    else
    {
        float real_pwr = std::max((float)(intensity / (1 + (temp - RS128_TEMPERATURE_MIN) / 24.0f)), 1.0f);
        if (this->intensity_mode_ == 1)
        {
            if ((int)real_pwr < 126)
            {
                real_pwr = real_pwr * 4.0f;
            }
            else if ((int)real_pwr >= 126 && (int)real_pwr < 226)
            {
                real_pwr = (real_pwr - 125.0f) * 16.0f + 500.0f;
            }
            else
            {
                real_pwr = (real_pwr - 225.0f) * 256.0f + 2100.0f;
            }
        }
        else if (this->intensity_mode_ == 2)
        {
            if ((int)real_pwr < 64)
            {
                real_pwr = real_pwr;
            }
            else if ((int)real_pwr >= 64 && (int)real_pwr < 176)
            {
                real_pwr = (real_pwr - 64.0f) * 4.0f + 64.0f;
            }
            else
            {
                real_pwr = (real_pwr - 176.0f) * 16.0f + 512.0f;
            }
        }

        int temp_idx = (int)floor(temp + 0.5);
        if (temp_idx < RS128_TEMPERATURE_MIN)
        {
            temp_idx = 0;
        }
        else if (temp_idx > RS128_TEMPERATURE_MIN + RS128_TEMPERATURE_RANGE)
        {
            temp_idx = RS128_TEMPERATURE_RANGE;
        }
        else
        {
            temp_idx = temp_idx - RS128_TEMPERATURE_MIN;
        }
        
        int distance_cali = (distance > this->channel_cali_[channel][temp_idx]) ? distance : this->channel_cali_[channel][temp_idx];
        distance_cali = distance_cali - this->channel_cali_[channel][temp_idx];

        float distance_final = (float)distance_cali * RS_RESOLUTION_5mm_DISTANCE_COEF;
        if (this->resolution_type_ == RS_RESOLUTION_10mm)
        {
            distance_final = (float)distance_cali * RS_RESOLUTION_10mm_DISTANCE_COEF;
        }

        float ref_pwr_temp = 0.0f;
        int order = 3;
        float sect1 = 5.0f;
        float sect2 = 40.0f;

        if (this->intensity_mode_ == 1)
        {
            if (distance_final <= sect1)
            {
                ref_pwr_temp = this->intensity_cali_[0][channel] * exp(this->intensity_cali_[1][channel] -
                                                                       this->intensity_cali_[2][channel] * distance_final) +
                               this->intensity_cali_[3][channel];
            }
            else
            {
                for (int i = 0; i < order; i++)
                {
                    ref_pwr_temp += this->intensity_cali_[i + 4][channel] * (pow(distance_final, order - 1 - i));
                }
            }
        }
        else if (this->intensity_mode_ == 2)
        {
            if (distance_final <= sect1)
            {
                ref_pwr_temp = this->intensity_cali_[0][channel] * exp(this->intensity_cali_[1][channel] -
                                                                       this->intensity_cali_[2][channel] * distance_final) +
                               this->intensity_cali_[3][channel];
            }
            else if (distance_final > sect1 && distance_final <= sect2)
            {
                for (int i = 0; i < order; i++)
                {
                    ref_pwr_temp += this->intensity_cali_[i + 4][channel] * (pow(distance_final, order - 1 - i));
                }
            }
            else
            {
                float ref_pwr_t0 = 0.0f;
                float ref_pwr_t1 = 0.0f;
                for (int i = 0; i < order; i++)
                {
                    ref_pwr_t0 += this->intensity_cali_[i + 4][channel] * pow(40.0f, order - 1 - i);
                    ref_pwr_t1 += this->intensity_cali_[i + 4][channel] * pow(39.0f, order - 1 - i);
                }
                ref_pwr_temp = 0.3f * (ref_pwr_t0 - ref_pwr_t1) * distance_final + ref_pwr_t0;
            }
        }
        float ref_pwr = std::max(std::min(ref_pwr_temp, 500.0f), 4.0f);
        float intensity_f = (this->intensity_coef_ * ref_pwr) / real_pwr;
        intensity_f = (int)intensity_f > 255 ? 255.0f : intensity_f;
        return intensity_f;
    }
}

template <typename vpoint>
int Decoder128<vpoint>::decodeDifopPkt(const uint8_t *pkt)
{
    ST128_DifopPkt *rs128_ptr = (ST128_DifopPkt *)pkt;
    if (rs128_ptr->sync != RS128_DIFOP_SYNC)
    {
        return -2;
    }

    ST_Version *p_ver = &(rs128_ptr->version);
    if ((p_ver->bottom_sn[0] == 0x08 && p_ver->bottom_sn[1] == 0x02 && p_ver->bottom_sn[2] >= 0x09) ||
        (p_ver->bottom_sn[0] == 0x08 && p_ver->bottom_sn[1] > 0x02))
    {
        if (rs128_ptr->return_mode == 0x01 || rs128_ptr->return_mode == 0x02)
        {
            this->echo_mode_ = rs128_ptr->return_mode;
        }
        else
        {
            this->echo_mode_ = 0;
        }
    }
    else
    {
        this->echo_mode_ = 1;
    }

    int pkt_rate = 6760;
    if (this->echo_mode_ == RS_ECHO_DUAL)
    {
        pkt_rate = pkt_rate * 2;
    }
    this->pkts_per_frame_ = ceil(pkt_rate * 60 / this->rpm_);

    if ((p_ver->main_sn[1] == 0x00 && p_ver->main_sn[2] == 0x00 && p_ver->main_sn[3] == 0x00) ||
     (p_ver->main_sn[1] == 0xFF && p_ver->main_sn[2] == 0xFF && p_ver->main_sn[3] == 0xFF) ||
      (p_ver->main_sn[1] == 0x55 && p_ver->main_sn[2] == 0xAA && p_ver->main_sn[3] == 0x5A) || 
      (p_ver->main_sn[1] == 0xE9 && p_ver->main_sn[2] == 0x01 && p_ver->main_sn[3] == 0x00))
    {
        this->resolution_type_ = 1;
    }
    else
    {
        this->resolution_type_ = 0;
    }

    if (rs128_ptr->intensity.coef != 0x00 && rs128_ptr->intensity.coef != 0xff) 
    {
        this->intensity_coef_ = static_cast<int>(rs128_ptr->intensity.coef);
    }

    if (rs128_ptr->intensity.ver == 0x00 || 
        rs128_ptr->intensity.ver == 0xFF || 
        rs128_ptr->intensity.ver == 0xA1)
    {
        this->intensity_mode_ = 1;
    }
    else if (rs128_ptr->intensity.ver == 0xB1)
    {
        this->intensity_mode_ = 2;
    }
    else if (rs128_ptr->intensity.ver == 0xC1)
    {
        this->intensity_mode_ = 3;
    }

    if (!(this->cali_data_flag_ & 0x2))
    {
        bool angle_flag = true;
        const uint8_t *p_pitch_cali;
        p_pitch_cali = ((ST128_DifopPkt *)pkt)->pitch_cali;
        if ((p_pitch_cali[0] == 0x00 || p_pitch_cali[0] == 0xFF) && (p_pitch_cali[1] == 0x00 || p_pitch_cali[1] == 0xFF) &&
            (p_pitch_cali[2] == 0x00 || p_pitch_cali[2] == 0xFF) && (p_pitch_cali[3] == 0x00 || p_pitch_cali[3] == 0xFF))
        {
          angle_flag = false;
        }

        if (angle_flag)
        {
            int lsb, mid, msb, neg = 1;
            const uint8_t *p_yaw_cali = ((ST128_DifopPkt *)pkt)->yaw_cali;
            for (int i = 0; i < 128; i++)
            {
                lsb = p_pitch_cali[i * 3];
                mid = p_pitch_cali[i * 3 + 1];
                msb = p_pitch_cali[i * 3 + 2];
                this->vert_angle_list_[i] = (lsb * 256 * 256 + mid * 256 + msb) * neg * 0.0001f;
            }

            this->cali_data_flag_ = this->cali_data_flag_ | 0x2;
        }
    }

    return 0;
}

template <typename vpoint>
void Decoder128<vpoint>::loadCalibrationFile(std::string cali_path)
{
    int row_index = 0;
    int laser_num = 128;
    std::string line_str;
    this->cali_files_dir_ = cali_path;
    std::string file_dir = this->cali_files_dir_ + "/angle.csv";
    std::ifstream fd(file_dir.c_str(), std::ios::in);
    if (!fd.is_open())
    {
      std::cout<<file_dir << " does not exist"<< std::endl;
    }
    else
    {
        row_index = 0;
        while (std::getline(fd, line_str))
        {
            std::stringstream ss(line_str);
            std::string str;
            std::vector<std::string> vect_str;
            while (std::getline(ss, str, ','))
            {
                vect_str.push_back(str);
            }
            this->vert_angle_list_[row_index] = std::stof(vect_str[0]);/*  / 180 * M_PI */;
            this->hori_angle_list_[row_index] = std::stof(vect_str[1]) * 100;
            row_index++;
            if (row_index >= laser_num)
            {
                break;
            }
        }
        fd.close();
    }
   
}
} // namespace sensor
} // namespace robosense
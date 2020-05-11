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
#include "driver/decoder/decoder_base.hpp"
namespace robosense
{
namespace sensor
{
#define RS16_CHANNELS_PER_BLOCK (32)
#define RS16_BLOCKS_PER_PKT (12)
#define RS16_BLOCK_TDURATION_DUAL (50)
#define RS16_BLOCK_TDURATION_SINGLE (100)
#define RS16_POINTS_CHANNEL_PER_SECOND (20000)
#define RS16_BLOCKS_CHANNEL_PER_PKT (12)
#define RS16_MSOP_SYNC (0xA050A55A0A05AA55)
#define RS16_BLOCK_ID (0xEEFF)
#define RS16_DIFOP_SYNC (0x555511115A00FFA5)
#define RS16_CHANNEL_TOFFSET (3)
#define RS16_FIRING_TDURATION (50)

typedef struct
{
    uint16_t id;
    uint16_t azimuth;
    ST_Channel channels[RS16_CHANNELS_PER_BLOCK];
} __attribute__((packed)) ST16_MsopBlock;

typedef struct
{
    ST_MsopHeader header;
    ST16_MsopBlock blocks[RS16_BLOCKS_PER_PKT];
    uint32_t index;
    uint16_t tail;
} __attribute__((packed)) ST16_MsopPkt;

typedef struct
{
    uint8_t intensity_cali[240];
    uint8_t coef;
    uint8_t ver;
} __attribute__((packed)) ST16_Intensity;

typedef struct
{
    uint64_t sync;
    uint16_t rpm;
    ST_EthNet eth;
    ST_FOV fov;
    uint16_t static_base;
    uint16_t lock_phase_angle;
    ST_Version version;
    ST16_Intensity intensity;
    ST_SN sn;
    uint16_t zero_cali;
    uint8_t return_mode;
    uint16_t sw_ver;
    ST_Timestamp timestamp;
    ST_Status status;
    uint8_t reserved1[11];
    ST_Diagno diagno;
    uint8_t gprmc[86];
    uint8_t static_cali[697];
    uint8_t pitch_cali[48];
    uint8_t reserved2[33];
    uint16_t tail;
} __attribute__((packed)) ST16_DifopPkt;

template <typename vpoint>
class Decoder16 : public DecoderBase<vpoint>
{
public:
    Decoder16(RSDecoder_Param &param);
    int32_t decodeDifopPkt(const uint8_t *pkt);
    int32_t decodeMsopPkt(const uint8_t *pkt, std::vector<vpoint> &vec,int &height);
    double getLidarTime(const uint8_t *pkt);
    void loadCalibrationFile(std::string cali_path);
};

template <typename vpoint>
Decoder16<vpoint>::Decoder16(RSDecoder_Param &param) : DecoderBase<vpoint>(param)
{
    this->Rx_ = 0.03825;
    this->Ry_ = -0.01088;
    this->Rz_ = 0;
    this->channel_num_ = 16;
    this->temperature_max_ = 71;
    if (this->max_distance_ > 200.0f || this->max_distance_ < 0.2f)
    {
        this->max_distance_ = 200.0f;
    }
    if (this->min_distance_ > 200.0f || this->min_distance_ > this->max_distance_)
    {
        this->min_distance_ = 0.2f;
    }
}

template <typename vpoint>
double Decoder16<vpoint>::getLidarTime(const uint8_t *pkt)
{
    ST16_MsopPkt *mpkt_ptr = (ST16_MsopPkt *)pkt;
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
int Decoder16<vpoint>::decodeMsopPkt(const uint8_t *pkt, std::vector<vpoint> &vec,int &height)
{
    height=16;
    ST16_MsopPkt *mpkt_ptr = (ST16_MsopPkt *)pkt;
    if (mpkt_ptr->header.sync != RS16_MSOP_SYNC)
    {
        return -2;
    }
    int first_azimuth;
    first_azimuth = RS_SWAP_SHORT(mpkt_ptr->blocks[0].azimuth);
    float temperature = this->computeTemperatue(mpkt_ptr->header.temp_raw);
    for (int blk_idx = 0; blk_idx < RS16_BLOCKS_PER_PKT; blk_idx++)
    {
        if (mpkt_ptr->blocks[blk_idx].id != RS16_BLOCK_ID)
        {
            break;
        }
        int azimuth_blk = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx].azimuth);
        int azi_prev = 0;
        int azi_cur = 0;

        if (blk_idx < (RS16_BLOCKS_PER_PKT - 1)) // 12
        {
            azi_prev = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx + 1].azimuth);
            azi_cur = azimuth_blk;
        }
        else
        {
            azi_prev = azimuth_blk;
            azi_cur = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx - 1].azimuth);
        }
        float azimuth_diff = (float)((36000 + azi_prev - azi_cur) % 36000);
        // Ingnore the block if the azimuth change abnormal
        if (azimuth_diff <= 0.0 || azimuth_diff > 75.0)
        {
            continue;
        }        
        float azimuth_channel;
        int ab_flag = 0;
        for (int channel_idx = 0; channel_idx < RS16_CHANNELS_PER_BLOCK; channel_idx++)
        {
            int azimuth_final;

            if (this->echo_mode_ == RS_ECHO_DUAL)
            {
                azimuth_channel = azimuth_blk + azimuth_diff * RS16_CHANNEL_TOFFSET * (channel_idx % 16) / RS16_BLOCK_TDURATION_DUAL;
            }
            else
            {
                azimuth_channel = azimuth_blk + azimuth_diff * (RS16_FIRING_TDURATION * (channel_idx / 16) + RS16_CHANNEL_TOFFSET * (channel_idx % 16)) / RS16_BLOCK_TDURATION_SINGLE;
            }
            azimuth_final = ((int)round(azimuth_channel)) % 36000;
            int idx_map = channel_idx;
            int distance = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx].channels[idx_map].distance);
            float intensity = mpkt_ptr->blocks[blk_idx].channels[idx_map].intensity;
            intensity = this->intensityCalibration(intensity, channel_idx, distance, temperature);
            float distance_cali = this->distanceCalibration(distance, channel_idx, temperature);
            if (this->resolution_type_ == RS_RESOLUTION_5mm)
            {
                distance_cali = distance_cali * RS_RESOLUTION_5mm_DISTANCE_COEF;
            }
            else
            {
                distance_cali = distance_cali * RS_RESOLUTION_10mm_DISTANCE_COEF;
            }
            //
            int angle_horiz_ori;
            int angle_horiz = (azimuth_final + 36000) % 36000;
            int angle_vert;
            angle_horiz_ori = angle_horiz;
            angle_vert = (((int)(this->vert_angle_list_[channel_idx % 16] * 100) % 36000) + 36000) % 36000;

            //store to pointcloud buffer
            vpoint point;
            if ((distance_cali <= this->max_distance_ && distance_cali >= this->min_distance_) && ((this->angle_flag_ && angle_horiz >= this->start_angle_ && angle_horiz <= this->end_angle_) || (!this->angle_flag_ && ((angle_horiz >= this->start_angle_ && angle_horiz <= 36000) || (angle_horiz >= 0 && angle_horiz <= this->end_angle_)))))
            {
                const double vert_cos_value = this->cos_lookup_table_[angle_vert];
                const double horiz_cos_value = this->cos_lookup_table_[angle_horiz];
                const double horiz_ori_cos_value = this->cos_lookup_table_[angle_horiz_ori];
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
            else
            {
                point.x = NAN;
                point.y = NAN;
                point.z = NAN;
                point.intensity = NAN;
            }
            vec.push_back(point);
        }
    }
    return first_azimuth;
}

template <typename vpoint>
int32_t Decoder16<vpoint>::decodeDifopPkt(const uint8_t *pkt)
{
    ST16_DifopPkt *rs16_ptr = (ST16_DifopPkt *)pkt;
    if (rs16_ptr->sync != RS16_DIFOP_SYNC)
    {
        return -2;
    }

    ST_Version *p_ver = &(rs16_ptr->version);
    if ((p_ver->bottom_sn[0] == 0x08 && p_ver->bottom_sn[1] == 0x02 && p_ver->bottom_sn[2] >= 0x09) ||
        (p_ver->bottom_sn[0] > 0x08) || (p_ver->bottom_sn[0] == 0x08 && p_ver->bottom_sn[1] > 0x02))
    {
        if (rs16_ptr->return_mode == 0x01 || rs16_ptr->return_mode == 0x02)
        {
            this->echo_mode_ = rs16_ptr->return_mode;
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

    int pkt_rate = ceil(RS16_POINTS_CHANNEL_PER_SECOND / RS16_BLOCKS_CHANNEL_PER_PKT);
    if (this->echo_mode_ == RS_ECHO_LAST || this->echo_mode_ == RS_ECHO_MAX)
    {
        pkt_rate = ceil(pkt_rate / 2);
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
    if (rs16_ptr->intensity.ver == 0x00 || rs16_ptr->intensity.ver == 0xFF || rs16_ptr->intensity.ver == 0xA1)
    {
        this->intensity_mode_ = 1;
    }
    else if (rs16_ptr->intensity.ver == 0xB1)
    {
        this->intensity_mode_ = 2;
    }
    else if (rs16_ptr->intensity.ver == 0xC1)
    {
        this->intensity_mode_ = 3;
    }
    if (!(this->cali_data_flag_ & 0x1))
    {
        bool curve_flag = true;
        ST16_Intensity *p_intensity = &(rs16_ptr->intensity);
        if ((p_intensity->intensity_cali[0] == 0x00 || p_intensity->intensity_cali[0] == 0xFF) && 
            (p_intensity->intensity_cali[1] == 0x00 || p_intensity->intensity_cali[1] == 0xFF) && 
            (p_intensity->intensity_cali[2] == 0x00 || p_intensity->intensity_cali[2] == 0xFF) && 
            (p_intensity->intensity_cali[3] == 0x00 || p_intensity->intensity_cali[3] == 0xFF))
        {
            curve_flag = false;
        }

        if (curve_flag)
        {
            bool check_flag = true;
            uint8_t checksum;
            for (int k = 0; k < this->channel_num_; k++)
            {
                checksum = p_intensity->intensity_cali[15 * k] ^ p_intensity->intensity_cali[15 * k + 1];
                for (int n = 1; n < 7; n++)
                {
                    checksum = checksum ^ (p_intensity->intensity_cali[k * 15 + n * 2]) ^ (p_intensity->intensity_cali[k * 15 + n * 2 + 1]);
                }
                if (checksum != p_intensity->intensity_cali[k * 15 + 14])
                {
                    check_flag = false;
                    break;
                }
            }

            if (check_flag)
            {
                uint16_t *inten_p;
                for (int i = 0; i < this->channel_num_; i++)
                {
                    inten_p = (uint16_t *)(p_intensity->intensity_cali + i * 15);
                    for (int k = 0; k < 7; k++)
                    {
                        this->intensity_cali_[k][i] = RS_SWAP_SHORT(*(inten_p + k)) * 0.001;
                    }
                }

                this->cali_data_flag_ = this->cali_data_flag_ | 0x01;

                //std::cout << "[RS_decoder][difop][INFO] curves data is wrote in difop packet!" << std::endl;
            }
        }
        if (rs16_ptr->intensity.coef != 0x00 && rs16_ptr->intensity.coef != 0xFF)
        {
            this->intensity_coef_ = rs16_ptr->intensity.coef;
        }
    }

    if (!(this->cali_data_flag_ & 0x2))
    {
        bool angle_flag = true;
        const uint8_t *p_pitch_cali;

        p_pitch_cali = rs16_ptr->pitch_cali;

        if ((p_pitch_cali[0] == 0x00 || p_pitch_cali[0] == 0xFF) && 
            (p_pitch_cali[1] == 0x00 || p_pitch_cali[1] == 0xFF) && 
            (p_pitch_cali[2] == 0x00 || p_pitch_cali[2] == 0xFF) && 
            (p_pitch_cali[3] == 0x00 || p_pitch_cali[3] == 0xFF))
        {
            angle_flag = false;
        }

        if (angle_flag)
        {
            int lsb, mid, msb, neg = 1;

            for (int i = 0; i < 16; i++)
            {
                if (i < 8)
                {
                    neg = -1;
                }
                else
                {
                    neg = 1;
                }
                lsb = p_pitch_cali[i * 3];
                mid = p_pitch_cali[i * 3 + 1];
                msb = p_pitch_cali[i * 3 + 2];

                this->vert_angle_list_[i] = (lsb * 256 * 256 + mid * 256 + msb) * neg * 0.0001f; // / 180 * M_PI;
                this->hori_angle_list_[i] = 0;
            }

            this->cali_data_flag_ = this->cali_data_flag_ | 0x2;
        }
    }

    return 0;
}

template <typename vpoint>
void Decoder16<vpoint>::loadCalibrationFile(std::string cali_path)
{
    int row_index = 0;
    int laser_num = 16;
    std::string line_str;
    this->cali_files_dir_ = cali_path;
    std::string angle_file_path = this->cali_files_dir_ + "/angle.csv";

    // read angle.csv
    std::ifstream fd_angle(angle_file_path.c_str(), std::ios::in);
    if (!fd_angle.is_open())
    {
        std::cout << angle_file_path << " does not exist"<< std::endl;
    }
    else
    {
        row_index = 0;
        while (std::getline(fd_angle, line_str))
        {
            this->vert_angle_list_[row_index] = std::stof(line_str); // degree
            this->hori_angle_list_[row_index] = 0; // degree
            row_index++;
            if (row_index >= laser_num)
            {
                break;
            }
        }
        fd_angle.close();
    }

    // read ChannelNum.csv
    std::string chan_file_path = this->cali_files_dir_ + "/ChannelNum.csv";
    std::ifstream fd_ch_num(chan_file_path.c_str(), std::ios::in);
    if (!fd_ch_num.is_open())
    {
        std::cout << chan_file_path << " does not exist"<< std::endl;
    }
    else
    {
        row_index = 0;
        while (std::getline(fd_ch_num, line_str))
        {
            std::stringstream ss(line_str);
            std::string str;
            std::vector<std::string> vect_str;
            while (std::getline(ss, str, ','))
            {
                vect_str.push_back(str);
            }
            for (int col_index = 0; col_index < 41; col_index++)
            {
                this->channel_cali_[row_index][col_index] = std::stoi(vect_str[col_index]);
            }
            row_index++;
            if (row_index >= laser_num)
            {
                break;
            }
        }
        fd_ch_num.close();
    }

    // read ChannelNumDistance.csv
    std::string chan_dis_file_path = this->cali_files_dir_ + "/ChannelNumDistance.csv";
    std::ifstream fd_chan_dis(chan_dis_file_path.c_str(), std::ios::in);
    if (!fd_chan_dis.is_open())
    {
        std::cout << chan_dis_file_path << " does not exist"<< std::endl;
    }
    else
    {
        row_index = 0;
        while (std::getline(fd_chan_dis, line_str))
        {
            std::stringstream ss(line_str);
            std::string str;
            std::vector<std::string> vect_str;
            while (std::getline(ss, str, ','))
            {
                vect_str.push_back(str);
            }
            for (int col_index = 0; col_index < laser_num + 1; col_index++)
            {
                this->channel_dis_cali_[row_index][col_index] = std::stof(vect_str[col_index]);
            }
            row_index++;
            if (row_index > 3)
            {
                break;
            }
        }
        fd_chan_dis.close();
    }

    // read  ZeroAngleAbsdist
    std::string zero_angle_path = this->cali_files_dir_ + "/ZeroAngleAbsdist.csv";
    std::ifstream fd_zero_angle(zero_angle_path.c_str(), std::ios::in);
    if (!fd_zero_angle.is_open())
    {
        std::cout << zero_angle_path << " does not exist"<< std::endl;
    }
    else
    {
        std::getline(fd_zero_angle, line_str);
        float zero_angle_cali_ = std::stof(line_str);
        fd_zero_angle.close();
        for (int i = 0; i < laser_num; i++)
        {
            this->hori_angle_list_[i] -= zero_angle_cali_;
        }
    }

    // read limit.csv
    std::string dis_limit_path = this->cali_files_dir_ + "/limit.csv";
    std::ifstream fd_limit(dis_limit_path.c_str(), std::ios::in);
    if (!fd_limit.is_open())
    {
        std::cout << dis_limit_path << " does not exist"<< std::endl;
    }
    else
    {
        std::getline(fd_limit, line_str);
        this->min_distance_ = std::stof(line_str) / 100.0f;
        std::getline(fd_limit, line_str);
        this->max_distance_ = std::stof(line_str) / 100.0f;
        fd_limit.close();
    }
}
} // namespace sensor
} // namespace robosense
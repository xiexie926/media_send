#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/rtnetlink.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdbool.h>

#include "vlc_bits.h"


#define H264_FILE       "test.h264"
#define MAX_PACK_LEN    (1448-24)
#define MAX_FILE_LEN    (1024*1024)
#define RTP_HEAD_LEN    (12)
#define UDP_BASE_PORT   (60000+66)
#define H264_PAYLOAD    (96)
//封装PS流
#define PS_HDR_LEN  14  
#define SYS_HDR_LEN 15  
#define PSM_HDR_LEN 20  
#define PES_HDR_LEN 19  
#define RTP_HDR_LEN 12  
#define PS_PES_PAYLOAD_SIZE 1024
#define MAX_PES_DATA_LEN 65500

#define H264_GET_NALTYPE(c)  ((c) & 0x1F)  /* NALU */
#define NAL_TYPE_NOTIDR      1
#define NAL_TYPE_IDR         5
#define NAL_TYPE_SEI         6
#define NAL_TYPE_SPS         7
#define NAL_TYPE_PPS         8
#define NAL_TYPE_DELIMITER   9

#define PROFILE_H264_BASELINE             66
#define PROFILE_H264_MAIN                 77
#define PROFILE_H264_EXTENDED             88
#define PROFILE_H264_HIGH                 100
#define PROFILE_H264_HIGH_10              110
#define PROFILE_H264_HIGH_422             122
#define PROFILE_H264_HIGH_444             144
#define PROFILE_H264_HIGH_444_PREDICTIVE  244

#define PROFILE_H264_CAVLC_INTRA          44
#define PROFILE_H264_SVC_BASELINE         83
#define PROFILE_H264_SVC_HIGH             86
#define PROFILE_H264_MVC_STEREO_HIGH      128
#define PROFILE_H264_MVC_MULTIVIEW_HIGH   118

#define PROFILE_H264_MFC_HIGH                          134
#define PROFILE_H264_MVC_MULTIVIEW_DEPTH_HIGH          138
#define PROFILE_H264_MVC_ENHANCED_MULTIVIEW_DEPTH_HIGH 139

#define H264_SPS_ID_MAX (31)
#define H264_PPS_ID_MAX (255)

enum iso_23001_8_cp
{
    ISO_23001_8_CP_BT_709 = 1,
    ISO_23001_8_CP_UNSPECIFIED,
    ISO_23001_8_CP_RESERVED0,
    ISO_23001_8_CP_BT_470_M,
    ISO_23001_8_CP_BT_470_B_G,
    ISO_23001_8_CP_BT_601,
    ISO_23001_8_CP_SMPTE_240,
    ISO_23001_8_CP_GENERIC_FILM,
    ISO_23001_8_CP_BT_2020, /* BT.2100 */
    ISO_23001_8_CP_XYZ, /* SMPTE 428 */
    ISO_23001_8_CP_SMPTE_431,
    ISO_23001_8_CP_SMPTE_432,
    /* gap */
    ISO_23001_8_CP_EBU_3213 = 22,
};
enum iso_23001_8_tc
{
    ISO_23001_8_TC_RESERVED_0 = 0,
    ISO_23001_8_TC_BT_709,
    ISO_23001_8_TC_UNSPECIFIED,
    ISO_23001_8_TC_RESERVED_3,
    ISO_23001_8_TC_BT_470_M,
    ISO_23001_8_TC_BT_470_B_G,
    ISO_23001_8_TC_BT_601,
    ISO_23001_8_TC_SMPTE_240,
    ISO_23001_8_TC_LINEAR,
    ISO_23001_8_TC_LOG_100,
    ISO_23001_8_TC_LOG_100_SQRT10,
    ISO_23001_8_TC_IEC_61966,
    ISO_23001_8_TC_BT_1361,
    ISO_23001_8_TC_SRGB,
    ISO_23001_8_TC_BT_2020_10_BIT,
    ISO_23001_8_TC_BT_2020_12_BIT,
    ISO_23001_8_TC_SMPTE_2084,
    ISO_23001_8_TC_SMPTE_428,
    ISO_23001_8_TC_HLG /* BT.2100 HLG, ARIB STD-B67 */
};

enum iso_23001_8_mc
{
    ISO_23001_8_MC_IDENTITY = 0,
    ISO_23001_8_MC_BT_709,
    ISO_23001_8_MC_UNSPECIFIED,
    ISO_23001_8_MC_RESERVED_3,
    ISO_23001_8_MC_FCC,
    ISO_23001_8_MC_BT_470_B_G,
    ISO_23001_8_MC_BT_601,
    ISO_23001_8_MC_SMPTE_240,
    ISO_23001_8_MC_SMPTE_YCGCO,
    ISO_23001_8_MC_BT_2020_NCL,
    ISO_23001_8_MC_BT_2020_CL,
    ISO_23001_8_MC_SMPTE_2085,
    ISO_23001_8_MC_CHROMAT_NCL,
    ISO_23001_8_MC_CHROMAT_CL,
    ISO_23001_8_MC_ICTCP,
};

enum h264_nal_unit_type_e
{
    H264_NAL_UNKNOWN = 0,
    H264_NAL_SLICE   = 1,
    H264_NAL_SLICE_DPA   = 2,
    H264_NAL_SLICE_DPB   = 3,
    H264_NAL_SLICE_DPC   = 4,
    H264_NAL_SLICE_IDR   = 5,    /* ref_idc != 0 */
    H264_NAL_SEI         = 6,    /* ref_idc == 0 */
    H264_NAL_SPS         = 7,
    H264_NAL_PPS         = 8,
    H264_NAL_AU_DELIMITER= 9,
    /* ref_idc == 0 for 6,9,10,11,12 */
    H264_NAL_END_OF_SEQ  = 10,
    H264_NAL_END_OF_STREAM = 11,
    H264_NAL_FILLER_DATA = 12,
    H264_NAL_SPS_EXT     = 13,
    H264_NAL_PREFIX      = 14,
    H264_NAL_SUBSET_SPS  = 15,
    H264_NAL_DEPTH_PS    = 16,
    H264_NAL_RESERVED_17 = 17,
    H264_NAL_RESERVED_18 = 18,
    H264_NAL_SLICE_WP    = 19,
    H264_NAL_SLICE_EXT   = 20,
    H264_NAL_SLICE_3D_EXT= 21,
    H264_NAL_RESERVED_22 = 22,
    H264_NAL_RESERVED_23 = 23,
};

//rtp头结构
typedef struct rtp_head_s
{
    /**//* byte 0 */
    unsigned char u4CSrcLen:4;      /**//* expect 0 */
    unsigned char u1Externsion:1;   /**//* expect 1, see RTP_OP below */
    unsigned char u1Padding:1;      /**//* expect 0 */
    unsigned char u2Version:2;      /**//* expect 2 */
    /**//* byte 1 */
    unsigned char u7Payload:7;      /**//* RTP_PAYLOAD_RTSP */
    unsigned char u1Marker:1;       /**//* expect 1 */
    /**//* bytes 2, 3 */
    unsigned short u16SeqNum;
    /**//* bytes 4-7 */
    unsigned int u32TimeStamp;
    /**//* bytes 8-11 */
    unsigned int u32SSrc;          /**//* stream number is used here. */
} rtp_head;

typedef struct rtp_pack_head_s
{
    unsigned short payload;
    unsigned short sernum;
    unsigned int timtamp;
    unsigned int ssrc;
} rtp_pack_head;

typedef struct frame_info_S
{
    int type;
    int len;
    int start_code_len;
} frame_info;

typedef struct bits_buffer_s
{
    int     i_size;
    int     i_data;
    unsigned char i_mask;
    unsigned char *p_data;
} bits_buffer_t;

typedef struct packet_info_s
{
    unsigned long long s64CurPts;
    unsigned int IFrame;
    unsigned short  recv_port;
    char recv_ip[32];
    int sock_fd;
} packet_info;

typedef struct sps_s
{
    uint8_t i_id;
    uint8_t i_profile, i_level;
    uint8_t i_constraint_set_flags;
    /* according to avcC, 3 bits max for those */
    uint8_t i_chroma_idc;
    uint8_t i_bit_depth_luma;
    uint8_t i_bit_depth_chroma;
    uint8_t b_separate_colour_planes_flag;

    uint32_t pic_width_in_mbs_minus1;
    uint32_t pic_height_in_map_units_minus1;
    struct
    {
        uint32_t left_offset;
        uint32_t right_offset;
        uint32_t top_offset;
        uint32_t bottom_offset;
    } frame_crop;
    uint8_t frame_mbs_only_flag;
    uint8_t mb_adaptive_frame_field_flag;
    int i_log2_max_frame_num;
    int i_pic_order_cnt_type;
    int i_delta_pic_order_always_zero_flag;
    int32_t offset_for_non_ref_pic;
    int32_t offset_for_top_to_bottom_field;
    int i_num_ref_frames_in_pic_order_cnt_cycle;
    int32_t offset_for_ref_frame[255];
    int i_log2_max_pic_order_cnt_lsb;

    struct {
        bool b_valid;
        int i_sar_num, i_sar_den;
        struct {
            bool b_full_range;
            uint8_t i_colour_primaries;
            uint8_t i_transfer_characteristics;
            uint8_t i_matrix_coefficients;
        } colour;
        bool b_timing_info_present_flag;
        uint32_t i_num_units_in_tick;
        uint32_t i_time_scale;
        bool b_fixed_frame_rate;
        bool b_pic_struct_present_flag;
        bool b_hrd_parameters_present_flag; /* CpbDpbDelaysPresentFlag */
        uint8_t i_cpb_removal_delay_length_minus1;
        uint8_t i_dpb_output_delay_length_minus1;

        /* restrictions */
        uint8_t b_bitstream_restriction_flag;
        uint8_t i_max_num_reorder_frames;
    } vui;
}sps_t;

typedef struct pps_s
{
    uint8_t i_id;
    uint8_t i_sps_id;
    uint8_t i_pic_order_present_flag;
    uint8_t i_redundant_pic_present_flag;
    uint8_t weighted_pred_flag;
    uint8_t weighted_bipred_idc;
}pps_t;

int sock_udp_open(int sock_type);
void sock_udp_close(int sock_fd);
int udp_sock_send(int sock_fd, char * ip, int port, char * buffer, int len);
int pack_ps_stream(char *pData, int nFrameLen, packet_info* pPacker, int stream_type);
int get_h264_frame(unsigned char *buf, frame_info *pframe);
int parse_h264_sps(unsigned char *buf, int len, sps_t *p_sps);
int parse_h264_pps(unsigned char *buf, int len, pps_t *p_pps);


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "librtmp/rtmp.h"
#include "librtmp/rtmp_sys.h"
#include "librtmp/amf.h"
#include "librtmp/log.h"
#include "media_send.h"
#include "rtmp_h264.h"

typedef struct _RTMPMetadata
{
    // video, must be h264 type
    unsigned int    nSpsLen;
    unsigned char   *Sps;
    unsigned int    nPpsLen;
    unsigned char   *Pps;
} RTMPMetadata, *LPRTMPMetadata;

//定义包头长度，RTMP_MAX_HEADER_SIZE=18
#define RTMP_HEAD_SIZE   (sizeof(RTMPPacket)+RTMP_MAX_HEADER_SIZE)
static RTMP *m_pRtmp = NULL;
RTMPMetadata metaData = {0};

void rtmp264_init(void)
{
    RTMP_LogSetLevel(RTMP_LOGALL);//test
    m_pRtmp = RTMP_Alloc();
    RTMP_Init(m_pRtmp);
}

void rtmp264_unit(void)
{
    if(m_pRtmp)
    {
        RTMP_Close(m_pRtmp);
        RTMP_Free(m_pRtmp);
        m_pRtmp = NULL;
    }
}

int rtmp264_connect(const char *url)
{
    /*设置URL*/
    if (RTMP_SetupURL(m_pRtmp, (char *)url) == FALSE)
    {
        RTMP_Free(m_pRtmp);
        return FALSE;
    }
    /*设置可写,即发布流,这个函数必须在连接前使用,否则无效*/
    RTMP_EnableWrite(m_pRtmp);
    /*连接服务器*/
    if (RTMP_Connect(m_pRtmp, NULL) == FALSE)
    {
        RTMP_Free(m_pRtmp);
        return FALSE;
    }
    /*连接流*/
    if (RTMP_ConnectStream(m_pRtmp, 0) == FALSE)
    {
        RTMP_Close(m_pRtmp);
        RTMP_Free(m_pRtmp);
        return FALSE;
    }
    return TRUE;
}


static int rtmp264_send_packet(unsigned int nPacketType, unsigned char *data, unsigned int size, unsigned int nTimestamp)
{
    if (NULL == data)
    {
        printf("[%s:%d]invalid param\n", __FUNCTION__, __LINE__);
        return -1;
    }
    RTMPPacket *packet = (RTMPPacket *)malloc(RTMP_HEAD_SIZE + size);
    /*分配包内存和初始化,len为包体长度*/
    memset(packet, 0, RTMP_HEAD_SIZE);
    /*包体内存*/
    packet->m_body = (char *)packet + RTMP_HEAD_SIZE;
    packet->m_nBodySize = size;
    memcpy(packet->m_body, data, size);
    packet->m_hasAbsTimestamp = 0;
    packet->m_packetType = nPacketType; /*此处为类型有两种一种是音频,一种是视频*/
    packet->m_nInfoField2 = m_pRtmp->m_stream_id;
    packet->m_nChannel = 0x04;
    //printf("packet->m_nBodySize:%d\n", packet->m_nBodySize);
    packet->m_headerType = RTMP_PACKET_SIZE_LARGE;
    if (RTMP_PACKET_TYPE_AUDIO == nPacketType && size != 4)
    {
        packet->m_headerType = RTMP_PACKET_SIZE_MEDIUM;
    }
    packet->m_nTimeStamp = nTimestamp;
    //printf("packet->m_nTimeStamp:%u %x\n", packet->m_nTimeStamp, packet->m_nTimeStamp);
    /*发送*/
    int nRet = 0;
    if (RTMP_IsConnected(m_pRtmp))
    {
        nRet = RTMP_SendPacket(m_pRtmp, packet, TRUE); /*TRUE为放进发送队列,FALSE是不放进发送队列,直接发送*/
    }
    /*释放内存*/
    free(packet);
    return nRet;
}

int rtmp264_save_sps(unsigned char *data, unsigned int size)
{
    if (NULL == data)
    {
        printf("[%s:%d]invalid param\n", __FUNCTION__, __LINE__);
        return -1;
    }
    
    metaData.Sps = (unsigned char *)malloc(size);
    if (NULL == metaData.Sps)
    {
        printf("[%s:%d]malloc failed\n", __FUNCTION__, __LINE__);
        return -1;
    }
    metaData.nSpsLen = size;
    memcpy(metaData.Sps, data, size);

    return 0;
}
int rtmp264_save_pps(unsigned char *data, unsigned int size)
{
    if (NULL == data)
    {
        printf("[%s:%d]invalid param\n", __FUNCTION__, __LINE__);
        return -1;
    }
    metaData.nPpsLen = size;
    metaData.Pps = (unsigned char *)malloc(size);
    if (NULL == metaData.Pps)
    {
        printf("[%s:%d]malloc failed\n", __FUNCTION__, __LINE__);
        return -1;
    }
    memcpy(metaData.Pps, data, size);
    
    return 0;
}

int rtmp264_free_pps_sps(void)
{
    if (metaData.Sps)
    {
        free(metaData.Sps);
        metaData.Sps = NULL;
    }
    if (metaData.Pps)
    {
        free(metaData.Pps);
        metaData.Pps = NULL;
    }
    
    return 0;
}

static int rtmp264_send_sps_pps(unsigned char *pps, int pps_len, unsigned char *sps, int sps_len, unsigned int nTimestamp)
{
    if ((NULL == pps) || (NULL == sps))
    {
        printf("[%s:%d]invalid param\n", __FUNCTION__, __LINE__);
        return -1;
    }

    RTMPPacket *packet = NULL; //rtmp包结构
    unsigned char *body = NULL;
    int i = 0;
    packet = (RTMPPacket *)malloc(RTMP_HEAD_SIZE + 1024);
    //RTMPPacket_Reset(packet);//重置packet状态
    memset(packet, 0, RTMP_HEAD_SIZE + 1024);
    packet->m_body = (char *)packet + RTMP_HEAD_SIZE;
    body = (unsigned char *)packet->m_body;
    body[i++] = 0x17;
    body[i++] = 0x00;

    body[i++] = 0x00;
    body[i++] = 0x00;
    body[i++] = 0x00;

    /*AVCDecoderConfigurationRecord*/
    body[i++] = 0x01;
    body[i++] = sps[1];
    body[i++] = sps[2];
    body[i++] = sps[3];
    body[i++] = 0xff;

    /*sps*/
    body[i++]   = 0xe1;
    body[i++] = (sps_len >> 8) & 0xff;
    body[i++] = sps_len & 0xff;
    memcpy(&body[i], sps, sps_len);
    i +=  sps_len;

    /*pps*/
    body[i++]   = 0x01;
    body[i++] = (pps_len >> 8) & 0xff;
    body[i++] = (pps_len) & 0xff;
    memcpy(&body[i], pps, pps_len);
    i +=  pps_len;

    packet->m_packetType = RTMP_PACKET_TYPE_VIDEO;
    packet->m_nBodySize = i;
    packet->m_nChannel = 0x04;
    packet->m_nTimeStamp = nTimestamp;
    packet->m_hasAbsTimestamp = 0;
    packet->m_headerType = RTMP_PACKET_SIZE_MEDIUM;
    packet->m_nInfoField2 = m_pRtmp->m_stream_id;

    /*调用发送接口*/
    int nRet = RTMP_SendPacket(m_pRtmp, packet, TRUE);
    rtmp264_free_pps_sps();
    free(packet);//释放内存
    return nRet;
}

int rtmp264_send_frame(unsigned char *data, unsigned int size, int bIsKeyFrame, unsigned int nTimeStamp)
{
    if (NULL == data && size < 11)
    {
        printf("[%s:%d]invalid param\n", __FUNCTION__, __LINE__);
        return -1;
    }
    unsigned char *body = (unsigned char *)malloc(size + 9);
    memset(body, 0, size + 9);
    int i = 0;
    if (bIsKeyFrame)
    {
        body[i++] = 0x17;// 1:Iframe  7:AVC
        body[i++] = 0x01;// AVC NALU
        body[i++] = 0x00;
        body[i++] = 0x00;
        body[i++] = 0x00;
        // NALU size
        body[i++] = size >> 24 & 0xff;
        body[i++] = size >> 16 & 0xff;
        body[i++] = size >> 8 & 0xff;
        body[i++] = size & 0xff;
        // NALU data
        memcpy(&body[i], data, size);
        rtmp264_send_sps_pps(metaData.Pps, metaData.nPpsLen, metaData.Sps, metaData.nSpsLen, nTimeStamp);
    }
    else
    {
        body[i++] = 0x27;// 2:Pframe  7:AVC
        body[i++] = 0x01;// AVC NALU
        body[i++] = 0x00;
        body[i++] = 0x00;
        body[i++] = 0x00;
        // NALU size
        body[i++] = size >> 24 & 0xff;
        body[i++] = size >> 16 & 0xff;
        body[i++] = size >> 8 & 0xff;
        body[i++] = size & 0xff;
        // NALU data
        memcpy(&body[i], data, size);
    }
    int bRet = rtmp264_send_packet(RTMP_PACKET_TYPE_VIDEO, body, i + size, nTimeStamp);
    free(body);

    return bRet;
}

int test_rtmp(void)
{
    rtmp264_init();
    //初始化并连接到服务器
    rtmp264_connect("rtmp://192.168.126.199:1935/live/stream");

    //打开H264视频文件
    FILE *file = fopen(H264_FILE, "r");
    if(NULL == file)
    {
        printf("open file failed..\n");
        return -1;
    }
    struct stat statbuf;
    int    start_send = 0;
    //获取文件长度
    stat(H264_FILE, &statbuf);
    int file_size_cur = 0;
    int file_size = statbuf.st_size;
    unsigned int file_index = 0;
    packet_info pPacker = {0};
    frame_info frame = {0};
    pPacker.s64CurPts = 10000;

    unsigned char *buf = (unsigned char *)calloc(1, MAX_FILE_LEN);
    if(buf == NULL)
    {
        return -1;
    }
    int first_idr = 1;
    unsigned char *send_buf = (unsigned char *)calloc(1, MAX_FILE_LEN);
    int send_len = 0;
    int frameRate = 25;
    while(1)
    {
        fseek(file, file_index, 0);
        memset(buf, 0, MAX_FILE_LEN);
        int read_len = fread(buf, 1, MAX_FILE_LEN, file);
        if(read_len <= 0)
        {
            file_size_cur = 0;
            break;
        }
        //printf("1#file_index:%d read_len:%d file_size_cur:%d\n", file_index, read_len, file_size_cur);
        if (file_size_cur + read_len >= file_size)
        {
            printf("file end...\n");
            file_index = 0;
            file_size_cur = 0;
            fseek(file, file_index, 0);
            continue;
        }
        memset(&frame, 0, sizeof(frame));
        if(0 == get_h264_frame(buf, &frame))
        {
            file_size_cur += frame.len;
            file_index += frame.len;
            switch (frame.type)
            {
            case NAL_TYPE_SPS:
            {
                printf("NAL_TYPE_SPS\n");
                sps_t sps = {0};
                //offset 00 00 00 01 27 ...[data]
                int offset = frame.start_code_len + 1;
                parse_h264_sps(buf + offset, frame.len - offset, &sps);
                //printf("b_fixed_frame_rate:%d\n", sps.vui.b_fixed_frame_rate);
                //printf("i_time_scale:%d\n", sps.vui.i_time_scale);
                //printf("i_num_units_in_tick:%d\n", sps.vui.i_num_units_in_tick);
                if (sps.vui.b_fixed_frame_rate)
                {
                    frameRate = sps.vui.i_time_scale / (2 * sps.vui.i_num_units_in_tick);
                }
                else
                {
                    frameRate = sps.vui.i_time_scale / sps.vui.i_num_units_in_tick;
                }
                printf("frameRate:%d width*height:%d*%d\n", frameRate, (sps.pic_width_in_mbs_minus1 + 1) * 16,
                       (sps.pic_height_in_map_units_minus1 + 1) * 16);
                memcpy(send_buf, buf + frame.start_code_len, frame.len - frame.start_code_len);
                send_len += frame.len - frame.start_code_len;
                rtmp264_save_sps(send_buf, send_len);
                memset(send_buf, 0, send_len);
                send_len = 0;
                break;
            }
            case NAL_TYPE_PPS:
            {
                printf("NAL_TYPE_PPS\n");
                memcpy(send_buf, buf + frame.start_code_len, frame.len - frame.start_code_len);
                send_len += frame.len - frame.start_code_len;
                rtmp264_save_pps(send_buf, send_len);
                memset(send_buf, 0, send_len);
                send_len = 0;
                break;
            }
            case NAL_TYPE_DELIMITER:
            {
                //printf("NAL_TYPE_PPS\n");
                //memcpy(send_buf + send_len, buf, frame.len);
                //send_len += frame.len;
                break;
            }
            case NAL_TYPE_IDR:
            {
                printf("NAL_TYPE_IDR\n");
                //rtmp单位为ms 25fps增量36ms 30fps增量33ms
                pPacker.s64CurPts += 1000 / frameRate;
                pPacker.IFrame = 1;
                send_len = frame.len - frame.start_code_len;
                memcpy(send_buf, buf + frame.start_code_len, send_len);
                //pack_h264_stream(send_buf, send_len, &pPacker, &head);
                rtmp264_send_frame(send_buf, send_len, pPacker.IFrame, (unsigned int)pPacker.s64CurPts);
                memset(send_buf, 0, send_len);
                send_len = 0;
                usleep(1000 * 1000 / frameRate - 5 * 1000);
                break;
            }
            case NAL_TYPE_NOTIDR:
            {
                //printf("NAL_TYPE_NOTIDR\n");
                pPacker.IFrame = 0;
                //rtmp单位为ms 25fps增量36ms 30fps增量33ms
                pPacker.s64CurPts += 1000 / frameRate;
                send_len += frame.len - frame.start_code_len;
                memcpy(send_buf, buf + frame.start_code_len, send_len);
                //pack_h264_stream(send_buf, send_len, &pPacker, &head);
                rtmp264_send_frame(send_buf, send_len, pPacker.IFrame, (unsigned int)pPacker.s64CurPts);
                memset(send_buf, 0, send_len);
                send_len = 0;
                usleep(1000 * 1000 / frameRate - 5 * 1000);
                break;
            }
            }

        }
        else
        {
            usleep(1000 * 1000);
        }
    }
    free(buf);
    fclose(file);
    rtmp264_unit();

    return 0;
}


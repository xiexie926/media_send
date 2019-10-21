#include <sys/types.h>
#include "media_send.h"

#define TEST_FILE 0

static inline void bits_write( bits_buffer_t *p_buffer, int i_count, uint64_t i_bits);
static int check_start_code(unsigned char *p);
static int pack_pes_header(char *pData, int stream_id, unsigned int payload_len, unsigned long long pts, unsigned long long dts);
static int pack_psm_header(char *pData);
static int pack_sys_header(char *pData);
static int pack_ps_header(char *pData, unsigned long long s64Scr);
static int send_rtp_pack(char *pdata, int nDataLen, packet_info *pPacker, rtp_pack_head *phead);

static inline void bits_write( bits_buffer_t *p_buffer, int i_count, uint64_t i_bits)
{
    while( i_count > 0 )
    {
        i_count--;
        if( ( i_bits >> i_count ) & 0x01 )
        {
            p_buffer->p_data[p_buffer->i_data] |= p_buffer->i_mask;
        }
        else
        {
            p_buffer->p_data[p_buffer->i_data] &= ~p_buffer->i_mask;
        }
        p_buffer->i_mask >>= 1;
        if( p_buffer->i_mask == 0 )
        {
            p_buffer->i_data++;
            p_buffer->i_mask = 0x80;
        }
    }
}

int sock_udp_open(int sock_type)
{
    int fd = socket(AF_INET, sock_type, 0);
    if (fd <= 0)
    {
        printf("sock_open failed errno: 0x%x \n", errno);
        return -1;
    }

    return fd;
}

void sock_udp_close(int sock_fd)
{
    if (sock_fd > 0)
    {
        close(sock_fd);
    }
}

int udp_sock_send(int sock_fd, char *ip, int port, char *buffer, int len)
{
    if ((NULL == ip) || (NULL == buffer) || (len <= 0))
    {
        printf("Invalid param !\n");
        return 0;
    }
    struct sockaddr_in addr;
    addr.sin_family        = AF_INET;
    addr.sin_port          = htons(port);
    addr.sin_addr.s_addr   = inet_addr(ip);
    memset(&addr.sin_zero, 0, 8);

    int ret = sendto(sock_fd, buffer, len, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    if (ret < 0)
    {
        if ((errno == EWOULDBLOCK) || (errno == EINTR))
        {
            printf("udp send failed \n");
            return -1; /* would block */
        }
        //perror("sendto fail:");

        return 0;
    }

    return ret;
}

int check_start_code(unsigned char *p)
{
    if((*(p + 0) == 0x00) && (*(p + 1) == 0x00) && (*(p + 2) == 0x00) && (*(p + 3) == 0x01))
        return 4;
    else if((*(p + 0) == 0x00) && (*(p + 1) == 0x00) && (*(p + 2) == 0x01))
        return 3;
    else
        return 0;
}

int get_h264_frame(unsigned char *buf, frame_info *pframe)
{
    if ((NULL == buf) || (NULL == pframe))
    {
        printf("[%s:%d]invalid param\n", __FUNCTION__, __LINE__);
        return -1;
    }
    unsigned int len = 0;
    int nal_type = 0;
    unsigned char *databuf = NULL;
    unsigned char *start_pos = NULL;
    unsigned char *end_pos = NULL;
    unsigned char *p = buf;
    int start_code_len = 0;
    while (p)
    {
        if (3 == check_start_code(p))
        {
            start_code_len = 3;
            start_pos = p;
            p += 3;
            break;
        }
        else if (4 == check_start_code(p))
        {
            start_code_len = 4;
            start_pos = p;
            p += 4;
            break;
        }
        else
        {
            p++;
        }
    }
    if (NULL == start_pos)
    {
        printf("not found start code..\n");
        return -1;
    }
    while(p)
    {
        if(check_start_code(p) > 0)
        {
            end_pos = p;
            break;
        }
        else
        {
            p++;
        }
    }
    if (NULL == end_pos)
    {
        printf("not found end code..\n");
        return -1;
    }
    len = end_pos - start_pos;
    nal_type = H264_GET_NALTYPE(*(start_pos + start_code_len));
    pframe->type = nal_type;
    pframe->len = len;
    pframe->start_code_len = start_code_len;
    return 0;
}

static int pack_pes_header(char *pData, int stream_id, unsigned int payload_len, unsigned long long pts, unsigned long long dts)
{
    bits_buffer_t bitsBuffer;
    bitsBuffer.i_size = PES_HDR_LEN;
    bitsBuffer.i_data = 0;
    bitsBuffer.i_mask = 0x80;
    bitsBuffer.p_data = (unsigned char *)(pData);
    if(pts == 0)
    {
        memset(bitsBuffer.p_data, 0, PES_HDR_LEN - 10);
    }
    else
    {
        memset(bitsBuffer.p_data, 0, PES_HDR_LEN);
    }
    /*system header*/
    bits_write(&bitsBuffer, 24, 0x000001); /*start code*/
    bits_write(&bitsBuffer, 8, stream_id);   /*streamID*/
    //payload_len不能大于65535
    if (pts == 0)
    {
        bits_write(&bitsBuffer, 16, payload_len + 3);  /*packet_len*/ //指出pes分组中数据长度和该字节后的长度和
    }
    else
    {
        bits_write(&bitsBuffer, 16, payload_len + 13);  /*packet_len*/ //指出pes分组中数据长度和该字节后的长度和
    }
    bits_write(&bitsBuffer, 2, 2);        /*'10'*/
    bits_write(&bitsBuffer, 2, 0);        /*scrambling_control*/
    bits_write(&bitsBuffer, 1, 0);        /*priority*/
    bits_write(&bitsBuffer, 1, 0);        /*data_alignment_indicator*/
    bits_write(&bitsBuffer, 1, 0);        /*copyright*/
    bits_write(&bitsBuffer, 1, 0);        /*original_or_copy*/
    if (pts == 0)
    {
        bits_write(&bitsBuffer, 1, 0);        /*PTS_flag*/
        bits_write(&bitsBuffer, 1, 0);        /*DTS_flag*/
    }
    else
    {
        bits_write(&bitsBuffer, 1, 1);        /*PTS_flag*/
        bits_write(&bitsBuffer, 1, 1);        /*DTS_flag*/
    }
    bits_write(&bitsBuffer, 1, 0);        /*ESCR_flag*/
    bits_write(&bitsBuffer, 1, 0);        /*ES_rate_flag*/
    bits_write(&bitsBuffer, 1, 0);        /*DSM_trick_mode_flag*/
    bits_write(&bitsBuffer, 1, 0);        /*additional_copy_info_flag*/
    bits_write(&bitsBuffer, 1, 0);        /*PES_CRC_flag*/
    bits_write(&bitsBuffer, 1, 0);        /*PES_extension_flag*/  //30
    if (pts == 0)
    {
        bits_write(&bitsBuffer, 8, 0);        /*header_data_length*/
    }
    else
    {
        bits_write(&bitsBuffer, 8, 10);        /*header_data_length*/
        // 指出包含在 PES 分组标题中的可选字段和任何填充字节所占用的总字节数。该字段之前的字节指出了有无可选字段。
        bits_write(&bitsBuffer, 4, 3);                    /*'0011'*/
        bits_write(&bitsBuffer, 3, ((pts) >> 30) & 0x07 ); /*PTS[32..30]*/
        bits_write(&bitsBuffer, 1, 1);
        bits_write(&bitsBuffer, 15, ((pts) >> 15) & 0x7FFF); /*PTS[29..15]*/
        bits_write(&bitsBuffer, 1, 1);
        bits_write(&bitsBuffer, 15, (pts) & 0x7FFF);       /*PTS[14..0]*/
        bits_write(&bitsBuffer, 1, 1);
        bits_write(&bitsBuffer, 4, 1);                    /*'0001'*/
        bits_write(&bitsBuffer, 3, ((dts) >> 30) & 0x07 ); /*DTS[32..30]*/
        bits_write(&bitsBuffer, 1, 1);
        bits_write(&bitsBuffer, 15, ((dts) >> 15) & 0x7FFF); /*DTS[29..15]*/
        bits_write(&bitsBuffer, 1, 1);
        bits_write(&bitsBuffer, 15, (dts) & 0x7FFF);       /*DTS[14..0]*/
        bits_write(&bitsBuffer, 1, 1);
    }
    return 0;
}

static int pack_psm_header(char *pData)
{
    bits_buffer_t bitsBuffer;
    bitsBuffer.i_size = PSM_HDR_LEN;
    bitsBuffer.i_data = 0;
    bitsBuffer.i_mask = 0x80;
    bitsBuffer.p_data = (unsigned char *)(pData);
    memset(bitsBuffer.p_data, 0, PSM_HDR_LEN);
    bits_write(&bitsBuffer, 24, 0x000001);  /*start code*/
    bits_write(&bitsBuffer, 8, 0xBC);       /*map stream id*/
    bits_write(&bitsBuffer, 16, 14);        /*program stream map length*/
    bits_write(&bitsBuffer, 1, 1);          /*current next indicator */
    bits_write(&bitsBuffer, 2, 3);          /*reserved*/
    bits_write(&bitsBuffer, 5, 1);          /*program stream map version*/
    bits_write(&bitsBuffer, 7, 0x7F);       /*reserved */
    bits_write(&bitsBuffer, 1, 1);          /*marker bit */
    bits_write(&bitsBuffer, 16, 0);         /*programe stream info length*/
    bits_write(&bitsBuffer, 16, 4);         /*elementary stream map length  is*/
#if 0
    /*audio*/
    bits_write(&bitsBuffer, 8, 0x90);       /*stream_type*/
    bits_write(&bitsBuffer, 8, 0xC0);       /*elementary_stream_id*/
    bits_write(&bitsBuffer, 16, 0);         /*elementary_stream_info_length is*/
#endif
    /*video*/
    bits_write(&bitsBuffer, 8, 0x1B);       /*stream_type*/
    bits_write(&bitsBuffer, 8, 0xE0);       /*elementary_stream_id*/
    bits_write(&bitsBuffer, 8, 0x00);       /*elementary_stream_id*/
    bits_write(&bitsBuffer, 8, 0x00);       /*elementary_stream_id*/
    /*crc (53 12 F5 5C)*/
    bits_write(&bitsBuffer, 8, 0x53);       /*crc (24~31) bits*/
    bits_write(&bitsBuffer, 8, 0x12);       /*crc (16~23) bits*/
    bits_write(&bitsBuffer, 8, 0xF5);       /*crc (8~15) bits*/
    bits_write(&bitsBuffer, 8, 0x5C);       /*crc (0~7) bits*/

    return 0;
}

static int pack_sys_header(char *pData)
{
    bits_buffer_t bitsBuffer;
    bitsBuffer.i_size = SYS_HDR_LEN;
    bitsBuffer.i_data = 0;
    bitsBuffer.i_mask = 0x80;
    bitsBuffer.p_data = (unsigned char *)(pData);
    memset(bitsBuffer.p_data, 0, SYS_HDR_LEN);
    /*system header*/
    bits_write(&bitsBuffer, 32, 0x000001BB);   /*start code*/
    bits_write(&bitsBuffer, 16, 12 - 6 + 3 * 1); /*header_length 表示次字节后面的长度，后面的相关头也是次意思*/
    bits_write(&bitsBuffer, 1, 1);            /*marker_bit*/
    bits_write(&bitsBuffer, 22, 0);        /*rate_bound*/
    bits_write(&bitsBuffer, 1, 1);            /*marker_bit*/
    bits_write(&bitsBuffer, 6, 0);            /*audio_bound*/
    bits_write(&bitsBuffer, 1, 0);            /*fixed_flag */
    bits_write(&bitsBuffer, 1, 0);            /*CSPS_flag */
    bits_write(&bitsBuffer, 1, 0);            /*system_audio_lock_flag*/
    bits_write(&bitsBuffer, 1, 0);            /*system_video_lock_flag*/
    bits_write(&bitsBuffer, 1, 1);            /*marker_bit*/
    bits_write(&bitsBuffer, 5, 1);            /*video_bound*/
    bits_write(&bitsBuffer, 1, 1);            /*dif from mpeg1*/
    bits_write(&bitsBuffer, 7, 0x7F);         /*reserver*/
    /*audio stream bound*/
#if 0
    bits_write( &bitsBuffer, 8,  0xC0);         /*stream_id*/
    bits_write( &bitsBuffer, 2,  3);            /*marker_bit */
    bits_write( &bitsBuffer, 1,  0);            /*PSTD_buffer_bound_scale*/
    bits_write( &bitsBuffer, 13, 512);          /*PSTD_buffer_size_bound*/
#endif
    /*video stream bound*/
    bits_write(&bitsBuffer, 8, 0xE0);         /*stream_id*/
    bits_write(&bitsBuffer, 2, 3);            /*marker_bit */
    bits_write(&bitsBuffer, 1, 1);            /*PSTD_buffer_bound_scale*/
    bits_write(&bitsBuffer, 13, 400);         /*PSTD_buffer_size_bound*/
    return 0;
}

static int pack_ps_header(char *pData, unsigned long long s64Scr)
{
    unsigned long long lScrExt = 0;
    bits_buffer_t bitsBuffer;
    bitsBuffer.i_size = PS_HDR_LEN;
    bitsBuffer.i_data = 0;
    bitsBuffer.i_mask = 0x80; // 二进制：10000000 这里是为了后面对一个字节的每一位进行操作，避免大小端夸字节字序错乱
    bitsBuffer.p_data = (unsigned char *)pData;
    memset(bitsBuffer.p_data, 0, PS_HDR_LEN);
    bits_write(&bitsBuffer, 32, 0x000001BA);            /*start codes*/
    bits_write(&bitsBuffer, 2,  1);                     /*marker bits '01b'*/
    bits_write(&bitsBuffer, 3,  (s64Scr >> 30) & 0x07); /*System clock [32..30]*/
    bits_write(&bitsBuffer, 1,  1);                     /*marker bit*/
    bits_write(&bitsBuffer, 15, (s64Scr >> 15) & 0x7FFF); /*System clock [29..15]*/
    bits_write(&bitsBuffer, 1,  1);                     /*marker bit*/
    bits_write(&bitsBuffer, 15, s64Scr & 0x7fff);       /*System clock [14..0]*/
    bits_write(&bitsBuffer, 1,  1);                     /*marker bit*/
    bits_write(&bitsBuffer, 9,  lScrExt & 0x01ff);      /*System clock [14..0]*/
    bits_write(&bitsBuffer, 1,  1);                     /*marker bit*/
    bits_write(&bitsBuffer, 22, 25480 & 0x3fffff);      /*bit rate(n units of 50 bytes per second.)*/
    bits_write(&bitsBuffer, 2,  3);                     /*marker bits '11'*/
    bits_write(&bitsBuffer, 5,  0x1f);                  /*reserved(reserved for future use)*/
    bits_write(&bitsBuffer, 3,  0);                     /*stuffing length*/
    return 0;
}

int pack_ps_stream(char *pData, int nFrameLen, packet_info *pPacker, int stream_type)
{
    if ((NULL == pData) || (NULL == pPacker) || (0 == nFrameLen))
    {
        printf("[%s:%d]invalid param\n", __FUNCTION__, __LINE__);
        return -1;
    }
    char head_buf[128] = {0};
    int  nSizePos = 0;
    char *pBuff = NULL;
    pack_ps_header(head_buf + nSizePos, pPacker->s64CurPts);
    nSizePos += PS_HDR_LEN;
    if(pPacker->IFrame == 1)
    {
        pack_sys_header(head_buf + nSizePos);
        nSizePos += SYS_HDR_LEN;
    }
    pack_psm_header(head_buf + nSizePos);
    nSizePos += PSM_HDR_LEN;
#if TEST_FILE
    static FILE *fp = NULL;
    if (NULL == fp)
    {
        fp = fopen("test.ps", "wb");
    }
    if (NULL != fp)
    {
        fwrite(head_buf, 1, nSizePos, fp);
    }
#endif
    unsigned int nsize = 0;
    int buf_pos = 0;
    int remain_len = nFrameLen;
    int first_div_pack = 1;
    rtp_pack_head head = {0};
    head.ssrc = htonl(0x55667788);
    head.timtamp = pPacker->s64CurPts;
    head.payload = H264_PAYLOAD;
    while(remain_len > 0)
    {
        //printf("[%s:%d]1#remain_len:%d nsize:%d buf_pos:%d\n",__FUNCTION__,__LINE__,remain_len, nsize, buf_pos);
        if((remain_len - MAX_PES_DATA_LEN) >= 0)
        {
            nsize = MAX_PES_DATA_LEN;
            remain_len -= MAX_PES_DATA_LEN;
        }
        else
        {
            nsize = remain_len;
            remain_len = 0;
        }
        //printf("[%s:%d]2#remain_len:%d nsize:%d buf_pos:%d nSizePos:%d\n",__FUNCTION__,__LINE__,remain_len, nsize, buf_pos, nSizePos);
        pBuff = (char *)calloc(1, PES_HDR_LEN + nsize + nSizePos);
        if(pBuff == NULL)
        {
            printf("[%s:%d]calloc fail\n", __FUNCTION__, __LINE__);
            return -1;
        }
        memcpy(pBuff, head_buf, nSizePos);
        memcpy(pBuff + nSizePos + PES_HDR_LEN, pData + buf_pos, nsize);
        buf_pos += nsize;
        //first_div_pack = 0;
        int pts = pPacker->s64CurPts;
        pack_pes_header(pBuff + nSizePos, stream_type ? 0xC0 : 0xE0, nsize, pts, pts - 3000);
#if TEST_FILE
        if (NULL != fp)
        {
            //printf("write %d bytes \n", (PES_HDR_LEN + nFrameLen));
            fwrite(pBuff, 1, PES_HDR_LEN + nsize + nSizePos, fp);
        }
#endif
        send_rtp_pack(pBuff, nsize + PES_HDR_LEN + nSizePos, pPacker, &head);
        free(pBuff);
        pBuff = NULL;
    }

    return 0;
}

int pack_h264_stream(char *pdata, int nDataLen, packet_info *pPacker, rtp_pack_head *phead)
{
    static unsigned short sernum = 397;
    phead->sernum = sernum;
    rtp_head  *pRtpHead = NULL;
    int remain_len = nDataLen;
    int data_pos = 0;
    int data_len = 0;
    int start_bit = 0;
    int end_bit = 0;
    int nalu_type = 0;
    int first_pack = 1;
    int is_fua_packet = 0;
    nalu_type = pdata[0] & 0x1f;
    char *rtp_buff = NULL;
    int rtp_buff_len = 0;
    unsigned char fu_identifer = 0;
    unsigned char fu_header = 0;
    //printf("nalu_type:%d\n", nalu_type);
    while(remain_len > 0)
    {
        //printf("1#data_len:%d remain_len:%d\n", data_len, remain_len);
        if(remain_len > MAX_PACK_LEN - RTP_HEAD_LEN - 2)
        {
            is_fua_packet = 1;
            rtp_buff_len = MAX_PACK_LEN;
            data_len = rtp_buff_len - RTP_HEAD_LEN - 2;
            remain_len -= rtp_buff_len - RTP_HEAD_LEN - 2;
            rtp_buff = (char *)calloc(1, rtp_buff_len);
            if(rtp_buff == NULL)
            {
                printf("[%s:%d]calloc fail\n", __FUNCTION__, __LINE__);
                return -1;
            }
            pRtpHead = (rtp_head *)rtp_buff;
            pRtpHead->u1Marker    = 0;
            if(first_pack)
            {
                start_bit = 1;
                first_pack = 0;
                data_pos++;
            }
            else
            {
                start_bit = 0;
            }

        }
        else
        {
            data_len = remain_len;
            if (is_fua_packet)
            {
                rtp_buff_len = data_len + RTP_HEAD_LEN + 2;
            }
            else
            {
                rtp_buff_len = data_len + RTP_HEAD_LEN;
            }
            remain_len -= data_len;
            rtp_buff = (char *)calloc(1, rtp_buff_len);
            if(rtp_buff == NULL)
            {
                printf("[%s:%d]calloc fail\n", __FUNCTION__, __LINE__);
                return -1;
            }
            pRtpHead = (rtp_head *)rtp_buff;
            if((NAL_TYPE_SPS == nalu_type) || (NAL_TYPE_PPS == nalu_type))
            {
                pRtpHead->u1Marker    = 0;
            }
            else
            {
                pRtpHead->u1Marker    = 1;
            }
            end_bit = 1;
        }
        //printf("2#data_len:%d remain_len:%d data_pos:%d rtp_buff_len:%d\n", data_len, remain_len, data_pos, rtp_buff_len);

        pRtpHead->u7Payload   = phead->payload;
        pRtpHead->u2Version   = 2;
        pRtpHead->u32SSrc     = phead->ssrc;
        pRtpHead->u16SeqNum   = htons(phead->sernum);
        pRtpHead->u32TimeStamp = htonl(phead->timtamp);

        if(is_fua_packet)
        {
            fu_identifer = (0 << 7) | (3 << 5) | (RTP_FU_A_TYPE & 0x1f);
            //printf("fu_identifer:%02x\n", fu_identifer);
            fu_header = (start_bit << 7) | (end_bit << 6) | (nalu_type & 0x1f);
            rtp_buff[12] = fu_identifer;
            rtp_buff[13] = fu_header;
            memcpy(rtp_buff + RTP_HEAD_LEN + 2, pdata + data_pos, data_len);
            data_pos += data_len;
        }
        else
        {
            memcpy(rtp_buff + RTP_HEAD_LEN, pdata + data_pos, data_len);
            data_pos += data_len;
        }
        udp_sock_send(pPacker->sock_fd, pPacker->recv_ip, pPacker->recv_port, rtp_buff, rtp_buff_len);
        static int count = 0;
        if(count++ % 200 == 0)
        {
            printf("udp send to %s:%d %d bytes \n", pPacker->recv_ip, pPacker->recv_port, rtp_buff_len);
        }
        free(rtp_buff);
        rtp_buff = NULL;
        sernum++;
        phead->sernum = sernum;
        //udp 发送过快会丢包
        usleep(10);
    }
    return 0;
}

static int send_rtp_pack(char *pdata, int nDataLen, packet_info *pPacker, rtp_pack_head *phead)
{
    static unsigned short sernum = 0;
    phead->sernum = sernum;
    rtp_head  *pRtpHead = NULL;
    int remain_len = nDataLen;
    int data_pos = 0;
    int data_len = 0;
    while(remain_len > 0)
    {
        char *rtp_buff = NULL;
        int rtp_buff_len = 0;
        //printf("1#data_len:%d remain_len:%d\n", data_len, remain_len);
        if(remain_len > MAX_PACK_LEN - RTP_HEAD_LEN)
        {
            rtp_buff_len = MAX_PACK_LEN;
            data_len = rtp_buff_len - RTP_HEAD_LEN;
            remain_len -= rtp_buff_len - RTP_HEAD_LEN;
            rtp_buff = (char *)calloc(1, rtp_buff_len);
            if(rtp_buff == NULL)
            {
                printf("[%s:%d]calloc fail\n", __FUNCTION__, __LINE__);
                return -1;
            }
            pRtpHead = (rtp_head *)rtp_buff;
            pRtpHead->u1Marker    = 0;
        }
        else
        {
            data_len = remain_len;
            rtp_buff_len = data_len + RTP_HEAD_LEN;
            remain_len -= data_len;
            rtp_buff = (char *)calloc(1, rtp_buff_len);
            if(rtp_buff == NULL)
            {
                printf("[%s:%d]calloc fail\n", __FUNCTION__, __LINE__);
                return -1;
            }
            pRtpHead = (rtp_head *)rtp_buff;
            pRtpHead->u1Marker    = 1;
        }
        //printf("2#data_len:%d remain_len:%d data_pos:%d rtp_buff_len:%d\n", data_len, remain_len, data_pos, rtp_buff_len);

        pRtpHead->u7Payload   = phead->payload;
        pRtpHead->u2Version   = 2;
        pRtpHead->u32SSrc     = phead->ssrc;
        pRtpHead->u16SeqNum   = htons(phead->sernum);
        pRtpHead->u32TimeStamp = htonl(phead->timtamp);

        memcpy(rtp_buff + RTP_HEAD_LEN, pdata + data_pos, data_len);
        data_pos += data_len;
        udp_sock_send(pPacker->sock_fd, pPacker->recv_ip, pPacker->recv_port, rtp_buff, rtp_buff_len);
        static int count = 0;
        if(count++ % 200 == 0)
        {
            printf("udp send to %s:%d %d bytes \n", pPacker->recv_ip, pPacker->recv_port, rtp_buff_len);
        }
        free(rtp_buff);
        rtp_buff = NULL;
        sernum++;
        phead->sernum = sernum;
        //udp 发送过快会丢包
        usleep(10);
    }

    return 0;
}
static int nal_to_rbsp(const uint8_t *nal_buf, int *nal_size, uint8_t *rbsp_buf, int *rbsp_size)
{
    int i;
    int j     = 0;
    int count = 0;

    for( i = 0; i < *nal_size; i++ )
    {
        // in NAL unit, 0x000000, 0x000001 or 0x000002 shall not occur at any byte-aligned position
        if( ( count == 2 ) && ( nal_buf[i] < 0x03) )
        {
            return -1;
        }

        if( ( count == 2 ) && ( nal_buf[i] == 0x03) )
        {
            // check the 4th byte after 0x000003, except when cabac_zero_word is used, in which case the last three bytes of this NAL unit must be 0x000003
            if((i < *nal_size - 1) && (nal_buf[i + 1] > 0x03))
            {
                return -1;
            }

            // if cabac_zero_word is used, the final byte of this NAL unit(0x03) is discarded, and the last two bytes of RBSP must be 0x0000
            if(i == *nal_size - 1)
            {
                break;
            }

            i++;
            count = 0;
        }

        if ( j >= *rbsp_size )
        {
            // error, not enough space
            return -1;
        }

        rbsp_buf[j] = nal_buf[i];
        if(nal_buf[i] == 0x00)
        {
            count++;
        }
        else
        {
            count = 0;
        }
        j++;
    }

    *nal_size = i;
    *rbsp_size = j;
    return j;
}

int parse_h264_sps(unsigned char *buf, int len, sps_t *p_sps)
{
    if ((NULL == buf) || (NULL == p_sps))
    {
        printf("[%s:%d]invalid param\n", __FUNCTION__, __LINE__);
        return -1;
    }

    bs_t bs = {0};
    int i_tmp;
    int buf_len = len;
    int rbsp_len = len;
    int i = 0;
    uint8_t *rbsp_buf = (uint8_t *)calloc(1, rbsp_len);

    if (NULL == rbsp_buf)
    {
        printf("[%s:%d]calloc fail\n", __FUNCTION__, __LINE__);
        return -1;
    }
    nal_to_rbsp(buf, &buf_len, rbsp_buf, &rbsp_len);
    /*
    for(i = 0; i < rbsp_len; i++)
        printf("%x ", rbsp_buf[i]);
    printf("\n");
    */
    bs_init(&bs, rbsp_buf, rbsp_len);

    int i_profile_idc = bs_read( &bs, 8 );
    p_sps->i_profile = i_profile_idc;
    p_sps->i_constraint_set_flags = bs_read( &bs, 8 );
    p_sps->i_level = bs_read( &bs, 8 );
    /* sps id */
    uint32_t i_sps_id = bs_read_ue( &bs );
    if( i_sps_id > H264_SPS_ID_MAX )
        return false;
    p_sps->i_id = i_sps_id;

    if( i_profile_idc == PROFILE_H264_HIGH ||
            i_profile_idc == PROFILE_H264_HIGH_10 ||
            i_profile_idc == PROFILE_H264_HIGH_422 ||
            i_profile_idc == PROFILE_H264_HIGH_444 || /* Old one, no longer on spec */
            i_profile_idc == PROFILE_H264_HIGH_444_PREDICTIVE ||
            i_profile_idc == PROFILE_H264_CAVLC_INTRA ||
            i_profile_idc == PROFILE_H264_SVC_BASELINE ||
            i_profile_idc == PROFILE_H264_SVC_HIGH ||
            i_profile_idc == PROFILE_H264_MVC_MULTIVIEW_HIGH ||
            i_profile_idc == PROFILE_H264_MVC_STEREO_HIGH ||
            i_profile_idc == PROFILE_H264_MVC_MULTIVIEW_DEPTH_HIGH ||
            i_profile_idc == PROFILE_H264_MVC_ENHANCED_MULTIVIEW_DEPTH_HIGH ||
            i_profile_idc == PROFILE_H264_MFC_HIGH )
    {
        /* chroma_format_idc */
        p_sps->i_chroma_idc = bs_read_ue( &bs );
        if( p_sps->i_chroma_idc == 3 )
            p_sps->b_separate_colour_planes_flag = bs_read1( &bs );
        else
            p_sps->b_separate_colour_planes_flag = 0;
        /* bit_depth_luma_minus8 */
        p_sps->i_bit_depth_luma = bs_read_ue( &bs ) + 8;
        /* bit_depth_chroma_minus8 */
        p_sps->i_bit_depth_chroma = bs_read_ue( &bs ) + 8;
        /* qpprime_y_zero_transform_bypass_flag */
        bs_skip( &bs, 1 );
        /* seq_scaling_matrix_present_flag */
        i_tmp = bs_read( &bs, 1 );
        if( i_tmp )
        {
            for( int i = 0; i < ((3 != p_sps->i_chroma_idc) ? 8 : 12); i++ )
            {
                /* seq_scaling_list_present_flag[i] */
                i_tmp = bs_read( &bs, 1 );
                if( !i_tmp )
                    continue;
                const int i_size_of_scaling_list = (i < 6 ) ? 16 : 64;
                /* scaling_list (...) */
                int i_lastscale = 8;
                int i_nextscale = 8;
                for( int j = 0; j < i_size_of_scaling_list; j++ )
                {
                    if( i_nextscale != 0 )
                    {
                        /* delta_scale */
                        i_tmp = bs_read_se( &bs );
                        i_nextscale = ( i_lastscale + i_tmp + 256 ) % 256;
                        /* useDefaultScalingMatrixFlag = ... */
                    }
                    /* scalinglist[j] */
                    i_lastscale = ( i_nextscale == 0 ) ? i_lastscale : i_nextscale;
                }
            }
        }
    }
    else
    {
        p_sps->i_chroma_idc = 1; /* Not present == inferred to 4:2:0 */
        p_sps->i_bit_depth_luma = 8;
        p_sps->i_bit_depth_chroma = 8;
    }

    /* Skip i_log2_max_frame_num */
    p_sps->i_log2_max_frame_num = bs_read_ue( &bs );
    if( p_sps->i_log2_max_frame_num > 12)
        p_sps->i_log2_max_frame_num = 12;
    /* Read poc_type */
    p_sps->i_pic_order_cnt_type = bs_read_ue( &bs );
    if( p_sps->i_pic_order_cnt_type == 0 )
    {
        /* skip i_log2_max_poc_lsb */
        p_sps->i_log2_max_pic_order_cnt_lsb = bs_read_ue( &bs );
        if( p_sps->i_log2_max_pic_order_cnt_lsb > 12 )
            p_sps->i_log2_max_pic_order_cnt_lsb = 12;
    }
    else if( p_sps->i_pic_order_cnt_type == 1 )
    {
        p_sps->i_delta_pic_order_always_zero_flag = bs_read( &bs, 1 );
        p_sps->offset_for_non_ref_pic = bs_read_se( &bs );
        p_sps->offset_for_top_to_bottom_field = bs_read_se( &bs );
        p_sps->i_num_ref_frames_in_pic_order_cnt_cycle = bs_read_ue( &bs );
        if( p_sps->i_num_ref_frames_in_pic_order_cnt_cycle > 255 )
            return false;
        for( int i = 0; i < p_sps->i_num_ref_frames_in_pic_order_cnt_cycle; i++ )
            p_sps->offset_for_ref_frame[i] = bs_read_se( &bs );
    }
    /* i_num_ref_frames */
    bs_read_ue( &bs );
    /* b_gaps_in_frame_num_value_allowed */
    bs_skip( &bs, 1 );

    /* Read size */
    p_sps->pic_width_in_mbs_minus1 = bs_read_ue( &bs );
    p_sps->pic_height_in_map_units_minus1 = bs_read_ue( &bs );

    /* b_frame_mbs_only */
    p_sps->frame_mbs_only_flag = bs_read( &bs, 1 );
    if( !p_sps->frame_mbs_only_flag )
        p_sps->mb_adaptive_frame_field_flag = bs_read( &bs, 1 );

    /* b_direct8x8_inference */
    bs_skip( &bs, 1 );

    /* crop */
    if( bs_read1( &bs ) ) /* frame_cropping_flag */
    {
        p_sps->frame_crop.left_offset = bs_read_ue( &bs );
        p_sps->frame_crop.right_offset = bs_read_ue( &bs );
        p_sps->frame_crop.top_offset = bs_read_ue( &bs );
        p_sps->frame_crop.bottom_offset = bs_read_ue( &bs );
    }

    /* vui */
    i_tmp = bs_read( &bs, 1 );
    if( i_tmp )
    {
        p_sps->vui.b_valid = true;
        /* read the aspect ratio part if any */
        i_tmp = bs_read( &bs, 1 );
        if( i_tmp )
        {
            static const struct
            {
                int w, h;
            } sar[17] =
            {
                { 0,   0 }, { 1,   1 }, { 12, 11 }, { 10, 11 },
                { 16, 11 }, { 40, 33 }, { 24, 11 }, { 20, 11 },
                { 32, 11 }, { 80, 33 }, { 18, 11 }, { 15, 11 },
                { 64, 33 }, { 160, 99 }, {  4,  3 }, {  3,  2 },
                {  2,  1 },
            };
            int i_sar = bs_read( &bs, 8 );
            int w, h;

            if( i_sar < 17 )
            {
                w = sar[i_sar].w;
                h = sar[i_sar].h;
            }
            else if( i_sar == 255 )
            {
                w = bs_read( &bs, 16 );
                h = bs_read( &bs, 16 );
            }
            else
            {
                w = 0;
                h = 0;
            }

            if( w != 0 && h != 0 )
            {
                p_sps->vui.i_sar_num = w;
                p_sps->vui.i_sar_den = h;
            }
            else
            {
                p_sps->vui.i_sar_num = 1;
                p_sps->vui.i_sar_den = 1;
            }
        }

        /* overscan */
        i_tmp = bs_read( &bs, 1 );
        if ( i_tmp )
            bs_read( &bs, 1 );

        /* video signal type */
        i_tmp = bs_read( &bs, 1 );
        if( i_tmp )
        {
            bs_read( &bs, 3 );
            p_sps->vui.colour.b_full_range = bs_read( &bs, 1 );
            /* colour desc */
            i_tmp = bs_read( &bs, 1 );
            if ( i_tmp )
            {
                p_sps->vui.colour.i_colour_primaries = bs_read( &bs, 8 );
                p_sps->vui.colour.i_transfer_characteristics = bs_read( &bs, 8 );
                p_sps->vui.colour.i_matrix_coefficients = bs_read( &bs, 8 );
            }
            else
            {
                p_sps->vui.colour.i_colour_primaries = ISO_23001_8_CP_UNSPECIFIED;
                p_sps->vui.colour.i_transfer_characteristics = ISO_23001_8_TC_UNSPECIFIED;
                p_sps->vui.colour.i_matrix_coefficients = ISO_23001_8_MC_UNSPECIFIED;
            }
        }

        /* chroma loc info */
        i_tmp = bs_read( &bs, 1 );
        if( i_tmp )
        {
            bs_read_ue( &bs );
            bs_read_ue( &bs );
        }

        /* timing info */
        p_sps->vui.b_timing_info_present_flag = bs_read( &bs, 1 );
        if( p_sps->vui.b_timing_info_present_flag )
        {
            p_sps->vui.i_num_units_in_tick = bs_read( &bs, 32 );
            p_sps->vui.i_time_scale = bs_read( &bs, 32 );
            p_sps->vui.b_fixed_frame_rate = bs_read( &bs, 1 );
        }

        /* Nal hrd & VC1 hrd parameters */
        p_sps->vui.b_hrd_parameters_present_flag = false;
        for ( int i = 0; i < 2; i++ )
        {
            i_tmp = bs_read( &bs, 1 );
            if( i_tmp )
            {
                p_sps->vui.b_hrd_parameters_present_flag = true;
                uint32_t count = bs_read_ue( &bs ) + 1;
                if( count > 31 )
                    return false;
                bs_read( &bs, 4 );
                bs_read( &bs, 4 );
                for( uint32_t j = 0; j < count; j++ )
                {
                    if( bs_remain( &bs ) < 23 )
                        return false;
                    bs_read_ue( &bs );
                    bs_read_ue( &bs );
                    bs_read( &bs, 1 );
                }
                bs_read( &bs, 5 );
                p_sps->vui.i_cpb_removal_delay_length_minus1 = bs_read( &bs, 5 );
                p_sps->vui.i_dpb_output_delay_length_minus1 = bs_read( &bs, 5 );
                bs_read( &bs, 5 );
            }
        }

        if( p_sps->vui.b_hrd_parameters_present_flag )
            bs_read( &bs, 1 ); /* low delay hrd */

        /* pic struct info */
        p_sps->vui.b_pic_struct_present_flag = bs_read( &bs, 1 );

        p_sps->vui.b_bitstream_restriction_flag = bs_read( &bs, 1 );
        if( p_sps->vui.b_bitstream_restriction_flag )
        {
            bs_read( &bs, 1 ); /* motion vector pic boundaries */
            bs_read_ue( &bs ); /* max bytes per pic */
            bs_read_ue( &bs ); /* max bits per mb */
            bs_read_ue( &bs ); /* log2 max mv h */
            bs_read_ue( &bs ); /* log2 max mv v */
            p_sps->vui.i_max_num_reorder_frames = bs_read_ue( &bs );
            bs_read_ue( &bs ); /* max dec frame buffering */
        }
    }

    return true;
}
int parse_h264_pps(unsigned char *buf, int len, pps_t *p_pps)
{
    if ((NULL == buf) || (NULL == p_pps))
    {
        printf("[%s:%d]invalid param\n", __FUNCTION__, __LINE__);
        return -1;
    }

    bs_t bs = {0};
    int i_tmp;
    int buf_len = len;
    int rbsp_len = len;
    int i = 0;
    uint8_t *rbsp_buf = (uint8_t *)calloc(1, rbsp_len);
    if (NULL == rbsp_buf)
    {
        printf("[%s:%d]calloc fail\n", __FUNCTION__, __LINE__);
        return -1;
    }
    nal_to_rbsp(buf, &buf_len, rbsp_buf, &rbsp_len);
    /*for(i = 0; i < rbsp_len; i++)
        printf("%x ", rbsp_buf[i]);
    printf("\n");*/
    bs_init(&bs, rbsp_buf, rbsp_len);

    uint32_t i_pps_id = bs_read_ue( &bs ); // pps id
    uint32_t i_sps_id = bs_read_ue( &bs ); // sps id
    if( i_pps_id > H264_PPS_ID_MAX || i_sps_id > H264_SPS_ID_MAX )
        return false;
    p_pps->i_id = i_pps_id;
    p_pps->i_sps_id = i_sps_id;

    bs_skip( &bs, 1 ); // entropy coding mode flag
    p_pps->i_pic_order_present_flag = bs_read( &bs, 1 );

    unsigned num_slice_groups = bs_read_ue( &bs ) + 1;
    if( num_slice_groups > 8 ) /* never has value > 7. Annex A, G & J */
        return false;
    if( num_slice_groups > 1 )
    {
        unsigned slice_group_map_type = bs_read_ue( &bs );
        if( slice_group_map_type == 0 )
        {
            for( unsigned i = 0; i < num_slice_groups; i++ )
                bs_read_ue( &bs ); /* run_length_minus1[group] */
        }
        else if( slice_group_map_type == 2 )
        {
            for( unsigned i = 0; i < num_slice_groups; i++ )
            {
                bs_read_ue( &bs ); /* top_left[group] */
                bs_read_ue( &bs ); /* bottom_right[group] */
            }
        }
        else if( slice_group_map_type > 2 && slice_group_map_type < 6 )
        {
            bs_read1( &bs );   /* slice_group_change_direction_flag */
            bs_read_ue( &bs ); /* slice_group_change_rate_minus1 */
        }
        else if( slice_group_map_type == 6 )
        {
            unsigned pic_size_in_maps_units = bs_read_ue( &bs ) + 1;
            unsigned sliceGroupSize = 1;
            while(num_slice_groups > 1)
            {
                sliceGroupSize++;
                num_slice_groups = ((num_slice_groups - 1) >> 1) + 1;
            }
            for( unsigned i = 0; i < pic_size_in_maps_units; i++ )
            {
                bs_skip( &bs, sliceGroupSize );
            }
        }
    }

    bs_read_ue( &bs ); /* num_ref_idx_l0_default_active_minus1 */
    bs_read_ue( &bs ); /* num_ref_idx_l1_default_active_minus1 */
    p_pps->weighted_pred_flag = bs_read( &bs, 1 );
    p_pps->weighted_bipred_idc = bs_read( &bs, 2 );
    bs_read_se( &bs ); /* pic_init_qp_minus26 */
    bs_read_se( &bs ); /* pic_init_qs_minus26 */
    bs_read_se( &bs ); /* chroma_qp_index_offset */
    bs_read( &bs, 1 ); /* deblocking_filter_control_present_flag */
    bs_read( &bs, 1 ); /* constrained_intra_pred_flag */
    p_pps->i_redundant_pic_present_flag = bs_read( &bs, 1 );

    /* TODO */

    return true;
}

int test_rtp_ps_stream(void)
{

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
    pPacker.s64CurPts = 853945250;

    int sock_fd = sock_udp_open(SOCK_DGRAM);
    if (sock_fd <= 0)
    {
        printf("sock_open failed\n");
        return -1;
    }
    unsigned char *buf = (unsigned char *)calloc(1, MAX_FILE_LEN);
    if(buf == NULL)
    {
        return -1;
    }
    int first_idr = 1;
    char *send_buf = (char *)calloc(1, MAX_FILE_LEN);
    int send_len = 0;
    int frameRate = 25;
    while(1)
    {
        pPacker.sock_fd = sock_fd;
        strcpy(pPacker.recv_ip, "172.24.11.146");
        pPacker.recv_port = 9878;
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
                memcpy(send_buf + send_len, buf, frame.len);
                send_len += frame.len;
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
                break;
            }
            case NAL_TYPE_PPS:
            {
                printf("NAL_TYPE_PPS\n");
                pps_t pps = {0};
                //offset 00 00 00 01 28 ...[data]
                int offset = frame.start_code_len + 1;
                parse_h264_pps(buf + offset, frame.len - offset, &pps);
                memcpy(send_buf + send_len, buf, frame.len);
                send_len += frame.len;
                break;
            }
            case NAL_TYPE_DELIMITER:
            {
                //printf("NAL_TYPE_PPS\n");
                memcpy(send_buf + send_len, buf, frame.len);
                send_len += frame.len;
                break;
            }
            case NAL_TYPE_IDR:
            {
                //printf("NAL_TYPE_IDR\n");
                memcpy(send_buf + send_len, buf, frame.len);
                send_len += frame.len;
                pPacker.IFrame = 1;
                //帧率为25fps=3600 30fps=3000
                pPacker.s64CurPts += 90 * 1000 / frameRate;
                pack_ps_stream(send_buf, send_len, &pPacker, 0);
                memset(send_buf, 0, send_len);
                send_len = 0;
                usleep(1000 * 1000 / frameRate - 10 * 1000);
                break;
            }
            case NAL_TYPE_NOTIDR:
            {
                //printf("NAL_TYPE_NOTIDR\n");
                memcpy(send_buf + send_len, buf, frame.len);
                send_len += frame.len;
                pPacker.IFrame = 0;
                //clock:90kHz 25fps增量3600 30fps增量3000
                pPacker.s64CurPts += 90 * 1000 / frameRate;
                pack_ps_stream(send_buf, send_len, &pPacker, 0);
                memset(send_buf, 0, send_len);
                send_len = 0;
                usleep(1000 * 1000 / frameRate - 10 * 1000);
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
    return 0;
}

int test_rtp_h264_stream(void)
{

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
    pPacker.s64CurPts = 1007667475;

    int sock_fd = sock_udp_open(SOCK_DGRAM);
    if (sock_fd <= 0)
    {
        printf("sock_open failed\n");
        return -1;
    }
    unsigned char *buf = (unsigned char *)calloc(1, MAX_FILE_LEN);
    if(buf == NULL)
    {
        return -1;
    }
    int first_idr = 1;
    char *send_buf = (char *)calloc(1, MAX_FILE_LEN);
    int send_len = 0;
    int frameRate = 25;
    rtp_pack_head head = {0};
    head.ssrc = htonl(0xC063C979);
    head.timtamp = pPacker.s64CurPts;
    head.payload = H264_PAYLOAD;
    while(1)
    {
        pPacker.sock_fd = sock_fd;
        strcpy(pPacker.recv_ip, "172.24.11.146");
        pPacker.recv_port = 9878;
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
                pack_h264_stream(send_buf, send_len, &pPacker, &head);
                memset(send_buf, 0, send_len);
                send_len = 0;
                break;
            }
            case NAL_TYPE_PPS:
            {
                printf("NAL_TYPE_PPS\n");
                memcpy(send_buf, buf + frame.start_code_len, frame.len - frame.start_code_len);
                send_len += frame.len - frame.start_code_len;
                pack_h264_stream(send_buf, send_len, &pPacker, &head);
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
                //帧率为25fps=3600 30fps=3000
                pPacker.s64CurPts += 90 * 1000 / frameRate;
                head.timtamp = pPacker.s64CurPts;
                if (0 != send_len)
                {
                    pPacker.IFrame = 3;
                    pack_h264_stream(send_buf, send_len, &pPacker, &head);
                    send_len = 0;
                    memset(send_buf, 0, send_len);
                }
                pPacker.IFrame = 1;
                send_len = frame.len - frame.start_code_len;
                memcpy(send_buf, buf + frame.start_code_len, send_len);
                pack_h264_stream(send_buf, send_len, &pPacker, &head);

                memset(send_buf, 0, send_len);
                send_len = 0;
                usleep(1000 * 1000 / frameRate - 10 * 1000);
                break;
            }
            case NAL_TYPE_NOTIDR:
            {
                //printf("NAL_TYPE_NOTIDR\n");
                pPacker.IFrame = 0;
                //clock:90kHz 25fps增量3600 30fps增量3000
                pPacker.s64CurPts += 90 * 1000 / frameRate;

                head.timtamp = pPacker.s64CurPts;
                send_len += frame.len - frame.start_code_len;
                memcpy(send_buf, buf + frame.start_code_len, send_len);
                pack_h264_stream(send_buf, send_len, &pPacker, &head);

                memset(send_buf, 0, send_len);
                send_len = 0;
                usleep(1000 * 1000 / frameRate - 10 * 1000);
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
    return 0;
}


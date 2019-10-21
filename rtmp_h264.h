#ifndef __RTMP_H264_H__
#define __RTMP_H264_H__

#ifdef __cplusplus
extern "C"
{
#endif

void rtmp264_init(void);
int rtmp264_connect(const char *url);
void rtmp264_unit(void);
int rtmp264_send_frame(unsigned char *data, unsigned int size, int bIsKeyFrame, unsigned int nTimeStamp);
int rtmp264_save_sps(unsigned char *data, unsigned int size);
int rtmp264_save_pps(unsigned char *data, unsigned int size);

int test_rtmp(void);

#ifdef __cplusplus
};
#endif

#endif


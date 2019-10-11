typedef struct _RTMPMetadata
{
    // video, must be h264 type
    unsigned int    nSpsLen;
    unsigned char   *Sps;
    unsigned int    nPpsLen;
    unsigned char   *Pps;
} RTMPMetadata, *LPRTMPMetadata;

extern RTMPMetadata metaData;

void rtmp264_init();
int rtmp264_connect(const char *url);
void rtmp264_unit();
int rtmp264_send_frame(unsigned char *data, unsigned int size, int bIsKeyFrame, unsigned int nTimeStamp);
int rtmp264_save_sps(unsigned char *data, unsigned int size);
int rtmp264_save_pps(unsigned char *data, unsigned int size);


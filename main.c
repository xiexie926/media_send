#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "media_send.h"
#include "rtmp_h264.h"
#include "librtmp/rtmp.h"
#include "t_ffmpeg.h"


void init_signals(void)
{
    struct sigaction sa;

    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    /* 忽略socket写入错误导致的SIGPIPE信号 */
    sigaddset(&sa.sa_mask, SIGPIPE);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

int main(int argc, char *argv[])
{
    init_signals();
    //test_rtp_ps_stream();
    //test_rtp_h264_stream();
    //test_rtmp();
    //t_ffmpeg_streamer();
    //t_ffmpeg_recv();
    //t_ffmpeg_h264();
    //t_ffmpeg_ts();
    //t_ffmpeg_ps();
    t_ffmpeg_rtmp();
    while(1)
    {
        usleep(30 * 1000);
    }
    return 0;
}

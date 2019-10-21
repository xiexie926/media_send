#include "t_ffmpeg.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"
#include "common.h"

int t_ffmpeg_recv(void)
{
    AVOutputFormat *ofmt = NULL;
    //Input AVFormatContext and Output AVFormatContext
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket pkt;
    const char *in_filename, *out_filename;
    int ret, i;
    int videoindex = -1;
    int frame_index = 0;
    in_filename  = "rtmp://192.168.126.199:1935/live/stream";
    //in_filename  = "rtp://233.233.233.233:6666";
    //out_filename = "receive.ts";
    //out_filename = "receive.mkv";
    out_filename = "receive.flv";

    av_register_all();
    //Network
    avformat_network_init();
    //Input
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0)
    {
        LOG_ERROR( "Could not open input file.");
        goto END;
    }
    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0)
    {
        LOG_ERROR( "Failed to retrieve input stream information");
        goto END;
    }

    for(i = 0; i < ifmt_ctx->nb_streams; i++)
        if(ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoindex = i;
            break;
        }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    //Output
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename); //RTMP

    if (!ofmt_ctx)
    {
        LOG_ERROR( "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto END;
    }
    ofmt = ofmt_ctx->oformat;
    for (i = 0; i < ifmt_ctx->nb_streams; i++)
    {
        //Create output AVStream according to input AVStream
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
        if (!out_stream)
        {
            LOG_ERROR( "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto END;
        }
        //Copy the settings of AVCodecContext
        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
        if (ret < 0)
        {
            LOG_ERROR( "Failed to copy context from input to output stream codec context\n");
            goto END;
        }
        out_stream->codec->codec_tag = 0;
        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    //Dump Format------------------
    av_dump_format(ofmt_ctx, 0, out_filename, 1);
    //Open output URL
    if (!(ofmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            LOG_ERROR( "Could not open output URL '%s'", out_filename);
            goto END;
        }
    }
    //Write file header
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0)
    {
        LOG_ERROR( "Error occurred when opening output URL\n");
        goto END;
    }

#if 1
    AVBitStreamFilterContext *h264bsfc =  av_bitstream_filter_init("h264_mp4toannexb");
#endif

    while (1)
    {
        AVStream *in_stream, *out_stream;
        //Get an AVPacket
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;

        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        /* copy packet */
        //Convert PTS/DTS
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        //Print to Screen
        if(pkt.stream_index == videoindex)
        {
            LOG_ERROR("Receive %8d video frames from input URL\n", frame_index);
            frame_index++;

#if 1
            av_bitstream_filter_filter(h264bsfc, in_stream->codec, NULL, &pkt.data, &pkt.size, pkt.data, pkt.size, 0);
#endif
        }
        //ret = av_write_frame(ofmt_ctx, &pkt);
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

        if (ret < 0)
        {
            LOG_ERROR( "Error muxing packet\n");
            break;
        }

        av_free_packet(&pkt);

    }

#if USE_H264BSF
    av_bitstream_filter_close(h264bsfc);
#endif

    //Write file trailer
    av_write_trailer(ofmt_ctx);
END:
    avformat_close_input(&ifmt_ctx);
    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_close(ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    if (ret < 0 && ret != AVERROR_EOF)
    {
        LOG_ERROR( "Error occurred.\n");
        return -1;
    }
    return 0;
}
int t_ffmpeg_h264(void)
{
    AVFormatContext *ifmt_ctx = NULL,*ofmt_ctx = NULL;
    AVPacket pkt = {0};
    AVStream *in_stream = NULL, *out_stream = NULL;
    int frame_index = 0;
    //char *filename = "trim.mp4";
    char *in_file = "test.h264";
    char *out_file = "rtp://172.24.11.146:9878";
    int ret = -1;
    int i = 0;
    int i_video = -1;
    av_register_all();
    avformat_network_init();
    ret = avformat_open_input(&ifmt_ctx, in_file, 0, 0);
    if (ret < 0)
    {
        LOG_ERROR("open %s failed,%d\n",in_file, ret);
        goto  END;
    }
    ret = avformat_find_stream_info(ifmt_ctx, 0);
    if (ret < 0)
    {
        LOG_ERROR("avformat_find_stream_info failed,%d\n", ret);
        goto END;
    }
    av_dump_format(ifmt_ctx, 0, in_file, 0);

    LOG_INFO("nb_streams:%d\n", ifmt_ctx->nb_streams);
    for (i = 0; i < ifmt_ctx->nb_streams; i++)
    {
        if(AVMEDIA_TYPE_VIDEO == ifmt_ctx->streams[0]->codec->codec_type)
        {
            i_video = i;
            break;
        }
    }
    if (i_video < 0)
    {
        LOG_ERROR("can't find video channel\n");
        return -1;
    }
    avformat_alloc_output_context2(&ofmt_ctx, NULL, "rtp", out_file);
    
    in_stream = ifmt_ctx->streams[i_video];
    out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
    if (!out_stream)
    {
        LOG_ERROR("Failed allocating output stream\n");
        ret = AVERROR_UNKNOWN;
        goto END;
    }
    //Copy the settings of AVCodecContext
    ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
    if (ret < 0)
    {
        LOG_ERROR("Failed to copy context from input to output stream codec context\n");
        goto END;
    }
    av_dump_format(ofmt_ctx, 0, out_file, 1);

    ret = avio_open(&ofmt_ctx->pb, out_file, AVIO_FLAG_WRITE);
    if (ret < 0)
    {
        LOG_ERROR("Could not open output URL '%s\n",out_file);
        goto END;
    }
    //Write file header
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0)
    {
        LOG_ERROR("Error occurred when opening output URL\n");
        goto END;
    }
    int64_t start_time = av_gettime();
    while (1)
    {
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
        {
            LOG_INFO("read file end...");
            break;
        }
        LOG_INFO("size:%d pkt.pts:%lld", pkt.size, pkt.pts);
        if(pkt.pts == AV_NOPTS_VALUE)
        {
            //Write PTS
            AVRational time_base1 = ifmt_ctx->streams[i_video]->time_base;
            LOG_INFO("time_base1:%d %d r_frame_rate:%d %d",time_base1.num, time_base1.den,
                                    ifmt_ctx->streams[i_video]->r_frame_rate.num, ifmt_ctx->streams[i_video]->r_frame_rate.den);
            //Duration between 2 frames (us)
            int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(ifmt_ctx->streams[i_video]->r_frame_rate);
            LOG_INFO("calc_duration:%d",calc_duration);
            //Parameters
            pkt.pts = (double)(frame_index * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
            pkt.dts = pkt.pts;
            pkt.duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
        }
        LOG_INFO("size:%d pkt.pts:%lld duration:%lld", pkt.size, pkt.pts, pkt.duration);
        
        //Important:Delay
        if(pkt.stream_index == i_video)
        {
            AVRational time_base = ifmt_ctx->streams[i_video]->time_base;
            AVRational time_base_q = {1, AV_TIME_BASE};
            int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
            int64_t now_time = av_gettime() - start_time;
            if (pts_time > now_time)
                av_usleep(pts_time - now_time);
        }
        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        /* copy packet */
        //Convert PTS/DTS
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        LOG_INFO("pts:%lld ts:%lld duration:%lld", pkt.pts, pkt.dts, pkt.duration);
        //Print to Screen
        if(pkt.stream_index == i_video)
        {
            LOG_ERROR("Send %8d video frames to output URL\n", frame_index);
            frame_index++;
        }
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0)
        {
            LOG_ERROR( "Error muxing packet\n");
            break;
        }

        av_free_packet(&pkt);
    }
END:
    avformat_close_input(&ifmt_ctx);


    return 0;
}

int t_ffmpeg_ts(void)
{
    AVFormatContext *ifmt_ctx = NULL,*ofmt_ctx = NULL;
    AVPacket pkt = {0};
    AVStream *in_stream = NULL, *out_stream = NULL;
    int frame_index = 0;
    //char *filename = "trim.mp4";
    char *in_file = "test.h264";
    char *out_file = "rtp://172.24.11.146:9878";
    int ret = -1;
    int i = 0;
    int i_video = -1;
    av_register_all();
    avformat_network_init();
    ret = avformat_open_input(&ifmt_ctx, in_file, 0, 0);
    if (ret < 0)
    {
        LOG_ERROR("open %s failed,%d\n",in_file, ret);
        goto  END;
    }
    ret = avformat_find_stream_info(ifmt_ctx, 0);
    if (ret < 0)
    {
        LOG_ERROR("avformat_find_stream_info failed,%d\n", ret);
        goto END;
    }
    av_dump_format(ifmt_ctx, 0, in_file, 0);

    LOG_INFO("nb_streams:%d\n", ifmt_ctx->nb_streams);
    for (i = 0; i < ifmt_ctx->nb_streams; i++)
    {
        if(AVMEDIA_TYPE_VIDEO == ifmt_ctx->streams[0]->codec->codec_type)
        {
            i_video = i;
            break;
        }
    }
    if (i_video < 0)
    {
        LOG_ERROR("can't find video channel\n");
        return -1;
    }
    avformat_alloc_output_context2(&ofmt_ctx, NULL, "rtp_mpegts", out_file);
    
    in_stream = ifmt_ctx->streams[i_video];
    out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
    if (!out_stream)
    {
        LOG_ERROR("Failed allocating output stream\n");
        ret = AVERROR_UNKNOWN;
        goto END;
    }
    //Copy the settings of AVCodecContext
    ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
    if (ret < 0)
    {
        LOG_ERROR("Failed to copy context from input to output stream codec context\n");
        goto END;
    }
    av_dump_format(ofmt_ctx, 0, out_file, 1);

    ret = avio_open(&ofmt_ctx->pb, out_file, AVIO_FLAG_WRITE);
    if (ret < 0)
    {
        LOG_ERROR("Could not open output URL '%s\n",out_file);
        goto END;
    }
    //Write file header
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0)
    {
        LOG_ERROR("Error occurred when opening output URL\n");
        goto END;
    }
    int64_t start_time = av_gettime();
    while (1)
    {
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
        {
            LOG_INFO("read file end...");
            break;
        }
        LOG_INFO("size:%d pkt.pts:%lld", pkt.size, pkt.pts);
        if(pkt.pts == AV_NOPTS_VALUE)
        {
            //Write PTS
            AVRational time_base1 = ifmt_ctx->streams[i_video]->time_base;
            LOG_INFO("time_base1:%d %d r_frame_rate:%d %d",time_base1.num, time_base1.den,
                                    ifmt_ctx->streams[i_video]->r_frame_rate.num, ifmt_ctx->streams[i_video]->r_frame_rate.den);
            //Duration between 2 frames (us)
            int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(ifmt_ctx->streams[i_video]->r_frame_rate);
            LOG_INFO("calc_duration:%d",calc_duration);
            //Parameters
            pkt.pts = (double)(frame_index * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
            pkt.dts = pkt.pts;
            pkt.duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
        }
        LOG_INFO("size:%d pkt.pts:%lld duration:%lld", pkt.size, pkt.pts, pkt.duration);
        
        //Important:Delay
        if(pkt.stream_index == i_video)
        {
            AVRational time_base = ifmt_ctx->streams[i_video]->time_base;
            AVRational time_base_q = {1, AV_TIME_BASE};
            int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
            int64_t now_time = av_gettime() - start_time;
            if (pts_time > now_time)
                av_usleep(pts_time - now_time);
        }
        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        /* copy packet */
        //Convert PTS/DTS
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        LOG_INFO("pts:%lld ts:%lld duration:%lld", pkt.pts, pkt.dts, pkt.duration);
        //Print to Screen
        if(pkt.stream_index == i_video)
        {
            LOG_ERROR("Send %8d video frames to output URL\n", frame_index);
            frame_index++;
        }
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0)
        {
            LOG_ERROR( "Error muxing packet\n");
            break;
        }

        av_free_packet(&pkt);
    }
END:
    avformat_close_input(&ifmt_ctx);


    return 0;
}

int t_ffmpeg_rtmp(void)
{
    AVFormatContext *ifmt_ctx = NULL,*ofmt_ctx = NULL;
    AVPacket pkt = {0};
    AVStream *in_stream = NULL, *out_stream = NULL;
    int frame_index = 0;
    //char *filename = "trim.mp4";
    char *in_file = "test.h264";
    char *out_file = "rtmp://192.168.126.199:1935/live/stream";
    int ret = -1;
    int i = 0;
    int i_video = -1;
    av_register_all();
    avformat_network_init();
    ret = avformat_open_input(&ifmt_ctx, in_file, 0, 0);
    if (ret < 0)
    {
        LOG_ERROR("open %s failed,%d\n",in_file, ret);
        goto  END;
    }
    ret = avformat_find_stream_info(ifmt_ctx, 0);
    if (ret < 0)
    {
        LOG_ERROR("avformat_find_stream_info failed,%d\n", ret);
        goto END;
    }
    av_dump_format(ifmt_ctx, 0, in_file, 0);

    LOG_INFO("nb_streams:%d\n", ifmt_ctx->nb_streams);
    for (i = 0; i < ifmt_ctx->nb_streams; i++)
    {
        if(AVMEDIA_TYPE_VIDEO == ifmt_ctx->streams[0]->codec->codec_type)
        {
            i_video = i;
            break;
        }
    }
    if (i_video < 0)
    {
        LOG_ERROR("can't find video channel\n");
        return -1;
    }
    avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_file);
    
    in_stream = ifmt_ctx->streams[i_video];
    out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
    if (!out_stream)
    {
        LOG_ERROR("Failed allocating output stream\n");
        ret = AVERROR_UNKNOWN;
        goto END;
    }
    //Copy the settings of AVCodecContext
    ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
    if (ret < 0)
    {
        LOG_ERROR("Failed to copy context from input to output stream codec context\n");
        goto END;
    }
    av_dump_format(ofmt_ctx, 0, out_file, 1);

    ret = avio_open(&ofmt_ctx->pb, out_file, AVIO_FLAG_WRITE);
    if (ret < 0)
    {
        LOG_ERROR("Could not open output URL '%s\n",out_file);
        goto END;
    }
    //Write file header
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0)
    {
        LOG_ERROR("Error occurred when opening output URL\n");
        goto END;
    }
    int64_t start_time = av_gettime();
    while (1)
    {
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
        {
            LOG_INFO("read file end...");
            break;
        }
        LOG_INFO("size:%d pkt.pts:%lld", pkt.size, pkt.pts);
        if(pkt.pts == AV_NOPTS_VALUE)
        {
            //Write PTS
            AVRational time_base1 = ifmt_ctx->streams[i_video]->time_base;
            LOG_INFO("time_base1:%d %d r_frame_rate:%d %d",time_base1.num, time_base1.den,
                                    ifmt_ctx->streams[i_video]->r_frame_rate.num, ifmt_ctx->streams[i_video]->r_frame_rate.den);
            //Duration between 2 frames (us)
            int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(ifmt_ctx->streams[i_video]->r_frame_rate);
            LOG_INFO("calc_duration:%d",calc_duration);
            //Parameters
            pkt.pts = (double)(frame_index * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
            pkt.dts = pkt.pts;
            pkt.duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
        }
        LOG_INFO("size:%d pkt.pts:%lld duration:%lld", pkt.size, pkt.pts, pkt.duration);
        
        //Important:Delay
        if(pkt.stream_index == i_video)
        {
            AVRational time_base = ifmt_ctx->streams[i_video]->time_base;
            AVRational time_base_q = {1, AV_TIME_BASE};
            int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
            int64_t now_time = av_gettime() - start_time;
            if (pts_time > now_time)
                av_usleep(pts_time - now_time);
        }
        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        /* copy packet */
        //Convert PTS/DTS
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        LOG_INFO("pts:%lld ts:%lld duration:%lld", pkt.pts, pkt.dts, pkt.duration);
        //Print to Screen
        if(pkt.stream_index == i_video)
        {
            LOG_ERROR("Send %8d video frames to output URL\n", frame_index);
            frame_index++;
        }
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0)
        {
            LOG_ERROR( "Error muxing packet\n");
            break;
        }

        av_free_packet(&pkt);
    }
END:
    avformat_close_input(&ifmt_ctx);


    return 0;
}


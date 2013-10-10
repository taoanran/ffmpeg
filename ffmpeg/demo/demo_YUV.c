/*
 * A demo of ffmpeg(play)
 *
 * */

#include "config.h"
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include "libavutil/avstring.h"
#include "libavutil/colorspace.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"

#if CONFIG_AVFILTER
# include "libavfilter/avcodec.h"
# include "libavfilter/avfilter.h"
# include "libavfilter/avfiltergraph.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/buffersrc.h"
#endif

#include <SDL.h>
#include <SDL_thread.h>
#include "cmdutils.h"
#include <assert.h>

#include "demo.h"
#include <linux/soundcard.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>

#define DECODED_AUDIO_BUFFER_SIZE 192000
struct options
{
//    int streamId;
    int frames;
    int nodec;
    int bplay;
    int thread_count;
    int64_t lstart;
    char finput[256];
    //char foutput1[256];
    //char foutput2[256];
};

static int Myparse_option(struct options *opts, int argc, char** argv)
{
    int optidx;
    char *optstr;
    if (argc < 2) return -1;
    //opts->streamId = -1;
    opts->lstart = -1;
    opts->frames = -1;
   // opts->foutput1[0] = 0;
    //opts->foutput2[0] = 0;
    opts->nodec = 0;
    opts->bplay = 0;
    opts->thread_count = 0;
    strcpy(opts->finput, argv[1]);
    optidx = 2;
#if 0	
    while (optidx < argc)
    {
        optstr = argv[optidx++];
        if (*optstr++ != '-') return -1;
        switch (*optstr++)
        {
        case 's': //< stream id
            opts->streamId = atoi(optstr);
            break;
        case 'f': //< frames
            opts->frames = atoi(optstr);
            break;
        case 'k': //< skipped
            opts->lstart = atoll(optstr);
            break;
        case 'o': //< output
            strcpy(opts->foutput1, optstr);
            strcat(opts->foutput1, ".mpg");
            strcpy(opts->foutput2, optstr);
            strcat(opts->foutput2, ".raw");
            break;
        case 'n': //decoding and output options
            if (strcmp("dec", optstr) == 0)
                opts->nodec = 1;
            break;
        case 'p':
            opts->bplay = 1;
            break;
        case 't':
            opts->thread_count = atoi(optstr);
            break;
        default:
            return -1;
        }
    }
 #endif
    return 0;
}
static void Myshow_help(char* program)
{
    printf("简单的FFMPEG测试方案\n");
    printf("Usage: %s inputfile [-sstreamid [-fframes] [-kskipped] [-ooutput_filename(without extension)] [-p] [-tthread_count］\n",
           program);
    return;
}
static void log_callback(void* ptr, int level, const char* fmt, va_list vl)
{
    vfprintf(stdout, fmt, vl);
}
#define OSS_DEVICE "/dev/dsp0"
struct audio_dsp
{
    int audio_fd;
    int channels;
    int format;
    int speed;
};
int map_formats(enum AVSampleFormat format)
{
    switch(format)
    {
        case AV_SAMPLE_FMT_U8:
            return AFMT_U8;
        case AV_SAMPLE_FMT_S16:
            return AFMT_S16_LE;
        default:
            return AFMT_U8;
    }
}
int set_audio(struct audio_dsp* dsp)
{
    if (dsp->audio_fd == -1)
    {
        printf("无效的音频DSP ID!\n");
        return -1;
    }
    if (-1 == ioctl(dsp->audio_fd, SNDCTL_DSP_SETFMT, &dsp->format))
    {
        printf("无法设置DSP格式!\n");
        return -1;
    }
    if (-1 == ioctl(dsp->audio_fd, SNDCTL_DSP_CHANNELS, &dsp->channels))
    {
        printf("无法设置DSP格式!\n");
        return -1;
    }
    if (-1 == ioctl(dsp->audio_fd, SNDCTL_DSP_SPEED, &dsp->speed))
    {
        printf("无法设置DSP格式!\n");
        return -1;
    }
    return 0;
}
int play_pcm(struct audio_dsp* dsp, unsigned char *buf, int size)
{
    if (dsp->audio_fd == -1)
    {
        printf("无效的音频DSP ID！\n");
        return -1;
    }
    if (-1 == write(dsp->audio_fd, buf, size))
    {
        printf("音频DSP无法写入！\n");
        return -1;
    }
    return 0;
}


#define FB_DEVICE "/dev/fb0"
 
enum pic_format
{
    eYUV_420_Planer,
};
struct video_fb
{
    int video_fd;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    unsigned char *fbp;
    AVFrame *frameRGB;
    struct
    {
        int x;
        int y;
    } video_pos;
};
int open_video(struct video_fb *fb, int x, int y)
{
    int screensize;
    fb->video_fd = open(FB_DEVICE, O_WRONLY);
    if (fb->video_fd == -1) return -1;
    if (ioctl(fb->video_fd, FBIOGET_FSCREENINFO, &fb->finfo)) return -2;
    if (ioctl(fb->video_fd, FBIOGET_VSCREENINFO, &fb->vinfo)) return -2;
    printf("视频设备：分解 %dx%d, �pp\n", fb->vinfo.xres, fb->vinfo.yres, fb->vinfo.bits_per_pixel);
    screensize = fb->vinfo.xres * fb->vinfo.yres * fb->vinfo.bits_per_pixel / 8;
    fb->fbp = (unsigned char *) mmap(0, screensize, PROT_READ|PROT_WRITE, MAP_SHARED, fb->video_fd, 0);
    if (fb->fbp == -1) return -3;
    if (x >= fb->vinfo.xres || y >= fb->vinfo.yres)
    {
        return -4;
    }
    else
    {
        fb->video_pos.x = x;
        fb->video_pos.y = y;
    }
 
    fb->frameRGB = avcodec_alloc_frame();
    if (!fb->frameRGB) return -5;
    return 0;
}
#if 1

int show_picture(struct video_fb *fb, AVFrame *frame, int width, int height, enum pic_format format)
{
    struct SwsContext *sws;
    int i;
    unsigned char *dest;
    unsigned char *src;
    if (fb->video_fd == -1) return -1;
    if ((fb->video_pos.x >= fb->vinfo.xres) || (fb->video_pos.y >= fb->vinfo.yres)) return -2;
    if (fb->video_pos.x + width > fb->vinfo.xres)
    {
        width = fb->vinfo.xres - fb->video_pos.x;
    }
    if (fb->video_pos.y + height > fb->vinfo.yres)
    {
        height = fb->vinfo.yres - fb->video_pos.y;
    }
 
    if (format == PIX_FMT_YUV420P)
    {
        sws = sws_getContext(width, height, format, width, height, PIX_FMT_RGB32, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        if (sws == 0)
        {
            return -3;
        }
        if (sws_scale(sws, frame->data, frame->linesize, 0, height, fb->frameRGB->data, fb->frameRGB->linesize))
        {
            return -3;
        }
        dest = fb->fbp + (fb->video_pos.x+fb->vinfo.xoffset) * (fb->vinfo.bits_per_pixel/8) +(fb->video_pos.y+fb->vinfo.yoffset) * fb->finfo.line_length;
        for (i = 0; i < height; i++)
        {
            memcpy(dest, src, width*4);
            src += fb->frameRGB->linesize[0];
            dest += fb->finfo.line_length;
        }
    }
    return 0;
}
#endif
void close_video(struct video_fb *fb)
{
    if (fb->video_fd != -1)
    {
        munmap(fb->fbp, fb->vinfo.xres * fb->vinfo.yres * fb->vinfo.bits_per_pixel / 8);
        close(fb->video_fd);
        fb->video_fd = -1;
    }
}



// check the audio stream
static int find_audio_stream(AVFormatContext* pCtx)
{
	int audioStream = -1;
	int i;
	// check the audio stream
 	for (i = 0; i < pCtx->nb_streams; i++)
	{
        if (pCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audioStream = i;
			break;
        }
    }

	return audioStream;
}

// check the video stream
static int find_video_stream(AVFormatContext* pCtx)
{
	int videoStream = -1;
	int i;
	// check the video stream
 	for (i = 0; i < pCtx->nb_streams; i++)
	{
        if (pCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = i;
			break;
        }
    }

	return videoStream;
}


#define SDL_AUDIO_BUFFER_SIZE 1024  
static int sws_flags = SWS_BICUBIC;  
  
typedef struct PacketQueue  
{  
    AVPacketList *first_pkt, *last_pkt;  
    int nb_packets;  
    int size;  
    SDL_mutex *mutex;  
    SDL_cond *cond;  
} PacketQueue;  
PacketQueue audioq;  
int quit = 0;  
void packet_queue_init(PacketQueue *q)  
{  
    memset(q, 0, sizeof(PacketQueue));  
    q->mutex = SDL_CreateMutex();  
    q->cond = SDL_CreateCond();  
}  
int packet_queue_put(PacketQueue *q, AVPacket *pkt)  
{  
    AVPacketList *pkt1;  
    if(av_dup_packet(pkt) < 0)  
    {  
        return -1;  
    }  
    pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));  
    if (!pkt1)  
        return -1;  
    pkt1->pkt = *pkt;  
    pkt1->next = NULL;  
    SDL_LockMutex(q->mutex);  
    if (!q->last_pkt)  
        q->first_pkt = pkt1;  
    else  
        q->last_pkt->next = pkt1;  
    q->last_pkt = pkt1;  
    q->nb_packets++;  
    q->size += pkt1->pkt.size;  
    SDL_CondSignal(q->cond);  
    SDL_UnlockMutex(q->mutex);  
    return 0;  
}  
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)  
{  
    AVPacketList *pkt1;  
    int ret;  
    SDL_LockMutex(q->mutex);  
    for(;;)  
    {  
        if(quit)  
        {  
            ret = -1;  
            break;  
        }  
        pkt1 = q->first_pkt;  
        if (pkt1)  
        {  
            q->first_pkt = pkt1->next;  
            if (!q->first_pkt)  
                q->last_pkt = NULL;  
            q->nb_packets--;  
            q->size -= pkt1->pkt.size;  
            *pkt = pkt1->pkt;  
            av_free(pkt1);  
            ret = 1;  
            break;  
        }  
        else if (!block)  
        {  
            ret = 0;  
            break;  
        }  
        else  
        {  
            SDL_CondWait(q->cond, q->mutex);  
        }  
    }  
    SDL_UnlockMutex(q->mutex);  
    return ret;  
}  
#if 0  
int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size)  
{  
    static AVPacket pkt;  
    static uint8_t *audio_pkt_data = NULL;  
    static int audio_pkt_size = 0;  
    int len1, data_size;  
	AVFrame *pFrame = 0;
	pFrame = avcodec_alloc_frame();

	int got;
    for(;;)  
    {  
        while(audio_pkt_size > 0)  
        {  
            data_size = buf_size;  
			avcodec_decode_audio4(aCodecCtx, pFrame, &got, &packet);
            len1 = avcodec_decode_audio2(aCodecCtx, (int16_t *)audio_buf, &data_size, audio_pkt_data, audio_pkt_size);  
            if(len1 < 0)  
            { /* if error, skip frame */  
                audio_pkt_size = 0;  
                break;  
            }  
            audio_pkt_data += len1;  
            audio_pkt_size -= len1;  
            if(data_size <= 0)  
            { /* No data yet, get more frames */  
                continue;  
            } /* We have data, return it and come back for more later */  
            return data_size;  
        }  
        if(pkt.data)  
            av_free_packet(&pkt);  
        if(quit)  
        {  
            return -1;  
        }  
        if(packet_queue_get(&audioq, &pkt, 1) < 0)  
        {  
            return -1;  
        }  
        audio_pkt_data = pkt.data;  
        audio_pkt_size = pkt.size;  
    }  
}  
#endif
static void audio_callback(void *userdata, Uint8 *stream, int len)  
{  
#if 0
    AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;  
    int len1, audio_size;  
	#define AVCODEC_MAX_AUDIO_FRAME_SIZE  
    static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];  
    static unsigned int audio_buf_size = 0;  
    static unsigned int audio_buf_index = 0;  
    while(len > 0)  
    {  
        if(audio_buf_index >= audio_buf_size)  
        { /* We have already sent all our data; get more */  
            audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));  
            if(audio_size < 0)  
            { /* If error, output silence */  
                audio_buf_size = 1024; // arbitrary?  
                memset(audio_buf, 0, audio_buf_size);  
            }  
            else  
            {  
                audio_buf_size = audio_size;  
            }  
            audio_buf_index = 0;  
        }  
        len1 = audio_buf_size - audio_buf_index;  
        if(len1 > len)  
            len1 = len;  
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);  
        len -= len1;  
        stream += len1;  
        audio_buf_index += len1;  
    }  
	 #endif
}  
#define CATCH_YUV_FILE "./demo.yuv"

//the linesize is the size of each line, because the size of line not always equal the width of picture  (width != linesize )  !!!!!!!!!
static void saveYUV420P(unsigned char *buf, int wrap, int xsize ,int ysize)
{
    FILE *f = NULL;
    int i;

    if (buf == NULL)
    {
        av_log(NULL, AV_LOG_INFO, "buf == NULL\n");
        return ;
    }

    f=fopen(CATCH_YUV_FILE, "ab+");
    for(i=0;i<ysize;i++)
    {
        fwrite(buf + i * wrap, 1, xsize, f); 
    }
    fflush(f);
    fclose(f);

    f = NULL;
}

int main(int argc, char **argv)
{
    AVFormatContext* pCtx = 0;
    AVCodecContext *pCodecCtx = 0;
    AVCodec *pCodec = 0;
    AVPacket packet;
    AVFrame *pFrame = 0;
    FILE *fpo1 = NULL;
    FILE *fpo2 = NULL;
    int nframe;
    int err;
    int got_picture = -1;
    int picwidth, picheight, linesize;
    unsigned char *pBuf;
    int i;
    int64_t timestamp;
    struct options opt;
    int usefo = 0;
    struct audio_dsp dsp;
    int dusecs;
    float usecs1 = 0;
    float usecs2 = 0;
    struct timeval elapsed1, elapsed2;
    int decoded = 0;

	//taoanran add +++++++++
	int ret = -1;
	int videoStream = -1; //video streamID
	// ----------------------

	int flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
 #if 0
    if (SDL_Init (flags)) {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        fprintf(stderr, "(Did you set the DISPLAY variable?)\n");
        exit(1);
    }
#endif
    av_register_all();

    av_log_set_callback(log_callback);
    av_log_set_level(50);

    if (Myparse_option(&opt, argc, argv) < 0 || (strlen(opt.finput) == 0))
    {
        Myshow_help(argv[0]);
        return 0;
    }

    err = avformat_open_input(&pCtx, opt.finput, 0, 0);
    if (err < 0)
    {
        printf("\n->(avformat_open_input)\tERROR:\t%d\n", err);
        goto fail;
    }
	printf("=========================\n");
    err = avformat_find_stream_info(pCtx, 0);

    if (err < 0)
    {
        printf("\n->(avformat_find_stream_info)\tERROR:\t%d\n", err);
        goto fail;
    }
	av_dump_format(pCtx, 0, opt.finput, 0);

	// check the video stream
	videoStream = find_video_stream(pCtx);
	if (videoStream < 0)
	{
		printf("there is not audio stream !!!!!!! \n");
		return -1;
	}

	pCodecCtx = pCtx->streams[videoStream]->codec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);//find the video decoder
 	if (!pCodec)
    {
        printf("\ncan't find the audio decoder!\n");
        goto fail;
    }

	pFrame = avcodec_alloc_frame();

	//open videDecoder
	ret = avcodec_open2(pCodecCtx, pCodec, 0);

	if (ret < 0)
	{
		printf("avcodec_open2 error \n");
		return -1;
	}

#if 0
	//only for audio
	pFrame->nb_samples = pCodecCtx->frame_size;
	pFrame->format = pCodecCtx->sample_fmt;
	pFrame->channel_layout = pCodecCtx->channel_layout;
#endif
#if 0
	//set the param of SDL
	SDL_AudioSpec wanted_spec, spec; 
	wanted_spec.freq = pCodecCtx->sample_rate;  
	wanted_spec.format = AUDIO_S16SYS;  
	wanted_spec.channels = pCodecCtx->channels;  
	wanted_spec.silence = 0;  
	wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;  
	wanted_spec.callback = audio_callback;//audio_callback;  
	wanted_spec.userdata = pCodecCtx;;//pCodecCtx;  
	if(SDL_OpenAudio(&wanted_spec, &spec) < 0)  
    {  
        fprintf(stderr, "SDL_OpenAudio: %s/n", SDL_GetError());  
        return -1;  
    } 
#endif			

	 printf(" bit_rate = %d \r\n", pCodecCtx->bit_rate);
     printf(" sample_rate = %d \r\n", pCodecCtx->sample_rate);
     printf(" channels = %d \r\n", pCodecCtx->channels);
     printf(" code_name = %s \r\n", pCodecCtx->codec->name);

	char *data = NULL;
	while(av_read_frame(pCtx, &packet) >= 0)
	{
		//found the  audio frame !!!
		if (packet.stream_index == videoStream)
		{
			int got;
			int i;
		
			avcodec_decode_video2(pCodecCtx, pFrame,&got_picture,&packet);
			data = (char *)malloc(pFrame->width * pFrame->height);
			memset(data,0,pFrame->width * pFrame->height);
			printf("pFrame->width = %d\n", pFrame->width);
			printf("pFrame->height = %d\n", pFrame->height);
			printf("pFrame->linesize[0] = %d\n", pFrame->linesize[0]);
			printf("pFrame->linesize[1] = %d\n", pFrame->linesize[1]);
			printf("pFrame->linesize[2] = %d\n", pFrame->linesize[2]);

			//catch the YUV420P data
			saveYUV420P(pFrame->data[0], pFrame->linesize[0], pCodecCtx->width, pCodecCtx->height);      //Y: 4
			saveYUV420P(pFrame->data[1], pFrame->linesize[1], pCodecCtx->width/2, pCodecCtx->height/2);    //U : 1
			saveYUV420P(pFrame->data[2], pFrame->linesize[2], pCodecCtx->width/2, pCodecCtx->height/2);    //V : 1
		}
	}
	
	return 0;
#if 0	
	if (!opt.nodec)
    {
        
        pCodecCtx = pCtx->streams[opt.streamId]->codec;
 
        if (opt.thread_count <= 16 && opt.thread_count > 0 )
        {
            pCodecCtx->thread_count = opt.thread_count;
            pCodecCtx->thread_type = FF_THREAD_FRAME;
        }
        pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
        if (!pCodec)
        {
            printf("\n->不能找到编解码器!\n");
            goto fail;
        }
        err = avcodec_open2(pCodecCtx, pCodec, 0);
        if (err < 0)
        {
            printf("\n->(avcodec_open)\tERROR:\t%d\n", err);
            goto fail;
        }
        pFrame = avcodec_alloc_frame();
 
        if (opt.bplay)
        {
            dsp.audio_fd = open(OSS_DEVICE, O_WRONLY);
            if (dsp.audio_fd == -1)
            {
                printf("\n-> 无法打开音频设备\n");
                goto fail;
            }
            dsp.channels = pCodecCtx->channels;
            dsp.speed = pCodecCtx->sample_rate;
            dsp.format = map_formats(pCodecCtx->sample_fmt);
            if (set_audio(&dsp) < 0)
            {
                printf("\n-> 不能设置音频设备\n");
                goto fail;
            }
        }
    }
    nframe = 0;
	printf("=========================444444\n");
    while(nframe < opt.frames || opt.frames == -1)
    {
        gettimeofday(&elapsed1, NULL);
        err = av_read_frame(pCtx, &packet);
        if (err < 0)
        {
            printf("\n->(av_read_frame)\tERROR:\t%d\n", err);
            break;
        }
        gettimeofday(&elapsed2, NULL);
        dusecs = (elapsed2.tv_sec - elapsed1.tv_sec)*1000000 + (elapsed2.tv_usec - elapsed1.tv_usec);
        usecs2 += dusecs;
        timestamp = av_rescale_q(packet.dts, pCtx->streams[packet.stream_index]->time_base, (AVRational){1, AV_TIME_BASE});
        printf("\nFrame No ] stream#%d\tsize mB, timestamp:%6lld, dts:%6lld, pts:%6lld, ", nframe++, packet.stream_index, packet.size,
               timestamp, packet.dts, packet.pts);
        if (packet.stream_index == opt.streamId)
        {
#if 0
            for (i = 0; i < 16; i++)
            {
                if (i == 0) printf("\n pktdata: ");
                printf("%2x ", packet.data[i]);
            }
            printf("\n");
#endif
            if (usefo)
            {
                fwrite(packet.data, packet.size, 1, fpo1);
                fflush(fpo1);
            }
            if (pCtx->streams[opt.streamId]->codec->codec_type == AVMEDIA_TYPE_VIDEO && !opt.nodec)
            {
                picheight = pCtx->streams[opt.streamId]->codec->height;
                picwidth = pCtx->streams[opt.streamId]->codec->width;
 
                gettimeofday(&elapsed1, NULL);
                avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, &packet);
                decoded++;
                gettimeofday(&elapsed2, NULL);
                dusecs = (elapsed2.tv_sec - elapsed1.tv_sec)*1000000 + (elapsed2.tv_usec - elapsed1.tv_usec);
                usecs1 += dusecs;
                if (got_picture)
                {
                    printf("[Video: type %d, ref %d, pts %lld, pkt_pts %lld, pkt_dts %lld]",
                            pFrame->pict_type, pFrame->reference, pFrame->pts, pFrame->pkt_pts, pFrame->pkt_dts);
 
                    if (pCtx->streams[opt.streamId]->codec->pix_fmt == PIX_FMT_YUV420P)
                    {
                        if (usefo)
                        {
                            linesize = pFrame->linesize[0];
                            pBuf = pFrame->data[0];
                            for (i = 0; i < picheight; i++)
                            {
                                fwrite(pBuf, picwidth, 1, fpo2);
                                pBuf += linesize;
                            }
                            linesize = pFrame->linesize[1];
                            pBuf = pFrame->data[1];
                            for (i = 0; i < picheight/2; i++)
                            {
                                fwrite(pBuf, picwidth/2, 1, fpo2);
                                pBuf += linesize;
                            }
                            linesize = pFrame->linesize[2];
                            pBuf = pFrame->data[2];
                            for (i = 0; i < picheight/2; i++)
                            {
                                fwrite(pBuf, picwidth/2, 1, fpo2);
                                pBuf += linesize;
                            }
                            fflush(fpo2);
                        }
 
                        if (opt.bplay)
                        {
                            
                        }
                    }
                }
                av_free_packet(&packet);
            }
            else if (pCtx->streams[opt.streamId]->codec->codec_type == AVMEDIA_TYPE_AUDIO && !opt.nodec)
            {
                int got;
                gettimeofday(&elapsed1, NULL);
                avcodec_decode_audio4(pCodecCtx, pFrame, &got, &packet);
                decoded++;
                gettimeofday(&elapsed2, NULL);
                dusecs = (elapsed2.tv_sec - elapsed1.tv_sec)*1000000 + (elapsed2.tv_usec - elapsed1.tv_usec);
                usecs1 += dusecs;
                                if (got)
                                {
                    printf("[Audio: ]B raw data, decoding time: %d]", pFrame->linesize[0], dusecs);
                    if (usefo)
                    {
                        fwrite(pFrame->data[0], pFrame->linesize[0], 1, fpo2);
                        fflush(fpo2);
                    }
                    if (opt.bplay)
                    {
                        play_pcm(&dsp, pFrame->data[0], pFrame->linesize[0]);
                    }
                                }
            }
        }
    }
    if (!opt.nodec && pCodecCtx)
    {
        avcodec_close(pCodecCtx);
    }
    printf("\n%d 帧解析, average %.2f us per frame\n", nframe, usecs2/nframe);
    printf("%d 帧解码，平均 %.2f 我们每帧\n", decoded, usecs1/decoded);

#endif

fail:
    if (pCtx)
    {
        avformat_close_input(&pCtx);
    }


    return 0;
}


//
//  ffplay_main.cpp
//  ffplay_test
//
//  Created by devzhaoyou on 2019/10/23.
//  Copyright © 2019 devzhaoyou. All rights reserved.
//

#include "dii_ffplay.h"
#include "dii_common.h"
#include "dii_media_utils.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/timeutils.h"

#include <signal.h>
#include <thread>
#include <mutex>
#include <condition_variable>

extern "C" {
    #include "libavutil/avstring.h"
    #include "libavutil/eval.h"
    #include "libavutil/mathematics.h"
    #include "libavutil/pixdesc.h"
    #include "libavutil/imgutils.h"
    #include "libavutil/dict.h"
    #include "libavutil/parseutils.h"
    #include "libavutil/samplefmt.h"
    #include "libavutil/avassert.h"
    #include "libavutil/time.h"
    #include "libavutil/display.h"
    #include "libavformat/avformat.h"
    #include "libavdevice/avdevice.h"
    #include "libswscale/swscale.h"
    #include "libavutil/opt.h"
    #include "libavcodec/avfft.h"
    #include "libswresample/swresample.h"

    #if CONFIG_AVFILTER
    #include "libavfilter/avfilter.h"
    #include "libavfilter/buffersink.h"
    #include "libavfilter/buffersrc.h"
    #endif
    #include "libavutil/timestamp.h"
}

using namespace dii_media_kit;

#define MAX_QUEUE_SIZE (10 * 1024 * 1024)
#define MIN_FRAMES 50000
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
#define SDL_VOLUME_STEP (0.75)

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define CURSOR_HIDE_DELAY 1000000

#define USE_ONEPASS_SUBTITLE_RENDER 1

#define fftime_to_milliseconds(ts) (av_rescale(ts, 1000, AV_TIME_BASE))
#define milliseconds_to_fftime(ms) (av_rescale(ms, AV_TIME_BASE, 1000))

static unsigned sws_flags = SWS_BICUBIC;

typedef struct MyAVPacketList {
    AVPacket pkt;
    struct MyAVPacketList *next;
    int serial;
} MyAVPacketList;

typedef struct PacketQueue {
    MyAVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int64_t duration;
    int abort_request;
    int serial;

    std::mutex* mutex;
    std::condition_variable *cond;
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

typedef struct AudioParams {
    int freq;
    int channels;
    int64_t channel_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

typedef struct Clock {
    double pts;           /* clock base */
    double pts_drift;     /* clock base minus time at which we updated the clock */
    double last_updated;
    double speed;
    int serial;           /* clock is based on a packet with this serial */
    int paused;
    int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} Clock;

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct Frame {
    AVFrame *frame;
    AVSubtitle sub;
    int serial;
    double pts;           /* presentation timestamp for the frame */
    double duration;      /* estimated duration of the frame */
    int64_t pos;          /* byte position of the frame in the input file */
    int width;
    int height;
    int format;
    AVRational sar;
    int uploaded;
    int flip_v;
} Frame;

typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE];
    int rindex;
    int windex;
    int size;
    int max_size;
    int keep_last;
    int rindex_shown;
    std::mutex* mutex;
    std::condition_variable *cond;
    PacketQueue *pktq;
} FrameQueue;

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

typedef struct Decoder {
    AVPacket pkt;
    PacketQueue *queue;
    AVCodecContext *avctx;
    int pkt_serial;
    int finished;
    int packet_pending;
    std::condition_variable *empty_queue_cond;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
    std::thread *decoder_tid;
} Decoder;

typedef enum {
    SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
} ShowMode;

enum FFPlayEvent {
    EVENT_TYPE_NONE = 0,
    EVENT_TYPE_START,
    EVENT_TYPE_STOP,
    EVENT_TYPE_PAUSE,
    EVENT_TYPE_RESUME,
    EVENT_TYPE_FINISH,
    EVENT_TYPE_SEEK,
    EVENT_TYPE_LOADING,
    EVENT_TYPE_STEP,
    EVENT_TYPE_SEEKFORWARD,
    EVENT_TYPE_SEEKBACK,
    EVENT_TYPE_CYCLEAUDIO,
    EVENT_TYPE_CYCLEVIDEO
};

typedef struct FFTrackCacheStatistic {
    int64_t duration;
    int64_t bytes;
    int64_t packets;
} FFTrackCacheStatistic;

typedef struct FFStatistic {
    int64_t vdec_type;

    float vfps;
    float vdps;
    float avdelay;
    float avdiff;
    int64_t bit_rate;

    FFTrackCacheStatistic video_cache;
    FFTrackCacheStatistic audio_cache;

    int64_t buf_backwards;
    int64_t buf_forwards;
    int64_t buf_capacity;
    int64_t latest_seek_load_duration;
    int64_t byte_count;
    int64_t cache_physical_pos;
    int64_t cache_file_forwards;
    int64_t cache_file_pos;
    int64_t cache_count_bytes;
    int64_t logical_file_size;
    int drop_frame_count;
    int decode_frame_count;
    float drop_frame_rate;
} FFStatistic;

typedef struct _VideoState {
    std::thread *read_tid;
    AVInputFormat *iformat;
    int abort_request;
    int force_refresh;
    int refresh_display;
    int paused;
	int finished;
    int last_paused;
    int queue_attachments_req;
   
    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;
    int accurate_seek;
    int seek_forward;
    double seek_time;
    int seek_flag_audio;
    int seek_flag_video;
    int seek_flag_subtitle;
    
    int read_pause_return;
    AVFormatContext *ic;
    int realtime;

    Clock audclk;
    Clock vidclk;
    Clock extclk;

    FrameQueue pictq;
    FrameQueue subpq;
    FrameQueue sampq;

    Decoder auddec;
    Decoder viddec;
    Decoder subdec;

    int audio_stream;

    int av_sync_type;

    double audio_clock;
    int audio_clock_serial;
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    AVStream *audio_st;
    PacketQueue audioq;
    int audio_hw_buf_size;
    uint8_t *audio_buf;
    uint8_t *audio_buf1;
    unsigned int audio_buf_size; /* in bytes */
    unsigned int audio_buf1_size;
    int audio_buf_index; /* in bytes */
    int audio_write_buf_size;
    int audio_volume;
    int muted;
    struct AudioParams audio_src;
#if CONFIG_AVFILTER
    struct AudioParams audio_filter_src;
#endif
    struct AudioParams audio_tgt;
    struct SwrContext *swr_ctx;
    int frame_drops_early;
    int frame_drops_late;

    ShowMode show_mode;

    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;
    RDFTContext *rdft;
    int rdft_bits;
    FFTSample *rdft_data;
    int xpos;
    double last_vis_time;

    int subtitle_stream;
    AVStream *subtitle_st;
    PacketQueue subtitleq;

    double frame_timer;
    double frame_last_returned_time;
    double frame_last_filter_delay;
    int video_stream;
    AVStream *video_st;
    PacketQueue videoq;
    double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
    struct SwsContext *img_convert_ctx;
    struct SwsContext *sub_convert_ctx;
    int eof;

    char *filename;
    int width, height, xleft, ytop;
    int step;

#if CONFIG_AVFILTER
    int vfilter_idx;
    AVFilterContext *in_video_filter;   // the first filter in the video chain
    AVFilterContext *out_video_filter;  // the last filter in the video chain
    AVFilterContext *in_audio_filter;   // the first filter in the audio chain
    AVFilterContext *out_audio_filter;  // the last filter in the audio chain
    AVFilterGraph *agraph;              // audio filter graph
#endif
    //
    FFStatistic stat;
    int64_t playable_duration_ms;
    int last_video_stream, last_audio_stream, last_subtitle_stream;
    
    std::condition_variable *continue_read_thread;

    int event_type;
    std::thread* event_loop_thread;
	int64_t start_pos;
	WorkStat thr_stat;

    VideoFrameCallback frame_callback = nullptr;
    bool start_complete_ = false;
    StateCallback state_callback = nullptr;
    
    //state
    int is_buffering;
    
    // loop
    int loop = 1;
    int ff_stream_id;
} VideoState;

struct StreamContex {
    
};

/* options specified by the user */
static AVInputFormat *file_iformat;
//static char *input_filename;
static int audio_disable;
static int video_disable;
static int subtitle_disable;
static const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = {0};
static int seek_by_bytes = -1;
//static float seek_interval = 10;
static int display_disable;
//static int borderless;
static int startup_volume = 100;
static int show_status = 1;
static int av_sync_type = AV_SYNC_AUDIO_MASTER;
//static int64_t start_time = AV_NOPTS_VALUE;
static int64_t duration = AV_NOPTS_VALUE;
static int fast = 0;
static int genpts = 0;
static int lowres = 0;
static int decoder_reorder_pts = -1;
static int autoexit;
//static int exit_on_keydown;
//static int exit_on_mousedown;
static int framedrop = -1;
static int infinite_buffer = -1;
static ShowMode show_mode = SHOW_MODE_NONE;
static const char *audio_codec_name;
static const char *subtitle_codec_name;
static const char *video_codec_name;
double rdftspeed = 0.02;
//static int64_t cursor_last_shown;
//static int cursor_hidden = 0;
#if CONFIG_AVFILTER
static const char **vfilters_list = NULL;
static int nb_vfilters = 0;
static char *afilters = NULL;
#endif
static int autorotate = 1;
static int find_stream_info = 1;

/* current context */
//static int is_full_screen;
static int64_t audio_callback_time;

static AVPacket flush_pkt;

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)


#if CONFIG_AVFILTER
static int opt_add_vfilter(void *optctx, const char *opt, const char *arg)
{
    GROW_ARRAY(vfilters_list, nb_vfilters);
    vfilters_list[nb_vfilters - 1] = arg;
    return 0;
}
#endif

static inline
int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
                   enum AVSampleFormat fmt2, int64_t channel_count2)
{
    /* If channel count == 1, planar and non-planar formats are the same */
    if (channel_count1 == 1 && channel_count2 == 1)
        return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
    else
        return channel_count1 != channel_count2 || fmt1 != fmt2;
}

static inline
int64_t get_valid_channel_layout(int64_t channel_layout, int channels)
{
    if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
        return channel_layout;
    else
        return 0;
}

static void toggle_buffering(VideoState* is, int start_buffering) {
    if(start_buffering) {
        DII_LOG(LS_INFO, is->ff_stream_id, 600020) << "ffplay toggle start buffering.";
    } else if(!start_buffering) {
        DII_LOG(LS_INFO, is->ff_stream_id, 600022) << "ffplay toggle buffer ready.";
    }
}

static int64_t dii_ffplay_position(void *is);
static void ffp_check_buffering_l(VideoState* is) {
    int audio_time_base_valid = 0;
    int video_time_base_valid = 0;
    int64_t buf_time_position = -1;

    if(is->audio_st)
        audio_time_base_valid = is->audio_st->time_base.den > 0 && is->audio_st->time_base.num > 0;
    if(is->video_st)
        video_time_base_valid = is->video_st->time_base.den > 0 && is->video_st->time_base.num > 0;


        int64_t cached_duration_in_ms = -1;
        int64_t audio_cached_duration = -1;
        int64_t video_cached_duration = -1;

        if (is->audio_st && audio_time_base_valid) {
            audio_cached_duration = is->stat.audio_cache.duration;
        }

        if (is->video_st && video_time_base_valid) {
            video_cached_duration = is->stat.video_cache.duration;
        }

        if (video_cached_duration > 0 && audio_cached_duration > 0) {
            cached_duration_in_ms = FFMIN(video_cached_duration, audio_cached_duration);
        } else if (video_cached_duration > 0) {
            cached_duration_in_ms = (int)video_cached_duration;
        } else if (audio_cached_duration > 0) {
            cached_duration_in_ms = (int)audio_cached_duration;
        }

        if (cached_duration_in_ms >= 0) {
              buf_time_position = dii_ffplay_position(is) + cached_duration_in_ms;
              is->playable_duration_ms = buf_time_position;
        }
        DII_LOG(LS_VERBOSE, is->ff_stream_id, 0) << "queue playable_duration_ms: " << is->playable_duration_ms;
}

static void ffp_track_statistic_l(VideoState* is, AVStream *st, PacketQueue *q, FFTrackCacheStatistic *cache) {
    assert(cache);

    if (q) {
        cache->bytes   = q->size;
        cache->packets = q->nb_packets;
    }

    if (q && st && st->time_base.den > 0 && st->time_base.num > 0) {
        cache->duration = q->duration * av_q2d(st->time_base) * 1000;
    }
    DII_LOG(LS_VERBOSE, is->ff_stream_id, 0) << "queue cache->duration: " << cache->duration
                                               << "cache->bytes: " << q->size
                                               << "cache->packets: " << q->nb_packets;
}

static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
    MyAVPacketList *pkt1;

    if (q->abort_request)
        return -1;

    pkt1 = (MyAVPacketList *)av_malloc(sizeof(MyAVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;
    if (pkt == &flush_pkt)
        q->serial++;
    pkt1->serial = q->serial;

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    q->duration += pkt1->pkt.duration;
    /* XXX: should duplicate packet data in DV case */
    q->cond->notify_one();
    return 0;
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    int ret;

    std::unique_lock<std::mutex> lck(*q->mutex);
    ret = packet_queue_put_private(q, pkt);


    if (pkt != &flush_pkt && ret < 0)
        av_packet_unref(pkt);

    return ret;
}

static int packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}

/* packet queue handling */
static int packet_queue_init(PacketQueue *q)
{
    memset((void*)q, 0, sizeof(PacketQueue));
	q->mutex = new std::mutex();
    q->cond = new std::condition_variable();
    if (!q->cond) {
        DII_LOG(LS_ERROR, 0, DII_CODE_COMMON_ERROR) << "NSCondition alloc fail.";
        return AVERROR(ENOMEM);
    }
    q->abort_request = 1;
    return 0;
}

static void packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList *pkt, *pkt1;

    std::unique_lock<std::mutex> lck(*q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
}

static void packet_queue_destroy(PacketQueue *q)
{
    packet_queue_flush(q);
    delete q->mutex;
    delete q->cond;
}

static void packet_queue_abort(PacketQueue *q)
{
    std::unique_lock<std::mutex> lck(*q->mutex);
    q->abort_request = 1;
    q->cond->notify_one();
}

static void packet_queue_start(PacketQueue *q)
{
    std::unique_lock<std::mutex> lck(*q->mutex);
    q->abort_request = 0;
    packet_queue_put_private(q, &flush_pkt);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
static int packet_queue_get(VideoState* is, PacketQueue *q, AVPacket *pkt, int block, int *serial, int finished)
{
    MyAVPacketList *pkt1;
    int ret;
    std::unique_lock<std::mutex> lck(*q->mutex);
    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            q->duration -= pkt1->pkt.duration;
            *pkt = pkt1->pkt;
            if (serial)
                *serial = pkt1->serial;
            av_free(pkt1);
            ret = 1;
            if(is->is_buffering && q->nb_packets > 15 && !finished) {
                is->is_buffering = 0;
                toggle_buffering(is, 0);
            }
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            if(!is->is_buffering && !finished) {
                is->is_buffering = 1;
                toggle_buffering(is, 1);
            }
            q->cond->wait(lck);
        }
    }
    return ret;
}

static void decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, std::condition_variable *empty_queue_cond) {
    memset(d, 0, sizeof(Decoder));
    d->avctx = avctx;
    d->queue = queue;
    d->empty_queue_cond = empty_queue_cond;
    d->start_pts = AV_NOPTS_VALUE;
    d->pkt_serial = -1;
}

static int decoder_decode_frame(VideoState* is, Decoder *d, AVFrame *frame, AVSubtitle *sub) {
    int ret = AVERROR(EAGAIN);

    for (;;) {
        AVPacket pkt;

        if (d->queue->serial == d->pkt_serial) {
            do {
                if (d->queue->abort_request)
                    return -1;

                switch (d->avctx->codec_type) {
                    case AVMEDIA_TYPE_VIDEO:
                        // 取出解码后的帧
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0) {
                            if (decoder_reorder_pts == -1) {
                                frame->pts = frame->best_effort_timestamp;
                            } else if (!decoder_reorder_pts) {
                                frame->pts = frame->pkt_dts;
                            }
                        }
                        break;
                    case AVMEDIA_TYPE_AUDIO:
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0) {
                            AVRational tb = {1, frame->sample_rate};
                            if (frame->pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(frame->pts, d->avctx->pkt_timebase, tb);
                            else if (d->next_pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
                            if (frame->pts != AV_NOPTS_VALUE) {
                                d->next_pts = frame->pts + frame->nb_samples;
                                d->next_pts_tb = tb;
                            }
                        }
                        break;
                    default:
                        break;
                }
                if (ret == AVERROR_EOF) {
                    d->finished = d->pkt_serial;
                    avcodec_flush_buffers(d->avctx);
                    return 0;
                }
                if (ret >= 0)
                    return 1;
            } while (ret != AVERROR(EAGAIN));
        }

        do {
            if (d->queue->nb_packets == 0)
                d->empty_queue_cond->notify_one();
            if (d->packet_pending) {
                av_packet_move_ref(&pkt, &d->pkt);
                d->packet_pending = 0;
            } else {
                if (packet_queue_get(is, d->queue, &pkt, 1, &d->pkt_serial, d->finished) < 0) //从队列中获取包
                    return -1;
            }
        } while (d->queue->serial != d->pkt_serial);

        if (pkt.data == flush_pkt.data) {
            avcodec_flush_buffers(d->avctx);
            d->finished = 0;
            d->next_pts = d->start_pts;
            d->next_pts_tb = d->start_pts_tb;
        } else {
            if (d->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) { // decode 字幕
                int got_frame = 0;
                ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, &pkt);
                if (ret < 0) {
                    ret = AVERROR(EAGAIN);
                } else {
                    if (got_frame && !pkt.data) {
                        d->packet_pending = 1;
                        av_packet_move_ref(&d->pkt, &pkt);
                    }
                    ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
                }
            } else {
                // 给解码器发送包用于解码
                if (avcodec_send_packet(d->avctx, &pkt) == AVERROR(EAGAIN)) {
                    DII_LOG(LS_ERROR, 0, 600010) << "Receive_frame and send_packet both returned EAGAIN, which is an API violation.";
                    d->packet_pending = 1;
                    av_packet_move_ref(&d->pkt, &pkt);
                }
            }
            av_packet_unref(&pkt);
        }
    }
}

static void decoder_destroy(Decoder *d) {
    av_packet_unref(&d->pkt);
    avcodec_free_context(&d->avctx);
}

static void frame_queue_unref_item(Frame *vp)
{
    av_frame_unref(vp->frame);
    avsubtitle_free(&vp->sub);
}

static int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last)
{
    int i;
    memset(f, 0, sizeof(FrameQueue));
    if (!(f->mutex = new std::mutex())) {
        DII_LOG(LS_ERROR, 0, DII_CODE_COMMON_ERROR) << "create std mutex error.";
        return AVERROR(ENOMEM);
    }
    if (!(f->cond = new std::condition_variable())) {
        DII_LOG(LS_ERROR, 0, DII_CODE_COMMON_ERROR) << "NSCondition alloc fail";
        return AVERROR(ENOMEM);
    }

    f->pktq = pktq;
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    f->keep_last = !!keep_last;
    for (i = 0; i < f->max_size; i++)
        if (!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

static void frame_queue_destory(FrameQueue *f)
{
    int i;
    for (i = 0; i < f->max_size; i++) {
        Frame *vp = &f->queue[i];
        frame_queue_unref_item(vp);
        av_frame_free(&vp->frame);
    }
    delete f->mutex;
    delete f->cond;
}

static void frame_queue_signal(FrameQueue *f)
{
    std::unique_lock<std::mutex> lck(*f->mutex);
    f->cond->notify_one();
}

static Frame *frame_queue_peek(FrameQueue *f)
{
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

static Frame *frame_queue_peek_next(FrameQueue *f)
{
    return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}

static Frame *frame_queue_peek_last(FrameQueue *f)
{
    return &f->queue[f->rindex];
}

static Frame *frame_queue_peek_writable(FrameQueue *f)
 {
    /* wait until we have space to put a new frame */
    {
        std::unique_lock<std::mutex> lck(*f->mutex);
        while (f->size >= f->max_size &&
               !f->pktq->abort_request) {
            f->cond->wait(lck);
        }
    }

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[f->windex];
}

static Frame *frame_queue_peek_readable(FrameQueue *f)
{
    /* wait until we have a readable a new frame */
    std::unique_lock<std::mutex> lck(*f->mutex);
    while (f->size - f->rindex_shown <= 0 && !f->pktq->abort_request) {
//        f->cond->wait(lck);
        // 防止音频回调阻塞和因此造成的声音滋啦破音, 简单测试此问题修复，不排除引起其他问题
        return NULL;
    }

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

static void frame_queue_push(FrameQueue *f)
{
    if (++f->windex == f->max_size)
        f->windex = 0;
    std::unique_lock<std::mutex> lck(*f->mutex);
    f->size++;
    f->cond->notify_one();
}

static void frame_queue_next(FrameQueue *f)
{
    if (f->keep_last && !f->rindex_shown) {
        f->rindex_shown = 1;
        return;
    }
    frame_queue_unref_item(&f->queue[f->rindex]);
    if (++f->rindex == f->max_size)
        f->rindex = 0;
    std::unique_lock<std::mutex> lck(*f->mutex);
    f->size--;
    f->cond->notify_one();
}

/* return the number of undisplayed frames in the queue */
static int frame_queue_nb_remaining(FrameQueue *f)
{
    return f->size - f->rindex_shown;
}

/* return last shown position */
static int64_t frame_queue_last_pos(FrameQueue *f)
{
    Frame *fp = &f->queue[f->rindex];
    if (f->rindex_shown && fp->serial == f->pktq->serial)
        return fp->pos;
    else
        return -1;
}

static void decoder_abort(Decoder *d, FrameQueue *fq)
{
    packet_queue_abort(d->queue);
    frame_queue_signal(fq);
	if (d->decoder_tid->joinable()) {
		d->decoder_tid->join();
	}   
    delete d->decoder_tid;
    d->decoder_tid = nullptr;

    packet_queue_flush(d->queue);
}

static double get_rotation(AVStream *st)
{
    AVDictionaryEntry *rotate_tag = av_dict_get(st->metadata, "rotate", NULL, 0);
    uint8_t* displaymatrix = av_stream_get_side_data(st,
                                                     AV_PKT_DATA_DISPLAYMATRIX, NULL);
    double theta = 0;

    if (rotate_tag && *rotate_tag->value && strcmp(rotate_tag->value, "0")) {
        char *tail;
        theta = av_strtod(rotate_tag->value, &tail);
        if (*tail)
            theta = 0;
    }
    if (displaymatrix && !theta)
        theta = - av_display_rotation_get((int32_t*) displaymatrix);

    theta -= 360*floor(theta/360 + 0.9/360);

    if (fabs(theta - 90*round(theta/90)) > 2) {
        DII_LOG(LS_WARNING, 0, DII_CODE_COMMON_WARN) << "Odd rotation angle."
        "If you want to help, upload a sample "
        "of this file to ftp://upload.ffmpeg.org/incoming/ "
        "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)";
    }

    return theta;
}

static int upload_texture(VideoState* is, AVFrame *frame, struct SwsContext **img_convert_ctx) {
    if(frame->width <= 0 || frame->height <= 0) {
        return -1;
    }

    // limit to 4k resolution
    if (frame->linesize[0] <= 0 || frame->linesize[0] > 2160||
        frame->linesize[1] <= 0 || frame->linesize[1] > 2160 ||
        frame->linesize[2] <= 0 || frame->linesize[2] > 2160 ||
        frame->data[0] == nullptr || frame->data[1] == nullptr || frame->data[2] == nullptr) {
        DII_LOG(LS_ERROR, is->ff_stream_id, DII_CODE_COMMON_ERROR) << "upoad texture: avframe error.";
        is->state_callback(DII_STATE_ERROR, DII_CODE_COMMON_ERROR, "upoad texture: avframe error.");
        return -1;
    }
    
    dii_media_kit::VideoRotation frame_rotation = kVideoRotation_0;
    if (autorotate) {
        int theta  = (int)get_rotation(is->video_st);
        switch (theta) {
            case 0:
                frame_rotation = kVideoRotation_0;
                break;
            case 90:
                frame_rotation = kVideoRotation_90;
                break;
            case 180:
                frame_rotation = kVideoRotation_180;
                break;
            case 270:
                frame_rotation = kVideoRotation_270;
                break;
            default:
                break;
        }
    }

    if(is->refresh_display > 0) {
        is->refresh_display--;
    }
    
    if (frame->format == AV_PIX_FMT_YUV420P) {
        if (is->frame_callback != nullptr) {
            dii_media_kit::VideoFrame video_frame;
            int ret = -1;
            if(frame->data[0] && frame->data[1] && frame->data[2] && frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0){
              //  if(frame->width == frame->linesize[0] && frame->width/2 == frame->linesize[1] && frame->linesize[1] == frame->linesize[2]){
                    try{
                        ret = video_frame.CreateFrame(frame->data[0], frame->data[1], frame->data[2],
                                            frame->width,
                                            frame->height,
                                            frame->linesize[0], frame->linesize[1], frame->linesize[2],
                                            frame_rotation);
                    }catch(...){
                    }
            //    }
            }
            if(ret == 0) {
                is->frame_callback(video_frame);
            } else {
                DII_LOG(LS_ERROR, is->ff_stream_id, DII_CODE_COMMON_ERROR) << "Create Display Video Frame Faild.";
            }
        }
        return -1;
    } else {
        int ret = 0;
        uint8_t *dst_frame_data[4] = {nullptr};
        int dst_frame_linesize[4]  = {0};
        ret = av_image_alloc(dst_frame_data,
                             dst_frame_linesize,
                             frame->width,
                             frame->height,
                             AV_PIX_FMT_YUV420P,
                             1);
        if (ret < 0) {
            DII_LOG(LS_ERROR, is->ff_stream_id, DII_CODE_COMMON_ERROR) << "Could not allocate raw video buffer.";
            is->state_callback(DII_STATE_ERROR, DII_CODE_COMMON_ERROR, "Could not allocate raw video buffer.");
            return ret;
        }
    
        *img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
                                                frame->width,
                                                frame->height,
                                                (AVPixelFormat)frame->format,
                                                frame->width,
                                                frame->height,
                                                AV_PIX_FMT_YUV420P,
                                                sws_flags,
                                                nullptr,
                                                nullptr,
                                                nullptr);
        if (*img_convert_ctx != nullptr) {
            sws_scale(*img_convert_ctx,
                      (const uint8_t * const *)frame->data,
                      frame->linesize,
                      0,
                      frame->height,
                      dst_frame_data,
                      dst_frame_linesize);

            dii_media_kit::VideoFrame video_frame;
            int ret  = -1;
            if(dst_frame_data[0] && dst_frame_data[1] && dst_frame_data[0] && dst_frame_linesize[0] > 0 && dst_frame_linesize[1] > 0 && dst_frame_linesize[2] > 0){
             //   if(frame->width == dst_frame_linesize[0] && frame->width/2 == dst_frame_linesize[1] && dst_frame_linesize[1] == dst_frame_linesize[2]){
                    try{
                        ret = video_frame.CreateFrame(dst_frame_data[0],
                                           dst_frame_data[1],
                                           dst_frame_data[2],
                                           frame->width,
                                           frame->height,
                                           dst_frame_linesize[0],
                                           dst_frame_linesize[1],
                                           dst_frame_linesize[2],
                                           frame_rotation);
                    }catch(...){
                    }
            //    }
            }
            if(ret == 0) {
                is->frame_callback(video_frame);
            } else {
                DII_LOG(LS_ERROR, is->ff_stream_id, DII_CODE_COMMON_ERROR) << "Create Display Video Frame Faild.";
            }
            av_freep(&dst_frame_data[0]);
            return -1;
        } else {
            ret = -1;
            DII_LOG(LS_ERROR, is->ff_stream_id, DII_CODE_COMMON_ERROR) << "Cannot initialize the conversion context.";
            is->state_callback(DII_STATE_ERROR, DII_CODE_COMMON_ERROR, "Cannot initialize the conversion context.");
            return ret;
        }
    }
}

static void video_image_display(VideoState *is)
{
    Frame *vp;
    vp = frame_queue_peek_last(&is->pictq);

    if (!vp->uploaded) {
        if (upload_texture(is, vp->frame, &is->img_convert_ctx) < 0) {
			return;
        }
        vp->uploaded = 1;
        vp->flip_v = vp->frame->linesize[0] < 0;
    }
}

static void stream_component_close(VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecParameters *codecpar;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;
    codecpar = ic->streams[stream_index]->codecpar;

    switch (codecpar->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            decoder_abort(&is->auddec, &is->sampq);
            decoder_destroy(&is->auddec);
            swr_free(&is->swr_ctx);
            av_freep(&is->audio_buf1);
            is->audio_buf1_size = 0;
            is->audio_buf = NULL;

            if (is->rdft) {
                av_rdft_end(is->rdft);
                av_freep(&is->rdft_data);
                is->rdft = NULL;
                is->rdft_bits = 0;
            }
            break;
        case AVMEDIA_TYPE_VIDEO:
            decoder_abort(&is->viddec, &is->pictq);
            decoder_destroy(&is->viddec);
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            decoder_abort(&is->subdec, &is->subpq);
            decoder_destroy(&is->subdec);
            break;
        default:
            break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            is->audio_st = NULL;
            is->audio_stream = -1;
            break;
        case AVMEDIA_TYPE_VIDEO:
            is->video_st = NULL;
            is->video_stream = -1;
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            is->subtitle_st = NULL;
            is->subtitle_stream = -1;
            break;
        default:
            break;
    }
}

static void stream_close(VideoState *is)
{
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    is->abort_request = 1;
	if (is->read_tid->joinable()) {
		is->read_tid->join();
	}   

    delete is->read_tid;
    is->read_tid = nullptr;

    /* close each stream */
    if (is->audio_stream >= 0)
        stream_component_close(is, is->audio_stream);
    if (is->video_stream >= 0)
        stream_component_close(is, is->video_stream);
    if (is->subtitle_stream >= 0)
        stream_component_close(is, is->subtitle_stream);

    avformat_close_input(&is->ic);

    packet_queue_destroy(&is->videoq);
    packet_queue_destroy(&is->audioq);
    packet_queue_destroy(&is->subtitleq);
    
    /* free all pictures */
    frame_queue_destory(&is->pictq);
    frame_queue_destory(&is->sampq);
    frame_queue_destory(&is->subpq);
    
    // must delete after call 'stream_component_close(is, is->audio_stream)'
    // otherwith audio decode thread still use continue_read_thread, then crash.
    delete is->continue_read_thread;
    is->continue_read_thread = nullptr;
    
    sws_freeContext(is->img_convert_ctx);
    sws_freeContext(is->sub_convert_ctx);
    av_free(is->filename);

    av_free(is);
}

static void do_exit(VideoState *is)
{
    if (is) {
        stream_close(is);
    }
#if CONFIG_AVFILTER
    av_freep(&vfilters_list);
#endif
    avformat_network_deinit();
}

static void sigterm_handler(int sig)
{
    exit(123);
}

/* display the current picture, if any */
static void video_display(VideoState *is)
{
    if (is->video_st)
        video_image_display(is);
}

static double get_clock(Clock *c)
{
    if (*c->queue_serial != c->serial)
        return NAN;
    if (c->paused) {
        return c->pts;
    } else {
        double time = av_gettime_relative() / 1000000.0;
        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
    }
}

static void set_clock_at(Clock *c, double pts, int serial, double time)
{
    c->pts = pts;
    c->last_updated = time;
    c->pts_drift = c->pts - time;
    c->serial = serial;
}

static void set_clock(Clock *c, double pts, int serial)
{
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(c, pts, serial, time);
}

static void set_clock_speed(Clock *c, double speed)
{
    set_clock(c, get_clock(c), c->serial);
    c->speed = speed;
}

static void init_clock(Clock *c, int *queue_serial)
{
    c->speed = 1.0;
    c->paused = 0;
    c->queue_serial = queue_serial;
    set_clock(c, NAN, -1);
}

static void sync_clock_to_slave(Clock *c, Clock *slave)
{
    double clock = get_clock(c);
    double slave_clock = get_clock(slave);
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        set_clock(c, slave_clock, slave->serial);
}

static int get_master_sync_type(VideoState *is) {
    if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (is->video_st)
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
    } else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (is->audio_st)
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
    } else {
        return AV_SYNC_EXTERNAL_CLOCK;
    }
}

/* get the current master clock value */
static double get_master_clock(VideoState *is)
{
    double val;

    switch (get_master_sync_type(is)) {
        case AV_SYNC_VIDEO_MASTER:
            val = get_clock(&is->vidclk);
            break;
        case AV_SYNC_AUDIO_MASTER:
            val = get_clock(&is->audclk);
            break;
        default:
            val = get_clock(&is->extclk);
            break;
    }
    return val;
}

static void check_external_clock_speed(VideoState *is) {
    if ((is->video_stream >= 0 && is->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) ||
        (is->audio_stream >= 0 && is->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES)) {
        set_clock_speed(&is->extclk, FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
    } else if ((is->video_stream < 0 || is->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
               (is->audio_stream < 0 || is->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
        set_clock_speed(&is->extclk, FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
    } else {
        double speed = is->extclk.speed;
        if (speed != 1.0)
            set_clock_speed(&is->extclk, speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
    }
}

static int64_t compute_seek_pos(VideoState *is, int64_t pos) {
    pos = pos >  is->ic->duration ? is->ic->duration : pos;
    pos = pos >  milliseconds_to_fftime(2500) ? pos -  milliseconds_to_fftime(2500) : pos;
  
    return pos;
}

/* seek in the stream */
static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes)
{
    if (!is->seek_req) {
        is->seek_pos = compute_seek_pos(is, pos);
        is->seek_rel = rel;
        is->seek_flags &= ~(AVSEEK_FLAG_BYTE);
        if (seek_by_bytes)
            is->seek_flags |=  AVSEEK_FLAG_BYTE;
        is->seek_req = 1;
        is->continue_read_thread->notify_one();
    }
}

/* pause or resume the video */
static void stream_toggle_pause(VideoState *is)
{
    if (is->paused) {
        is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
        if (is->read_pause_return != AVERROR(ENOSYS)) {
            is->vidclk.paused = 0;
        }
        set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
        set_clock(&is->audclk, get_clock(&is->audclk), is->audclk.serial);
    }
    set_clock(&is->extclk, get_clock(&is->extclk), is->extclk.serial);
    is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !is->paused;
}

static void toggle_pause(VideoState *is)
{
    stream_toggle_pause(is);
    is->step = 0;
}

static void toggle_mute(VideoState *is)
{
    is->muted = !is->muted;
}

static void update_volume(VideoState *is, int sign, double step)
{
    //    double volume_level = is->audio_volume ? (20 * log(is->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    //    int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    //    is->audio_volume = av_clip(is->audio_volume == new_volume ? (is->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}

static void step_to_next_frame(VideoState *is)
{
    /* if the stream is paused unpause it, then step */
    if (is->paused)
        stream_toggle_pause(is);
    is->step = 1;
}

static double compute_target_delay(double delay, VideoState *is)
{
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
         duplicating or deleting a frame */
        diff = get_clock(&is->vidclk) - get_master_clock(is);

        /* skip or repeat frame. We take into account the
         delay to compute the threshold. I still don't know
         if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!isnan(diff) && fabs(diff) < is->max_frame_duration) {
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }
    DII_LOG(LS_VERBOSE, is->ff_stream_id, DII_CODE_COMMON_INFO) << "video: delay=" << delay << " A-V=" << -diff;
    return delay;
}

static double vp_duration(VideoState *is, Frame *vp, Frame *nextvp) {
    if (vp->serial == nextvp->serial) {
        double duration = nextvp->pts - vp->pts;
        if (isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
            return vp->duration;
        else
            return duration;
    } else {
        return 0.0;
    }
}

static void update_video_pts(VideoState *is, double pts, int64_t pos, int serial) {
    /* update current video pts */
    set_clock(&is->vidclk, pts, serial);
    sync_clock_to_slave(&is->extclk, &is->vidclk);
}

/* called to display each frame */
static void video_refresh(void *opaque, double *remaining_time)
{
    VideoState *is = (VideoState *)opaque;
    double time;

    if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
        check_external_clock_speed(is);

    if (!display_disable && is->show_mode != SHOW_MODE_VIDEO && is->audio_st) {
        time = av_gettime_relative() / 1000000.0;
        if (is->force_refresh || is->last_vis_time + rdftspeed < time) {
            video_display(is);
            is->last_vis_time = time;
        }
        *remaining_time = FFMIN(*remaining_time, is->last_vis_time + rdftspeed - time);
    }

    if (is->video_st) {
    retry:
        if (frame_queue_nb_remaining(&is->pictq) == 0) {
            // nothing to do, no picture to display in the queue
        } else {

            double last_duration, duration, delay;
            Frame *vp, *lastvp;

            /* dequeue the picture */
            lastvp = frame_queue_peek_last(&is->pictq);
            vp = frame_queue_peek(&is->pictq);

            if (vp->serial != is->videoq.serial) {
                frame_queue_next(&is->pictq);
                goto retry;
            }

            if (lastvp->serial != vp->serial)
                is->frame_timer = av_gettime_relative() / 1000000.0;

            if (is->paused)
                goto display;

            /* compute nominal last_duration */
            last_duration = vp_duration(is, lastvp, vp);
            delay = compute_target_delay(last_duration, is);

            time= av_gettime_relative()/1000000.0;
            if (time < is->frame_timer + delay) {
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                goto display;
            }

            is->frame_timer += delay;
            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
                is->frame_timer = time;

            {
                std::unique_lock<std::mutex> lck(*is->pictq.mutex);
                if (!isnan(vp->pts))
                    update_video_pts(is, vp->pts, vp->pos, vp->serial);
            }
            // not for audio master
            if (frame_queue_nb_remaining(&is->pictq) > 1) {
                Frame *nextvp = frame_queue_peek_next(&is->pictq);
                duration = vp_duration(is, vp, nextvp);
                if(!is->step && (framedrop>0 || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->frame_timer + duration){
                    is->frame_drops_late++;
                    frame_queue_next(&is->pictq);
                    goto retry;
                }
            }

            frame_queue_next(&is->pictq);
            is->force_refresh = 1;

            if (is->step && !is->paused)
                stream_toggle_pause(is);
        }
    display:
        /* display picture */
        if ((!display_disable && is->force_refresh && is->show_mode == SHOW_MODE_VIDEO && is->pictq.rindex_shown) || is->refresh_display) {
            video_display(is);
        }
    }

    is->force_refresh = 0;
    if (show_status) {
        static int64_t last_time;
        int64_t cur_time;
        int aqsize, vqsize, sqsize;
        double av_diff;

        cur_time = av_gettime_relative();
        if (!last_time || (cur_time - last_time) >= 30000) {
            aqsize = 0;
            vqsize = 0;
            sqsize = 0;
            if (is->audio_st)
                aqsize = is->audioq.size;
            if (is->video_st)
                vqsize = is->videoq.size;
            if (is->subtitle_st)
                sqsize = is->subtitleq.size;
            av_diff = 0;
            if (is->audio_st && is->video_st)
                av_diff = get_clock(&is->audclk) - get_clock(&is->vidclk);
            else if (is->video_st)
                av_diff = get_master_clock(is) - get_clock(&is->vidclk);
            else if (is->audio_st)
                av_diff = get_master_clock(is) - get_clock(&is->audclk);
            //            av_log(NULL, AV_LOG_INFO,
            //                   "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
            //                   get_master_clock(is),
            //                   (is->audio_st && is->video_st) ? "A-V" : (is->video_st ? "M-V" : (is->audio_st ? "M-A" : "   ")),
            //                   av_diff,
            //                   is->frame_drops_early + is->frame_drops_late,
            //                   aqsize / 1024,
            //                   vqsize / 1024,
            //                   sqsize,
            //                   is->video_st ? is->viddec.avctx->pts_correction_num_faulty_dts : 0,
            //                   is->video_st ? is->viddec.avctx->pts_correction_num_faulty_pts : 0);
            fflush(stdout);
            last_time = cur_time;
        }
    }
}

static int queue_picture(VideoState *is, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    Frame *vp;

#if defined(DEBUG_SYNC)
    printf("frame_type=%c pts=%0.3f\n",
           av_get_picture_type_char(src_frame->pict_type), pts);
#endif

    if (!(vp = frame_queue_peek_writable(&is->pictq)))
        return -1;

    vp->sar = src_frame->sample_aspect_ratio;
    vp->uploaded = 0;

    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;
    vp->serial = serial;

    av_frame_move_ref(vp->frame, src_frame);
    frame_queue_push(&is->pictq);
    return 0;
}

static int get_video_frame(VideoState *is, AVFrame *frame)
{
    ffp_track_statistic_l(is, is->video_st, &is->videoq, &is->stat.video_cache);
    
    int got_picture;
    if ((got_picture = decoder_decode_frame(is, &is->viddec, frame, NULL)) < 0)
        return -1;

    if (got_picture) {
        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE)
            dpts = av_q2d(is->video_st->time_base) * frame->pts;

        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

        if (framedrop>0 || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - get_master_clock(is);
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - is->frame_last_filter_delay < 0 &&
                    is->viddec.pkt_serial == is->vidclk.serial &&
                    is->videoq.nb_packets) {
                    is->frame_drops_early++;
                    av_frame_unref(frame);
                    got_picture = 0;
                }
            }
        }
    }

    return got_picture;
}

#if CONFIG_AVFILTER
static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                                 AVFilterContext *source_ctx, AVFilterContext *sink_ctx)
{
    int ret, i;
    int nb_filters = graph->nb_filters;
    AVFilterInOut *outputs = NULL, *inputs = NULL;

    if (filtergraph) {
        outputs = avfilter_inout_alloc();
        inputs  = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        outputs->name       = av_strdup("in");
        outputs->filter_ctx = source_ctx;
        outputs->pad_idx    = 0;
        outputs->next       = NULL;

        inputs->name        = av_strdup("out");
        inputs->filter_ctx  = sink_ctx;
        inputs->pad_idx     = 0;
        inputs->next        = NULL;

        if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
            goto fail;
    } else {
        if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
            goto fail;
    }

    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);

    ret = avfilter_graph_config(graph, NULL);
fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

static int configure_video_filters(AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame)
{
    enum AVPixelFormat pix_fmts[FF_ARRAY_ELEMS(sdl_texture_format_map)];
    char sws_flags_str[512] = "";
    char buffersrc_args[256];
    int ret;
    AVFilterContext *filt_src = NULL, *filt_out = NULL, *last_filter = NULL;
    AVCodecParameters *codecpar = is->video_st->codecpar;
    AVRational fr = av_guess_frame_rate(is->ic, is->video_st, NULL);
    AVDictionaryEntry *e = NULL;
    int nb_pix_fmts = 0;
    int i, j;

    for (i = 0; i < renderer_info.num_texture_formats; i++) {
        for (j = 0; j < FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; j++) {
            if (renderer_info.texture_formats[i] == sdl_texture_format_map[j].texture_fmt) {
                pix_fmts[nb_pix_fmts++] = sdl_texture_format_map[j].format;
                break;
            }
        }
    }
    pix_fmts[nb_pix_fmts] = AV_PIX_FMT_NONE;

    while ((e = av_dict_get(sws_dict, "", e, AV_DICT_IGNORE_SUFFIX))) {
        if (!strcmp(e->key, "sws_flags")) {
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", "flags", e->value);
        } else
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", e->key, e->value);
    }
    if (strlen(sws_flags_str))
        sws_flags_str[strlen(sws_flags_str)-1] = '\0';

    graph->scale_sws_opts = av_strdup(sws_flags_str);

    snprintf(buffersrc_args, sizeof(buffersrc_args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             frame->width, frame->height, frame->format,
             is->video_st->time_base.num, is->video_st->time_base.den,
             codecpar->sample_aspect_ratio.num, FFMAX(codecpar->sample_aspect_ratio.den, 1));
    if (fr.num && fr.den)
        av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", fr.num, fr.den);

    if ((ret = avfilter_graph_create_filter(&filt_src,
                                            avfilter_get_by_name("buffer"),
                                            "ffplay_buffer", buffersrc_args, NULL,
                                            graph)) < 0)
        goto fail;

    ret = avfilter_graph_create_filter(&filt_out,
                                       avfilter_get_by_name("buffersink"),
                                       "ffplay_buffersink", NULL, NULL, graph);
    if (ret < 0)
        goto fail;

    if ((ret = av_opt_set_int_list(filt_out, "pix_fmts", pix_fmts,  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto fail;

    last_filter = filt_out;

    /* Note: this macro adds a filter before the lastly added filter, so the
     * processing order of the filters is in reverse */
#define INSERT_FILT(name, arg) do {                                          \
AVFilterContext *filt_ctx;                                               \
\
ret = avfilter_graph_create_filter(&filt_ctx,                            \
avfilter_get_by_name(name),           \
"ffplay_" name, arg, NULL, graph);    \
if (ret < 0)                                                             \
goto fail;                                                           \
\
ret = avfilter_link(filt_ctx, 0, last_filter, 0);                        \
if (ret < 0)                                                             \
goto fail;                                                           \
\
last_filter = filt_ctx;                                                  \
} while (0)

    if (autorotate) {
        double theta  = get_rotation(is->video_st);

        if (fabs(theta - 90) < 1.0) {
            INSERT_FILT("transpose", "clock");
        } else if (fabs(theta - 180) < 1.0) {
            INSERT_FILT("hflip", NULL);
            INSERT_FILT("vflip", NULL);
        } else if (fabs(theta - 270) < 1.0) {
            INSERT_FILT("transpose", "cclock");
        } else if (fabs(theta) > 1.0) {
            char rotate_buf[64];
            snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
            INSERT_FILT("rotate", rotate_buf);
        }
    }

    if ((ret = configure_filtergraph(graph, vfilters, filt_src, last_filter)) < 0)
        goto fail;

    is->in_video_filter  = filt_src;
    is->out_video_filter = filt_out;

fail:
    return ret;
}

static int configure_audio_filters(VideoState *is, const char *afilters, int force_output_format)
{
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
    int sample_rates[2] = { 0, -1 };
    int64_t channel_layouts[2] = { 0, -1 };
    int channels[2] = { 0, -1 };
    AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;
    char aresample_swr_opts[512] = "";
    AVDictionaryEntry *e = NULL;
    char asrc_args[256];
    int ret;

    avfilter_graph_free(&is->agraph);
    if (!(is->agraph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);

    while ((e = av_dict_get(swr_opts, "", e, AV_DICT_IGNORE_SUFFIX)))
        av_strlcatf(aresample_swr_opts, sizeof(aresample_swr_opts), "%s=%s:", e->key, e->value);
    if (strlen(aresample_swr_opts))
        aresample_swr_opts[strlen(aresample_swr_opts)-1] = '\0';
    av_opt_set(is->agraph, "aresample_swr_opts", aresample_swr_opts, 0);

    ret = snprintf(asrc_args, sizeof(asrc_args),
                   "sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d",
                   is->audio_filter_src.freq, av_get_sample_fmt_name(is->audio_filter_src.fmt),
                   is->audio_filter_src.channels,
                   1, is->audio_filter_src.freq);
    if (is->audio_filter_src.channel_layout)
        snprintf(asrc_args + ret, sizeof(asrc_args) - ret,
                 ":channel_layout=0x%"PRIx64,  is->audio_filter_src.channel_layout);

    ret = avfilter_graph_create_filter(&filt_asrc,
                                       avfilter_get_by_name("abuffer"), "ffplay_abuffer",
                                       asrc_args, NULL, is->agraph);
    if (ret < 0)
        goto end;


    ret = avfilter_graph_create_filter(&filt_asink,
                                       avfilter_get_by_name("abuffersink"), "ffplay_abuffersink",
                                       NULL, NULL, is->agraph);
    if (ret < 0)
        goto end;

    if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts,  AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;
    if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;

    if (force_output_format) {
        channel_layouts[0] = is->audio_tgt.channel_layout;
        channels       [0] = is->audio_tgt.channels;
        sample_rates   [0] = is->audio_tgt.freq;
        if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_layouts", channel_layouts,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "channel_counts" , channels       ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "sample_rates"   , sample_rates   ,  -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
    }


    if ((ret = configure_filtergraph(is->agraph, afilters, filt_asrc, filt_asink)) < 0)
        goto end;

    is->in_audio_filter  = filt_asrc;
    is->out_audio_filter = filt_asink;

end:
    if (ret < 0)
        avfilter_graph_free(&is->agraph);
    return ret;
}
#endif  /* CONFIG_AVFILTER */

static int audio_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    AVFrame *frame = av_frame_alloc();
    Frame *af;
#if CONFIG_AVFILTER
    int last_serial = -1;
    int64_t dec_channel_layout;
    int reconfigure;
#endif
    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    if (!frame)
        return AVERROR(ENOMEM);

    // 循环解码
    do {
        ffp_track_statistic_l(is, is->audio_st, &is->audioq, &is->stat.audio_cache);
        if ((got_frame = decoder_decode_frame(is, &is->auddec, frame, NULL)) < 0)
            goto the_end;
        if (got_frame) {
            tb = /*(AVRational)*/{1, frame->sample_rate};

#if CONFIG_AVFILTER
            dec_channel_layout = get_valid_channel_layout(frame->channel_layout, frame->channels);

            reconfigure =
            cmp_audio_fmts(is->audio_filter_src.fmt, is->audio_filter_src.channels,
                           frame->format, frame->channels)    ||
            is->audio_filter_src.channel_layout != dec_channel_layout ||
            is->audio_filter_src.freq           != frame->sample_rate ||
            is->auddec.pkt_serial               != last_serial;

            if (reconfigure) {
                char buf1[1024], buf2[1024];
                av_get_channel_layout_string(buf1, sizeof(buf1), -1, is->audio_filter_src.channel_layout);
                av_get_channel_layout_string(buf2, sizeof(buf2), -1, dec_channel_layout);
                av_log(NULL, AV_LOG_DEBUG,
                       "Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
                       is->audio_filter_src.freq, is->audio_filter_src.channels, av_get_sample_fmt_name(is->audio_filter_src.fmt), buf1, last_serial,
                       frame->sample_rate, frame->channels, av_get_sample_fmt_name(frame->format), buf2, is->auddec.pkt_serial);

                is->audio_filter_src.fmt            = frame->format;
                is->audio_filter_src.channels       = frame->channels;
                is->audio_filter_src.channel_layout = dec_channel_layout;
                is->audio_filter_src.freq           = frame->sample_rate;
                last_serial                         = is->auddec.pkt_serial;

                if ((ret = configure_audio_filters(is, afilters, 1)) < 0)
                    goto the_end;
            }

            if ((ret = av_buffersrc_add_frame(is->in_audio_filter, frame)) < 0)
                goto the_end;

            while ((ret = av_buffersink_get_frame_flags(is->out_audio_filter, frame, 0)) >= 0) {
                tb = av_buffersink_get_time_base(is->out_audio_filter);
#endif
                if (!(af = frame_queue_peek_writable(&is->sampq)))
                    goto the_end;

                af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
                af->pos = frame->pkt_pos;
                af->serial = is->auddec.pkt_serial;
                af->duration = av_q2d(/*(AVRational)*/{frame->nb_samples, frame->sample_rate});
                
                if (is->accurate_seek && is->seek_flag_audio) {
                    if (is->seek_flag_audio == 2 && (isnan(af->pts) /*|| af->pts > is->seek_time*/)) {
                        continue;
                    }
                    is->seek_flag_audio = 1;

                    if (isnan(af->pts) || af->pts < is->seek_time) {
                        continue;
                    } else {
                        is->seek_flag_audio = 0;
                    }
                }
                
                av_frame_move_ref(af->frame, frame);
                // 解码后的音频帧缓存到队列
                frame_queue_push(&is->sampq);

#if CONFIG_AVFILTER
                if (is->audioq.serial != is->auddec.pkt_serial)
                    break;
            }
            if (ret == AVERROR_EOF)
                is->auddec.finished = is->auddec.pkt_serial;
#endif
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
the_end:
#if CONFIG_AVFILTER
    avfilter_graph_free(&is->agraph);
#endif

    av_frame_free(&frame);
    return ret;
}

static int decoder_start(Decoder *d, int (*fn)(void *), const char *thread_name, void* arg)
{
    packet_queue_start(d->queue);
    d->decoder_tid = new std::thread(fn, arg);
    return 0;
}


static int video_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    AVFrame *frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVRational tb = is->video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);

#if CONFIG_AVFILTER
    AVFilterGraph *graph = avfilter_graph_alloc();
    AVFilterContext *filt_out = NULL, *filt_in = NULL;
    int last_w = 0;
    int last_h = 0;
    enum AVPixelFormat last_format = -2;
    int last_serial = -1;
    int last_vfilter_idx = 0;
    if (!graph) {
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }

#endif

    if (!frame) {
#if CONFIG_AVFILTER
        avfilter_graph_free(&graph);
#endif
        return AVERROR(ENOMEM);
    }

    for (;;) {
        ret = get_video_frame(is, frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;
#if CONFIG_AVFILTER
        if (   last_w != frame->width
            || last_h != frame->height
            || last_format != frame->format
            || last_serial != is->viddec.pkt_serial
            || last_vfilter_idx != is->vfilter_idx) {
            av_log(NULL, AV_LOG_DEBUG,
                   "Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
                   last_w, last_h,
                   (const char *)av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
                   frame->width, frame->height,
                   (const char *)av_x_if_null(av_get_pix_fmt_name(frame->format), "none"), is->viddec.pkt_serial);
            avfilter_graph_free(&graph);
            graph = avfilter_graph_alloc();
            if ((ret = configure_video_filters(graph, is, vfilters_list ? vfilters_list[is->vfilter_idx] : NULL, frame)) < 0) {
                SDL_Event event;
                event.type = FF_QUIT_EVENT;
                event.user.data1 = is;
                SDL_PushEvent(&event);
                goto the_end;
            }
            filt_in  = is->in_video_filter;
            filt_out = is->out_video_filter;
            last_w = frame->width;
            last_h = frame->height;
            last_format = frame->format;
            last_serial = is->viddec.pkt_serial;
            last_vfilter_idx = is->vfilter_idx;
            frame_rate = av_buffersink_get_frame_rate(filt_out);
        }

        ret = av_buffersrc_add_frame(filt_in, frame);
        if (ret < 0)
            goto the_end;

        while (ret >= 0) {
            is->frame_last_returned_time = av_gettime_relative() / 1000000.0;

            ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
            if (ret < 0) {
                if (ret == AVERROR_EOF)
                    is->viddec.finished = is->viddec.pkt_serial;
                ret = 0;
                break;
            }

            is->frame_last_filter_delay = av_gettime_relative() / 1000000.0 - is->frame_last_returned_time;
            if (fabs(is->frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
                is->frame_last_filter_delay = 0;
            tb = av_buffersink_get_time_base(filt_out);
#endif
            duration = (frame_rate.num && frame_rate.den ? av_q2d(/*(AVRational)*/{frame_rate.den, frame_rate.num}) : 0);
            pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            
            if (is->accurate_seek && is->seek_flag_video) {
                if (is->seek_flag_video == 2 && (isnan(pts) /*|| pts > is->seek_time*/)) {
                    continue;
                }
                is->seek_flag_video = 1;
                
                if (isnan(pts) || pts < is->seek_time) {
                    continue;
                } else {
                    is->seek_flag_video = 0;
                }
            }
            
            ret = queue_picture(is, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial);
            av_frame_unref(frame);
#if CONFIG_AVFILTER
            if (is->videoq.serial != is->viddec.pkt_serial)
                break;
        }
#endif

        if (ret < 0)
            goto the_end;
    }
the_end:
#if CONFIG_AVFILTER
    avfilter_graph_free(&graph);
#endif
    av_frame_free(&frame);
    return 0;
}

static int subtitle_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    Frame *sp;
    int got_subtitle;
    double pts;

    for (;;) {
        if (!(sp = frame_queue_peek_writable(&is->subpq)))
            return 0;

        if ((got_subtitle = decoder_decode_frame(is, &is->subdec, NULL, &sp->sub)) < 0)
            break;

        pts = 0;

        if (got_subtitle && sp->sub.format == 0) {
            if (sp->sub.pts != AV_NOPTS_VALUE)
                pts = sp->sub.pts / (double)AV_TIME_BASE;
            sp->pts = pts;
            sp->serial = is->subdec.pkt_serial;
            sp->width = is->subdec.avctx->width;
            sp->height = is->subdec.avctx->height;
            sp->uploaded = 0;
            
            if (is->accurate_seek && is->seek_flag_subtitle) {
                if (is->seek_flag_subtitle == 2 && (isnan(sp->pts) || sp->pts > is->seek_time)) {
                    continue;
                }
                is->seek_flag_subtitle = 1;
   
                if (sp->pts < is->seek_time) {
                    continue;
                } else {
                    is->seek_flag_subtitle = 0;
                }
            }
            
            /* now we can update the picture count */
            frame_queue_push(&is->subpq);
        } else if (got_subtitle) {
            avsubtitle_free(&sp->sub);
        }
    }
    return 0;
}

/* copy samples for viewing in editor window */
static void update_sample_display(VideoState *is, short *samples, int samples_size)
{
    int size, len;

    size = samples_size / sizeof(short);
    while (size > 0) {
        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}

/* return the wanted number of samples to get better sync if sync_type is video
 * or external master clock */
static int synchronize_audio(VideoState *is, int nb_samples)
{
    int wanted_nb_samples = nb_samples;

    /* if not master, then we try to remove or add samples to correct the clock */
    if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
        double diff, avg_diff;
        int min_nb_samples, max_nb_samples;

        diff = get_clock(&is->audclk) - get_master_clock(is);

        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                is->audio_diff_avg_count++;
            } else {
                /* estimate the A-V difference */
                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

                if (fabs(avg_diff) >= is->audio_diff_threshold) {
                    wanted_nb_samples = nb_samples + (int)(diff * is->audio_src.freq);
                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                }
                DII_LOG(LS_INFO, is->ff_stream_id, DII_CODE_COMMON_INFO) << "diff=" << diff
                << "adiff=" << avg_diff
                << "sample_diff=" << wanted_nb_samples - nb_samples
                << "apts= " << is->audio_clock
                << " " << is->audio_diff_threshold;
            }
        } else {
            /* too big difference : may be initial PTS errors, so
             reset A-V filter */
            is->audio_diff_avg_count = 0;
            is->audio_diff_cum       = 0;
        }
    }

    return wanted_nb_samples;
}

/**
 * Decode one audio frame and return its uncompressed size.
 *
 * The processed audio frame is decoded, converted if required, and
 * stored in is->audio_buf, with size in bytes given by the return
 * value.
 */
static int audio_decode_frame(VideoState *is)
{
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    Frame *af;

    if (is->paused)
        return -1;

    do {
#if defined(_WIN32)
        while (frame_queue_nb_remaining(&is->sampq) == 0) {
            if ((av_gettime_relative() - audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2)
                return -1;
            av_usleep (1000);
        }
#endif
        if (!(af = frame_queue_peek_readable(&is->sampq)))
            return -1;
        frame_queue_next(&is->sampq);
    } while (af->serial != is->audioq.serial);

    data_size = av_samples_get_buffer_size(NULL, af->frame->channels,
                                           af->frame->nb_samples,
                                           (AVSampleFormat)(af->frame->format), 1);

    dec_channel_layout =
    (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
    af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
    wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);

    if (af->frame->format        != is->audio_src.fmt            ||
        dec_channel_layout       != is->audio_src.channel_layout ||
        af->frame->sample_rate   != is->audio_src.freq           ||
        (wanted_nb_samples       != af->frame->nb_samples && !is->swr_ctx)) {
        
        swr_free(&is->swr_ctx);
        is->swr_ctx = swr_alloc_set_opts(NULL,
                                         is->audio_tgt.channel_layout,
                                         (AVSampleFormat)is->audio_tgt.fmt,
                                         is->audio_tgt.freq,
                                         dec_channel_layout,
                                         (AVSampleFormat)af->frame->format,
                                         af->frame->sample_rate,
                                         0, NULL);
        if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
            DII_LOG(LS_ERROR, is->ff_stream_id, DII_CODE_COMMON_ERROR) << "Cannot create sample rate converter for conversion of "
            << af->frame->sample_rate << " Hz "
            << av_get_sample_fmt_name(AVSampleFormat(af->frame->format)) << " "
            << af->frame->channels << " channels to "
            << is->audio_tgt.freq << " Hz "
            << av_get_sample_fmt_name(is->audio_tgt.fmt) << " "
            << is->audio_tgt.channels
            << " channels!";
            swr_free(&is->swr_ctx);
            return -1;
        }
        is->audio_src.channel_layout = dec_channel_layout;
        is->audio_src.channels       = af->frame->channels;
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.fmt = (AVSampleFormat)af->frame->format;
    }

    if (is->swr_ctx) {
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
        uint8_t **out = &is->audio_buf1;
        int out_count = (int)((int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256);
        int out_size  = av_samples_get_buffer_size(NULL, is->audio_tgt.channels, out_count, is->audio_tgt.fmt, 0);
        int len2;
        if (out_size < 0) {
            DII_LOG(LS_ERROR, is->ff_stream_id, 600010) << "av_samples_get_buffer_size() failed";
            is->state_callback(DII_STATE_ERROR, 600010, "av_samples_get_buffer_size() failed");
            return -1;
        }
        if (wanted_nb_samples != af->frame->nb_samples) {
            if (swr_set_compensation(is->swr_ctx,
                                     (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq / af->frame->sample_rate,
                                     
                                     wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
                DII_LOG(LS_ERROR, is->ff_stream_id, 600010) << "swr_set_compensation() failed.";
                is->state_callback(DII_STATE_ERROR, 600010, "swr_set_compensation() failed.");
                return -1;
            }
        }
        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if (!is->audio_buf1) {
            DII_LOG(LS_ERROR, is->ff_stream_id, 600010) << "can not alloc memory.";
            is->state_callback(DII_STATE_ERROR, 600010, "can not alloc memory.");
            return AVERROR(ENOMEM);
        }
        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0) {
            DII_LOG(LS_ERROR, is->ff_stream_id, 600010) << "swr_convert() failed.";
            is->state_callback(DII_STATE_ERROR, 600010, "swr_convert() failed.");
            return -1;
        }
        if (len2 == out_count) {
            DII_LOG(LS_WARNING, is->ff_stream_id, 600010) << "audio buffer is probably too small.";
            if (swr_init(is->swr_ctx) < 0)
                swr_free(&is->swr_ctx);
        }
        is->audio_buf = is->audio_buf1;
        resampled_data_size = len2 * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
    } else {
        is->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = is->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts))
        is->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
    else
        is->audio_clock = NAN;
    is->audio_clock_serial = af->serial;
#ifdef DEBUG
    {
        static double last_clock;
        last_clock = is->audio_clock;
    }
#endif
    return resampled_data_size;
}

/* prepare a new audio buffer */
int32_t dii_ffplay_need_10ms_pcm_data(void *opaque, uint8_t *stream, size_t sample_rate, size_t channel)
{
    if(!opaque) return 0;
    VideoState *is = (VideoState*)opaque;
    
    AVCodecContext *avctx = is->auddec.avctx;
    if(avctx == nullptr) {
        return 0;
    }
   
    if(is->audio_tgt.freq != (int)sample_rate ||
       is->audio_tgt.channels != (int)channel ||
       is->audio_tgt.fmt != AV_SAMPLE_FMT_S16) {
        
            is->audio_tgt.freq = (int)sample_rate;
            is->audio_tgt.channels = (int)channel;
            is->audio_tgt.channel_layout = av_get_default_channel_layout((int)channel);
            /* prepare audio output */
            is->audio_tgt.fmt = AV_SAMPLE_FMT_S16;
            is->audio_tgt.frame_size = av_samples_get_buffer_size(NULL,
                                                                  is->audio_tgt.channels,
                                                                  1,
                                                                  is->audio_tgt.fmt,
                                                                  1);
            is->audio_tgt.bytes_per_sec = av_samples_get_buffer_size(NULL,
                                                                       is->audio_tgt.channels,
                                                                       is->audio_tgt.freq,
                                                                       is->audio_tgt.fmt,
                                                                       1);
    }
    
    int bytes_per_sampele = 2;
    int32_t len_10ms = static_cast<int32_t>((float)sample_rate/100*channel*bytes_per_sampele);

    int audio_size, len1;
    audio_callback_time = av_gettime_relative();
    int need_len = len_10ms;
    while (need_len > 0) {
        if (is->audio_buf_index >= is->audio_buf_size) {
           audio_size = audio_decode_frame(is);
           if (audio_size < 0) {
                /* if error, just output silence */
               is->audio_buf = NULL;
               is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
           } else {
               if (is->show_mode != SHOW_MODE_VIDEO)
                   update_sample_display(is, (int16_t *)is->audio_buf, audio_size);
               is->audio_buf_size = audio_size;
           }
           is->audio_buf_index = 0;
        }
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > need_len)
            len1 = need_len;
        if (!is->muted && is->audio_buf && is->audio_volume == 1024)
            memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        else {
            memset(stream, 0, len1);
            if (!is->muted && is->audio_buf) {
                memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
            }
        }
        need_len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
    
    // if no stream only has audio, audio stream coming mean start complete
//    if(!is->start_complete_ && is->video_st == nullptr) {
//		if (is->state_callback) {
//			is->state_callback(DII_STATE_PLAYING, '0', "playing");
//		}
//		is->start_complete_ = true;
//    }
    
    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    if (!isnan(is->audio_clock)) {
        set_clock_at(&is->audclk, is->audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec, is->audio_clock_serial, audio_callback_time / 1000000.0);
        sync_clock_to_slave(&is->extclk, &is->audclk);		
    }
    
    return len_10ms;
}

/* open a given stream. Return 0 if OK */
static int stream_component_open(VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecContext *avctx;
    AVCodec *codec;
    const char *forced_codec_name = NULL;
    AVDictionary *opts = NULL;
    AVDictionaryEntry *t = NULL;
    int sample_rate, nb_channels;
    int64_t channel_layout;
    int ret = 0;
    int stream_lowres = lowres;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;

    avctx = avcodec_alloc_context3(NULL);
    if (!avctx) {
        DII_LOG(LS_ERROR, is->ff_stream_id, 600012) << "stream_component_open error.";
        is->state_callback(DII_STATE_ERROR, 600012, "stream_component_open error.");
        return AVERROR(ENOMEM);
    }

    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0) {
        DII_LOG(LS_ERROR, is->ff_stream_id, 600012) << "can not alloc memory.";
        is->state_callback(DII_STATE_ERROR, 600012, "can not alloc memory.");
        goto fail;
    }
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;

    codec = avcodec_find_decoder(avctx->codec_id); //查找解码器

    switch(avctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO   : is->last_audio_stream    = stream_index; forced_codec_name =    audio_codec_name; break;
        case AVMEDIA_TYPE_SUBTITLE: is->last_subtitle_stream = stream_index; forced_codec_name = subtitle_codec_name; break;
        case AVMEDIA_TYPE_VIDEO   : is->last_video_stream    = stream_index; forced_codec_name =    video_codec_name; break;
        default:
            break;
    }
    
    if (forced_codec_name)
        codec = avcodec_find_decoder_by_name(forced_codec_name);
    if (!codec) {
        if (forced_codec_name) {
            DII_LOG(LS_WARNING, is->ff_stream_id, 600012) << "No codec could be found with name " << forced_codec_name;
        } else {
            DII_LOG(LS_WARNING, is->ff_stream_id, 600012) << "No decoder could be found for codec " << avcodec_get_name(avctx->codec_id);
        }
        ret = AVERROR(EINVAL);
        
        goto fail;
    }

    avctx->codec_id = codec->id;
    if (stream_lowres > codec->max_lowres) {
        DII_LOG(LS_WARNING, is->ff_stream_id, 600012) << "The maximum value for lowres supported by the decoder is " << codec->max_lowres;
        stream_lowres = codec->max_lowres;
    }
    avctx->lowres = stream_lowres;

    if (fast)
        avctx->flags2 |= AV_CODEC_FLAG2_FAST;

    //    opts = filter_codec_opts(codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    if (!av_dict_get(opts, "threads", NULL, 0))
        av_dict_set(&opts, "threads", "auto", 0);
    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
        av_dict_set(&opts, "refcounted_frames", "1", 0);
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        DII_LOG(LS_ERROR, is->ff_stream_id, 600017) << "avcodec_open2 error."<<"with error code: "<<ret;
        is->state_callback(DII_STATE_ERROR, 600017, "avcodec_open2 error.");
        goto fail;
    }
    if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        DII_LOG(LS_ERROR, is->ff_stream_id, 600012) << "Option " << t->key << " not found.";
        is->state_callback(DII_STATE_ERROR, 600012, "Option not found.");
        ret =  AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }

    is->eof = 0;
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    //每个stream解码都有自己独立的线程
    switch (avctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
#if CONFIG_AVFILTER
        {
            AVFilterContext *sink;

            is->audio_filter_src.freq           = avctx->sample_rate;
            is->audio_filter_src.channels       = avctx->channels;
            is->audio_filter_src.channel_layout = get_valid_channel_layout(avctx->channel_layout, avctx->channels);
            is->audio_filter_src.fmt            = avctx->sample_fmt;
            if ((ret = configure_audio_filters(is, afilters, 0)) < 0)
                goto fail;
            sink = is->out_audio_filter;
            sample_rate    = av_buffersink_get_sample_rate(sink);
            nb_channels    = av_buffersink_get_channels(sink);
            channel_layout = av_buffersink_get_channel_layout(sink);
        }
#else
            sample_rate    = avctx->sample_rate;
            nb_channels    = avctx->channels;
            channel_layout = avctx->channel_layout;
#endif

            is->audio_hw_buf_size = 1024;
            is->audio_src = is->audio_tgt;
            is->audio_buf_size  = 0;
            is->audio_buf_index = 0;

            /* init averaging filter */
            is->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
            is->audio_diff_avg_count = 0;
            /* since we do not have a precise anough audio FIFO fullness,
             we correct audio sync only if larger than this threshold */
            is->audio_diff_threshold = (double)(is->audio_hw_buf_size) / is->audio_tgt.bytes_per_sec;

            is->audio_stream = stream_index;
            is->audio_st = ic->streams[stream_index];

            decoder_init(&is->auddec, avctx, &is->audioq, is->continue_read_thread);
            if ((is->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !is->ic->iformat->read_seek) {
                is->auddec.start_pts = is->audio_st->start_time;
                is->auddec.start_pts_tb = is->audio_st->time_base;
            }
            
            // 创建线程，并解码 
            if ((ret = decoder_start(&is->auddec, audio_thread, "audio_decoder", is)) < 0) //音频解码线程audio_thread
                goto out1;
            break;
        case AVMEDIA_TYPE_VIDEO:
            is->video_stream = stream_index;
            is->video_st = ic->streams[stream_index];
            decoder_init(&is->viddec, avctx, &is->videoq, is->continue_read_thread);
            if ((ret = decoder_start(&is->viddec, video_thread, "video_decoder", is)) < 0)
                goto out1;
            is->queue_attachments_req = 1;
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            is->subtitle_stream = stream_index;
            is->subtitle_st = ic->streams[stream_index];

            decoder_init(&is->subdec, avctx, &is->subtitleq, is->continue_read_thread);
            if ((ret = decoder_start(&is->subdec, subtitle_thread, "subtitle_decoder", is)) < 0)
                goto out1;
            break;
        default:
            break;
    }
    goto out1;

fail:
    avcodec_free_context(&avctx);
out1:
    av_dict_free(&opts);

    return ret;
}

static int decode_interrupt_cb(void *ctx)
{
    VideoState *is = (VideoState *)ctx;
    return is->abort_request;
}

static int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue) {
    return stream_id < 0 ||
    queue->abort_request ||
    (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
    ((queue->nb_packets > MIN_FRAMES) && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0));
}

static int is_realtime(AVFormatContext *s)
{
    if(   !strcmp(s->iformat->name, "rtp")
       || !strcmp(s->iformat->name, "rtsp")
       || !strcmp(s->iformat->name, "sdp")
       )
        return 1;

    if(s->pb && (   !strncmp(s->url, "rtp:", 4)
                 || !strncmp(s->url, "udp:", 4)
                 )
       )
        return 1;
    return 0;
}

static void compute_accurate_seek_pos(VideoState* is, int64_t pos) {
    if(is->accurate_seek) {
		int64_t adapt_pos = pos + milliseconds_to_fftime(2500);
		int64_t tpos = pos;
		if (pos > milliseconds_to_fftime(2500)) {
			if (adapt_pos >= 0 ) {
				adapt_pos = pos + milliseconds_to_fftime(2500);
			}
			tpos = adapt_pos;
		}
		//int64_t tpos = pos > milliseconds_to_fftime(2500) ? pos + milliseconds_to_fftime(3000) : pos;
        is->seek_time =  tpos / 1000000.0;
		if(is->seek_forward) {
			is->seek_flag_audio = 1;
			is->seek_flag_video = 1;
			is->seek_flag_subtitle = 1;
		} else {
			is->seek_flag_audio = 2;
			is->seek_flag_video = 2;
			is->seek_flag_subtitle = 2;
		}
	}
}
static int64_t dii_ffplay_duration(void *is);
/* this thread gets the stream from the disk or the network */
static int read_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    AVFormatContext *ic = NULL;
    AVDictionary* opts = NULL;
    int err, i, ret;
    int st_index[AVMEDIA_TYPE_NB];
    AVPacket pkt1, *pkt = &pkt1;
    int64_t stream_start_time;
    int pkt_in_play_range = 0;
    AVDictionaryEntry *t;
    std::mutex wait_mutex;
    int scan_all_pmts_set = 0;
    int64_t pkt_ts;
    
    int64_t buffer_check_timer = 0;

    memset(st_index, -1, sizeof(st_index));
    is->last_video_stream = is->video_stream = -1;
    is->last_audio_stream = is->audio_stream = -1;
    is->last_subtitle_stream = is->subtitle_stream = -1;
    is->eof = 0;

    ic = avformat_alloc_context();
    if (!ic) {
        DII_LOG(LS_ERROR, is->ff_stream_id, 600008) << "Could not allocate context.";
        is->state_callback(DII_STATE_ERROR, 600008, "Could not allocate context.");
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ic->interrupt_callback.callback = decode_interrupt_cb;
    ic->interrupt_callback.opaque = is;

	is->iformat = av_find_input_format(is->filename);

    av_dict_set(&opts, "rw_timeout", "3000*1000", 0);
    av_dict_set(&opts, "buffer_size", "1024*1000*10", 0); //设置缓存大小，1080p可将值调大

    err = avformat_open_input(&ic, is->filename, is->iformat, &opts);
    if (err < 0) {
        char buf[1024] = {0};
        av_strerror(err, buf, 1024);
        is->state_callback(DII_STATE_ERROR, 600013, "open input file error");
        DII_LOG(LS_ERROR, is->ff_stream_id, 600013) << "open input file error: " << buf;
        ret = -1;
        goto fail;
    }

	is->ic = ic;
	is->thr_stat = WORK_OK;

    if (genpts)
        ic->flags |= AVFMT_FLAG_GENPTS;

    av_format_inject_global_side_data(ic);

    if (find_stream_info) {

        err = avformat_find_stream_info(ic, NULL);

        if (err < 0) {
            DII_LOG(LS_WARNING, is->ff_stream_id, 600008) << is->filename << " could not find codec parameters.";
            ret = -1;
            goto fail;
        }
    }

    if (ic->pb)
        ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    if (seek_by_bytes < 0)
        seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);

    is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    /* if seeking requested, we execute it */
    if (is->start_pos > 0) {
        int64_t timestamp;

        timestamp = is->start_pos;
        /* add the stream start time */
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;
        timestamp  = compute_seek_pos(is, timestamp);
        is->accurate_seek = 1;
        is->seek_forward = 1;
        compute_accurate_seek_pos(is, timestamp);
        ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            DII_LOG(LS_WARNING, is->ff_stream_id, 600008) << is->filename << ": could not seek to position " << (double)timestamp / AV_TIME_BASE;
        }
    }
    
    // 判断是否实时网络流rtp,rtsp,sdp,udp
    is->realtime = is_realtime(ic);

    if (show_status)
        av_dump_format(ic, 0, is->filename, 0);

    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        enum AVMediaType type = st->codecpar->codec_type;
        st->discard = AVDISCARD_ALL;
        if (type >= 0 && wanted_stream_spec[type] && st_index[type] == -1)
            if (avformat_match_stream_specifier(ic, st, wanted_stream_spec[type]) > 0)
                st_index[type] = i;
    }
    for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
        if (wanted_stream_spec[i] && st_index[i] == -1) {
            DII_LOG(LS_ERROR, is->ff_stream_id, DII_CODE_COMMON_ERROR) << "Stream specifier " << wanted_stream_spec[i] << " does not match any stream.";
            is->state_callback(DII_STATE_ERROR, DII_CODE_COMMON_ERROR, "Stream specifier does not match any stream.");
            st_index[i] = INT_MAX;
        }
    }

    if (!video_disable)
        st_index[AVMEDIA_TYPE_VIDEO] =
        av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                            st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    if (!audio_disable)
        st_index[AVMEDIA_TYPE_AUDIO] =
        av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                            st_index[AVMEDIA_TYPE_AUDIO],
                            st_index[AVMEDIA_TYPE_VIDEO],
                            NULL, 0);
    if (!video_disable && !subtitle_disable)
        st_index[AVMEDIA_TYPE_SUBTITLE] =
        av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
                            st_index[AVMEDIA_TYPE_SUBTITLE],
                            (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                             st_index[AVMEDIA_TYPE_AUDIO] :
                             st_index[AVMEDIA_TYPE_VIDEO]),
                            NULL, 0);

    is->show_mode = show_mode;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
        AVCodecParameters *codecpar = st->codecpar;
        AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
    }

    /* open the streams */
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        stream_component_open(is, st_index[AVMEDIA_TYPE_AUDIO]);
    }

    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        ret = stream_component_open(is, st_index[AVMEDIA_TYPE_VIDEO]);
    }
    if (is->show_mode == SHOW_MODE_NONE)
        is->show_mode = ret >= 0 ? SHOW_MODE_VIDEO : SHOW_MODE_RDFT;

    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        stream_component_open(is, st_index[AVMEDIA_TYPE_SUBTITLE]);
    }

    if (is->video_stream < 0 && is->audio_stream < 0) {
        DII_LOG(LS_ERROR, is->ff_stream_id, 600008) << "Failed to open file:"<< is->filename << " or configure filtergraph.";
        is->state_callback(DII_STATE_ERROR, 600008, "Failed to open file or configure filtergraph.");
        ret = -1;
        goto fail;
    }

    if (infinite_buffer < 0 && is->realtime)
        infinite_buffer = 1;

    for (;;) {
        if (is->abort_request)
            break;
        if (is->paused != is->last_paused) { //暂停与继续播放处理
            is->last_paused = is->paused;
            if (is->paused)
                is->read_pause_return = av_read_pause(ic);
            else
                av_read_play(ic);
        }
#if CONFIG_RTSP_DEMUXER || CONFIG_MMSH_PROTOCOL
        if (is->paused &&
            (!strcmp(ic->iformat->name, "rtsp") ||
             (ic->pb && !strncmp(input_filename, "mmsh:", 5)))) {
                /* wait 10 ms to avoid trying to get another packet */
                /* XXX: horrible */
                SDL_Delay(10);
                continue;
            }
#endif
        // seek处理
        if (is->seek_req) {
            int64_t seek_target = is->seek_pos;
            int64_t seek_min    = is->seek_rel > 0 ? seek_target - is->seek_rel + 2: INT64_MIN;
            int64_t seek_max    = is->seek_rel < 0 ? seek_target - is->seek_rel - 2: INT64_MAX;
            // FIXME the +-2 is due to rounding being not done in the correct direction in generation
            //      of the seek_pos/seek_rel variables

            ret = avformat_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
            if (ret < 0) {
                DII_LOG(LS_ERROR, is->ff_stream_id, 600007) << is->ic->url << ": error while seeking."<<"whith error code: "<<ret;
                is->state_callback(DII_STATE_ERROR, 600007, "error while seeking, url: %s");
            } else {
                if (is->audio_stream >= 0) {
                    packet_queue_flush(&is->audioq);
                    packet_queue_put(&is->audioq, &flush_pkt);
                }
                if (is->subtitle_stream >= 0) {
                    packet_queue_flush(&is->subtitleq);
                    packet_queue_put(&is->subtitleq, &flush_pkt);
                }
                if (is->video_stream >= 0) {
                    packet_queue_flush(&is->videoq);
                    packet_queue_put(&is->videoq, &flush_pkt);
                }
                if (is->seek_flags & AVSEEK_FLAG_BYTE) {
                    set_clock(&is->extclk, NAN, 0);
                } else {
                    set_clock(&is->extclk, seek_target / (double)AV_TIME_BASE, 0);
                }
            }
            
            compute_accurate_seek_pos(is, is->seek_pos);
            
            is->seek_req = 0;
            is->queue_attachments_req = 1;
            is->eof = 0;
            if (is->paused)
                step_to_next_frame(is);
        }

        if (is->queue_attachments_req) {
            if (is->video_st && is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                AVPacket copy = { 0 };
                if ((ret = av_packet_ref(&copy, &is->video_st->attached_pic)) < 0) {
                    is->state_callback(DII_STATE_ERROR, 600014, "av_packet_ref error");
                    DII_LOG(LS_ERROR, is->ff_stream_id, 600014) << "av_packet_ref error."<<"with error code: "<<ret;
                    goto fail;
                }
                packet_queue_put(&is->videoq, &copy);
                packet_queue_put_nullpacket(&is->videoq, is->video_stream);
            }
            is->queue_attachments_req = 0;
        }

        /* if the queue are full, no need to read more */
        if (infinite_buffer<1 &&
            (is->audioq.size + is->videoq.size + is->subtitleq.size > MAX_QUEUE_SIZE ||
            (stream_has_enough_packets(is->audio_st, is->audio_stream, &is->audioq) &&
            stream_has_enough_packets(is->video_st, is->video_stream, &is->videoq) &&
            stream_has_enough_packets(is->subtitle_st, is->subtitle_stream, &is->subtitleq)))) {
            std::unique_lock<std::mutex> lck(wait_mutex);
            is->continue_read_thread->wait_for(lck, std::chrono::milliseconds(10));
            continue;
        }
        // 结束了，从头播放或者退出
        if (!is->paused &&
            !is->finished &&
            (!is->audio_st || (is->auddec.finished == is->audioq.serial && frame_queue_nb_remaining(&is->sampq) == 0)) &&
            (!is->video_st || (is->viddec.finished == is->videoq.serial && frame_queue_nb_remaining(&is->pictq) == 0))) {
            if (is->loop != 0) {
                stream_seek(is, is->start_pos > 0 ? is->start_pos : 0, 0, 0);
            } else if (autoexit) {
                ret = AVERROR_EOF;
                is->state_callback(DII_STATE_ERROR, 600015, "AVERROR_EOF");
                DII_LOG(LS_ERROR, is->ff_stream_id, 600015) << "AVERROR_EOF.";
                goto fail;
            } else {
                is->finished = 1;
                // call this can solve audio callback block when stream finish, not a good solution. You should call dii_ffplay_stop.
                // packet_queue_abort(is->auddec.queue);
                
                is->state_callback(DII_STATE_FINISH, 0, "finish");
                DII_LOG(LS_INFO, is->ff_stream_id, 600003) << "ffplay play finish.";
            }
        }
        //主要流程就是不停的读取帧到队列中，如果队列满了则等待。
        //同时外部播放线程不断从队列中取帧进行播放。
        ret = av_read_frame(ic, pkt); //关键代码：读取帧

        if (ret < 0) { //错误或者结束，队列放入一个空包
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->eof) {
                if (is->video_stream >= 0)
                    packet_queue_put_nullpacket(&is->videoq, is->video_stream);
                if (is->audio_stream >= 0)
                    packet_queue_put_nullpacket(&is->audioq, is->audio_stream);
                if (is->subtitle_stream >= 0)
                    packet_queue_put_nullpacket(&is->subtitleq, is->subtitle_stream);
                is->eof = 1;
            }
            
            if(dii_rtc::TimeMillis() - buffer_check_timer > 500) {
               buffer_check_timer = dii_rtc::TimeMillis();
               ffp_check_buffering_l(is);
            }
            
            if (ic->pb && ic->pb->error)
                break;

            std::unique_lock<std::mutex> lck(wait_mutex);
            is->continue_read_thread->wait_for(lck, std::chrono::milliseconds(10));
            continue;
        } else {
            is->eof = 0;
        }
        /* check if packet is in play range specified by user, then queue, otherwise discard */
        stream_start_time = ic->streams[pkt->stream_index]->start_time;
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;

        //包的时间戳，先取pts播放时间戳，取不到则取dts解码时间戳
        //duration == AV_NOPTS_VALUE表示命令行未设置此参数，如果设置了则duration表示播放总时长。
        //后面一段是 包的时间戳与开始播放的时间戳之差即当前包的时间位置，也就是这个包应该在第几秒播放。
        //跟start_time真正播放开始的时间之差如果小于等于duration总时长则表示包有效然后加入队列，无效则丢弃。
        pkt_in_play_range = duration == AV_NOPTS_VALUE ||
        (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
        av_q2d(ic->streams[pkt->stream_index]->time_base) -
        (double)(is->start_pos > 0 ? is->start_pos : 0) / 1000000
        <= ((double)duration / 1000000);
        if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
            packet_queue_put(&is->audioq, pkt);
        } else if (pkt->stream_index == is->video_stream && pkt_in_play_range
                   && !(is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            packet_queue_put(&is->videoq, pkt);
        } else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range) {
            packet_queue_put(&is->subtitleq, pkt);
        } else {
            av_packet_unref(pkt);
        }
        
        if(dii_rtc::TimeMillis() - buffer_check_timer > 500) {
            buffer_check_timer = dii_rtc::TimeMillis();
            ffp_check_buffering_l(is);
        }
    }

    ret = 0;
	is->thr_stat = WORK_FINISH;
fail:
    if (ic && !is->ic)
        avformat_close_input(&ic);

    if (ret != 0) {
        is->event_type = EVENT_TYPE_STOP;
    }
    return 0;
}

static void stream_cycle_channel(VideoState *is, int codec_type)
{
    AVFormatContext *ic = is->ic;
    int start_index, stream_index;
    int old_index;
    AVStream *st;
    AVProgram *p = NULL;
    int nb_streams = is->ic->nb_streams;

    if (codec_type == AVMEDIA_TYPE_VIDEO) {
        start_index = is->last_video_stream;
        old_index = is->video_stream;
    } else if (codec_type == AVMEDIA_TYPE_AUDIO) {
        start_index = is->last_audio_stream;
        old_index = is->audio_stream;
    } else {
        start_index = is->last_subtitle_stream;
        old_index = is->subtitle_stream;
    }
    stream_index = start_index;

    if (codec_type != AVMEDIA_TYPE_VIDEO && is->video_stream != -1) {
        p = av_find_program_from_stream(ic, NULL, is->video_stream);
        if (p) {
            nb_streams = p->nb_stream_indexes;
            for (start_index = 0; start_index < nb_streams; start_index++)
                if (p->stream_index[start_index] == stream_index)
                    break;
            if (start_index == nb_streams)
                start_index = -1;
            stream_index = start_index;
        }
    }

    for (;;) {
        if (++stream_index >= nb_streams)
        {
            if (codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                stream_index = -1;
                is->last_subtitle_stream = -1;
                goto the_end;
            }
            if (start_index == -1)
                return;
            stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = is->ic->streams[p ? p->stream_index[stream_index] : stream_index];
        if (st->codecpar->codec_type == codec_type) {
            /* check that parameters are OK */
            switch (codec_type) {
                case AVMEDIA_TYPE_AUDIO:
                    if (st->codecpar->sample_rate != 0 &&
                        st->codecpar->channels != 0)
                        goto the_end;
                    break;
                case AVMEDIA_TYPE_VIDEO:
                case AVMEDIA_TYPE_SUBTITLE:
                    goto the_end;
                default:
                    break;
            }
        }
    }
the_end:
    if (p && stream_index != -1) {
        stream_index = p->stream_index[stream_index];
        DII_LOG(LS_INFO, is->ff_stream_id, 600006) << "Switch " <<  av_get_media_type_string((AVMediaType)codec_type)
        << " stream from #" << old_index
        << " to #" << stream_index;
    }

    stream_component_close(is, old_index);
    stream_component_open(is, stream_index);
}

static void refresh_loop_wait_event(VideoState *is) {
    double remaining_time = 0.0;
    while (is->event_type == EVENT_TYPE_NONE) {
        if (remaining_time > 0.0) {
            std::this_thread::sleep_for(std::chrono::microseconds((int64_t)(remaining_time*1000000.0)));
        }

        if(is->paused || is->finished) {
            remaining_time = 5*REFRESH_RATE;
        } else {
            remaining_time = REFRESH_RATE;
        }
        
        if (is->show_mode != SHOW_MODE_NONE && (!is->paused || is->force_refresh || is->refresh_display))
            video_refresh(is, &remaining_time);
    }
}

static void seek_chapter(VideoState *is, int incr)
{
    int64_t pos = get_master_clock(is) * AV_TIME_BASE;
    int i;

    if (!is->ic->nb_chapters)
        return;

    /* find the current chapter */
    for (i = 0; i < is->ic->nb_chapters; i++) {
        AVChapter *ch = is->ic->chapters[i];
		AVRational time_base = {1, AV_TIME_BASE};
        if (av_compare_ts(pos, time_base/*AV_TIME_BASE_Q*/, ch->start, ch->time_base) < 0) {
            i--;
            break;
        }
    }

    i += incr;
    i = FFMAX(i, 0);
    if (i >= is->ic->nb_chapters)
        return;
    
    DII_LOG(LS_VERBOSE, is->ff_stream_id, DII_CODE_COMMON_INFO) << "Seeking to chapter " << i;
	AVRational time_base = { 1, AV_TIME_BASE };
    stream_seek(is, av_rescale_q(is->ic->chapters[i]->start, is->ic->chapters[i]->time_base,
								time_base/*AV_TIME_BASE_Q*/), 0, 0);
}

/* handle an event sent by the GUI */
static void event_loop(VideoState *cur_stream)
{
    double incr = 0, pos;
    for (;;) {
        refresh_loop_wait_event(cur_stream);
        if (cur_stream->event_type == EVENT_TYPE_STOP) {
            break;
        }else if (cur_stream->event_type == EVENT_TYPE_PAUSE) {
            cur_stream->event_type = EVENT_TYPE_NONE;
            toggle_pause(cur_stream);
		}else if (cur_stream->event_type == EVENT_TYPE_RESUME) {
			cur_stream->event_type = EVENT_TYPE_NONE;
			toggle_pause(cur_stream);
        }else if (cur_stream->event_type == EVENT_TYPE_STEP) {
            step_to_next_frame(cur_stream);
        }else if (cur_stream->event_type == EVENT_TYPE_SEEKFORWARD || cur_stream->event_type == EVENT_TYPE_SEEKBACK) {
            if (cur_stream->event_type == EVENT_TYPE_SEEKFORWARD) {
                incr = 10;
            }else if (cur_stream->event_type == EVENT_TYPE_SEEKBACK) {
                incr = -10;
            }
            if (seek_by_bytes) {
                pos = -1;
                if (pos < 0 && cur_stream->video_stream >= 0)
                    pos = frame_queue_last_pos(&cur_stream->pictq);
                if (pos < 0 && cur_stream->audio_stream >= 0)
                    pos = frame_queue_last_pos(&cur_stream->sampq);
                if (pos < 0)
                    pos = avio_tell(cur_stream->ic->pb);
                if (cur_stream->ic->bit_rate)
                    incr *= cur_stream->ic->bit_rate / 8.0;
                else
                    incr *= 180000.0;
                pos += incr;
                stream_seek(cur_stream, pos, incr, 1);
            } else {
                pos = get_master_clock(cur_stream);
                if (isnan(pos))
                    pos = (double)cur_stream->seek_pos / AV_TIME_BASE;
                pos += incr;
                if (cur_stream->ic->start_time != AV_NOPTS_VALUE && pos < cur_stream->ic->start_time / (double)AV_TIME_BASE)
                    pos = cur_stream->ic->start_time / (double)AV_TIME_BASE;
                stream_seek(cur_stream, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
            }
        }else if (cur_stream->event_type == EVENT_TYPE_CYCLEAUDIO) {
            stream_cycle_channel(cur_stream, AVMEDIA_TYPE_AUDIO);
        }else if (cur_stream->event_type == EVENT_TYPE_CYCLEVIDEO) {
            stream_cycle_channel(cur_stream, AVMEDIA_TYPE_VIDEO);
        }

        cur_stream->event_type = EVENT_TYPE_NONE;
    }
}

#pragma open video
static VideoState *stream_open(const char *filename,
                               AVInputFormat *iformat,
                               int64_t pos,
                               int stream_id,
                               VideoFrameCallback frame_callback,
                               StateCallback state_callback)
{
    VideoState *is;
    is = (VideoState *)av_mallocz(sizeof(VideoState));
    if (!is) {
        return nullptr;
    }
    
    ///
    is->ff_stream_id = stream_id;
    is->finished = 0;
    is->frame_callback = frame_callback;
    is->state_callback = state_callback;
    is->event_loop_thread = new std::thread(event_loop, is);
    
    ///
	is->start_pos = milliseconds_to_fftime(pos);
    // bugfix: start + pos + pause, get position api return 0;
    is->seek_pos = is->start_pos;
    is->filename = av_strdup(filename);
    if (!is->filename) {
        DII_LOG(LS_ERROR, is->ff_stream_id, 600018) << "error: file name is null.";
        is->state_callback(DII_STATE_ERROR, 600018, "error: file name is null.");
        goto fail;
    }
    
    is->iformat = iformat;
    //is->ytop    = 0;
    //is->xleft   = 0;

    // 创建音频，视频，字幕帧队列
    /* start video display */
    if (frame_queue_init(&is->pictq, &is->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        goto fail;
    if (frame_queue_init(&is->subpq, &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        goto fail;
    if (frame_queue_init(&is->sampq, &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        goto fail;

    // 创建包队列?
    if (packet_queue_init(&is->videoq) < 0 ||
        packet_queue_init(&is->audioq) < 0 ||
        packet_queue_init(&is->subtitleq) < 0) {
        DII_LOG(LS_ERROR, is->ff_stream_id, 600016) << "create packet queue error.";
        is->state_callback(DII_STATE_ERROR, 600016, "create packet queue error.");
        goto fail;
    }

    if (!(is->continue_read_thread = new std::condition_variable())) {
        goto fail;
    }

    // 初始化时钟，serial从0开始
    init_clock(&is->vidclk, &is->videoq.serial);
    init_clock(&is->audclk, &is->audioq.serial);
    init_clock(&is->extclk, &is->extclk.serial);
    is->audio_clock_serial = -1;
    
    if (startup_volume < 0) {
        DII_LOG(LS_WARNING, is->ff_stream_id, DII_CODE_COMMON_WARN) << "-volume=" << startup_volume << " < 0, setting to 0.";
    }
    if (startup_volume > 100) {
        DII_LOG(LS_WARNING, is->ff_stream_id, DII_CODE_COMMON_WARN) << "-volume=" << startup_volume << " > 100, setting to 100.";
    }
    startup_volume = av_clip(startup_volume, 0, 100);
    is->audio_volume = startup_volume;
    is->muted = 0;
    // 音视频同步类型
    is->av_sync_type = av_sync_type;

    is->read_tid = new std::thread(read_thread, is);
    if(is->read_tid == nullptr) {
fail:
		stream_close(is);
		return NULL;
    }

	int wait_ms = 0;
	while (!is->thr_stat && wait_ms++ < 1000) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

    return is;
}

#pragma player start play
static int64_t dii_ffplay_duration(void *is) {
    VideoState *vis = (VideoState*)is;
    if (!vis || !vis->ic)
        return DII_ERROR;

    int64_t duration = fftime_to_milliseconds(vis->ic->duration);
    if (duration < 0) return 0;

    return duration;
}

static int64_t dii_ffplay_position(void *is) {
    VideoState *vis = (VideoState*)is;
    if (!vis || !vis->ic)
        return DII_ERROR;
    if(vis->finished)
        return dii_ffplay_duration(vis);
    int64_t start_time = vis->ic->start_time;
    int64_t start_diff = 0;
    if (start_time > 0 && start_time != AV_NOPTS_VALUE)
        start_diff = fftime_to_milliseconds(start_time);

    int64_t cur_pos = 0;
    double pos_clock = get_master_clock(vis);
    if (isnan(pos_clock)) {
        cur_pos = fftime_to_milliseconds(vis->seek_pos);
    } else {
        cur_pos = pos_clock * 1000;
    }

    if (cur_pos < 0 || cur_pos < start_diff) return 0;
    
    int64_t pos = cur_pos - start_diff;
    return pos;
}

static int32_t dii_ffplay_pause(void *is) {
    VideoState* vis = (VideoState*)is;
    if (!vis)
          return DII_PARAMETER_ERROR;
	if (vis->paused) {
		return DII_ALREADY_DONE;
	}
    vis->refresh_display = 5;
    toggle_pause(vis);
   
	if (vis->state_callback) {
		vis->state_callback(DII_STATE_PAUSED, 0, "paused");
	}
    
    DII_LOG(LS_INFO, vis->ff_stream_id, 600004) << "ffplay pause.";
    return DII_DONE;
}

static int32_t dii_ffplay_resume(void *is) {
	VideoState* vis = (VideoState*)is;
    if (!vis)
          return DII_PARAMETER_ERROR;
      
	if (!vis->paused) {
		return DII_ALREADY_DONE;
	}
 
    toggle_pause(vis);
	if (vis->state_callback) {
		vis->state_callback(DII_STATE_PLAYING, 0, "playing");
	}
    DII_LOG(LS_INFO, vis->ff_stream_id, 600005) << "ffplay resume.";

	return 0;
}

static int32_t dii_ffplay_stop(void *is) {
    VideoState* vis = (VideoState*)is;
    if (!vis)
       return DII_PARAMETER_ERROR;
   
	vis->event_type = EVENT_TYPE_STOP;
	  
	if (vis->event_loop_thread->joinable()) {
		vis->event_loop_thread->join();
        delete vis->event_loop_thread;
        vis->event_loop_thread = nullptr;
	}
    
    if (vis->state_callback) {
        vis->state_callback(DII_STATE_STOPPED, 0, "stop");
    }
    DII_LOG(LS_INFO, vis->ff_stream_id, 600002) << "ffplay stop play.";
	do_exit(vis);
    return 0;
}

static int32_t dii_ffplay_seek(void *is, int64_t pos) {
    VideoState* vis = (VideoState*)is;
       if (!vis || !vis->ic || pos < 0)
           return DII_ERROR;
       
       vis->finished = 0;
    
       vis->accurate_seek = 1;
    
       if(pos > dii_ffplay_position(vis)) {
           vis->seek_forward = 1;
       } else {
           vis->seek_forward = 0;
       }
       
       int64_t start_time = 0;
       int64_t seek_pos = milliseconds_to_fftime(pos);
       int64_t duration = milliseconds_to_fftime(dii_ffplay_duration(vis));

       if (duration > 0 && seek_pos >= duration) {
           return DII_ERROR;
       }

       start_time = vis->ic->start_time;
       if (start_time > 0 && start_time != AV_NOPTS_VALUE)
          seek_pos += start_time;

       stream_seek(vis, seek_pos, 0, 0);
    
       DII_LOG(LS_INFO, vis->ff_stream_id, 600006) << "ffplay stream seek, pos:" << seek_pos;
       if (vis->state_callback) {
           vis->state_callback(DII_STATE_SEEKING, 0, "seeking");
       }
       return 0;
}

static bool dii_ffplay_loop(void *is, bool loop) {
	VideoState *vis = (VideoState*)is;
	if (!vis)
		return true;

	if (vis->paused || vis->finished) {
		return true;
	}
    if(loop) {
        vis->loop = 1;
    } else {
        vis->loop = 0;
    }
	return false;
}

static void* dii_ffplay_start(const char* url,
                                int64_t pos,
                                int stream_id,
                                VideoFrameCallback frame_callback,
                                StateCallback state_callback) {
    
    DII_LOG(LS_INFO, stream_id, 600001) <<  "ffplay start play url:" << url;
    avformat_network_init();
    av_init_packet(&flush_pkt);
    flush_pkt.data = (uint8_t *)&flush_pkt;

    VideoState *vis = stream_open(url, file_iformat, pos, stream_id, frame_callback, state_callback);
    if (!vis) {
        DII_LOG(LS_ERROR, stream_id, 600009) << "Failed to initialize VideoState!";
        state_callback(DII_STATE_ERROR, 600009, "Failed to initialize VideoState!");
        return NULL;
    }
    if (state_callback) {
        state_callback(DII_STATE_PLAYING, 0, "playing");
        DII_LOG(LS_INFO, stream_id, 0) << "callback playing state.";
    }
    return vis;
}

namespace dii_media_kit  {
    DiiFFPlayer::DiiFFPlayer(int32_t stream_id) {
        this->stream_id_ = stream_id;
        
        _role = dii_radar::_Role_Unknown;
        _userid = NULL;
        _report = true;
    }

    DiiFFPlayer::~DiiFFPlayer() {
        this->StopPlay();
        
        if(_userid){
            free(_userid);
            _userid = NULL;
        }
    }

    int32_t DiiFFPlayer::Start(const char* url, int64_t pos, bool pause) {
        std::unique_lock<std::mutex> lck(mtx_);
        dii_ffplayer_ = dii_ffplay_start(url,
                                             pos,
                                             stream_id_,
                                             callback_.video_frame_callback_,
                                             callback_.state_callback_);
        return 0;
    }

    int32_t DiiFFPlayer::Pause() {
        std::unique_lock<std::mutex> lck(mtx_);
		int ret = -1;

		if (dii_ffplayer_) {
			ret = dii_ffplay_pause(dii_ffplayer_);
		}
		return ret;
    }

    int32_t DiiFFPlayer::Resume() {
        std::unique_lock<std::mutex> lck(mtx_);
		int ret = -1;
		if (dii_ffplayer_) {
			ret = dii_ffplay_resume(dii_ffplayer_);
		}
		return ret;
    }

    int32_t DiiFFPlayer::StopPlay() {
        std::unique_lock<std::mutex> lck(mtx_);
		int ret = -1;
		if (dii_ffplayer_) {
			ret = dii_ffplay_stop(dii_ffplayer_);
			dii_ffplayer_ = NULL;
		}
        return ret;
    }

    int32_t DiiFFPlayer::SetLoop(bool loop) {
        std::unique_lock<std::mutex> lck(mtx_);
		int ret = -1;
		if (dii_ffplayer_) {
			ret = dii_ffplay_loop(dii_ffplayer_, loop);
		}
        return ret;
    }

    int32_t DiiFFPlayer::Seek(int64_t pos) {
        std::unique_lock<std::mutex> lck(mtx_);
		int ret = -1;
		if (dii_ffplayer_) {
			ret = dii_ffplay_seek(dii_ffplayer_, pos);
		}
		return ret;
    }

    int64_t DiiFFPlayer::Position() {
        std::unique_lock<std::mutex> lck(mtx_);
		int ret = -1;
		if (dii_ffplayer_) {
			ret = dii_ffplay_position(dii_ffplayer_);
		}
		return ret;
    }

    int64_t DiiFFPlayer::Duration() {
        std::unique_lock<std::mutex> lck(mtx_);
		int ret = -1;
		if (dii_ffplayer_) {
			ret = dii_ffplay_duration(dii_ffplayer_);
		}
		return ret;
    }

    int32_t DiiFFPlayer::GetMoreAudioData(void *stream, size_t sample_rate, size_t channel) {
        std::unique_lock<std::mutex> lck(mtx_);
		int len = 0;
		if (dii_ffplayer_) {
			len = dii_ffplay_need_10ms_pcm_data(dii_ffplayer_, (uint8_t*)stream, sample_rate, channel);
		}
    
        return len;
    }

    int32_t DiiFFPlayer::SetCallback(DiiMediaBaseCallback callback) {
        callback_ = callback;
        return 0;
    }

    void DiiFFPlayer::DoStatistics(DiiPlayerStatistics& statistics) {
        
    }
}

#include "videoplayer.h"


extern "C"{
    #include <SDL.h>
    #include <SDL_audio.h>
    #include <SDL_types.h>
    #include <SDL_name.h>
    #include <SDL_syswm.h>
    #include <SDL_video.h>
    #include <SDL_main.h>
    #include <SDL_config.h>
}

int volume; //音量 0-128

struct PacketQueue
{
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
};
PacketQueue *audioq;


void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

    AVPacketList *pkt1;
    if(av_dup_packet(pkt) < 0)
    {
        return -1;
    }
    pkt1 = (AVPacketList*)av_malloc(sizeof(AVPacketList));
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
    SDL_UnlockMutex(q->mutex);
    return 0;
}

static void packet_queue_clear(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    AVPacketList *tmp;
    while (1)
    {
        tmp = q->first_pkt;
        if (tmp == NULL) break;
        q->first_pkt = tmp->next;
        q->size -= tmp->pkt.size;
        av_free(tmp);
    }
    q->last_pkt = NULL;
    SDL_UnlockMutex(q->mutex);
}

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
    AVPacketList *pkt1;
    int ret;
    SDL_LockMutex(q->mutex);
    pkt1 = q->first_pkt;
    if (pkt1)
    {
        q->first_pkt = pkt1->next;
        if (!q->first_pkt)
        q->last_pkt = NULL;
        q->nb_packets;
        q->size -= pkt1->pkt.size;
        *pkt = pkt1->pkt;
        av_free(pkt1);
        ret = 1;
    }
    else
    {
        ret = -1;
    }
     SDL_UnlockMutex(q->mutex);
     return ret;
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size)
{
    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    int len1, data_size;
    for(;;)
    {
        if(packet_queue_get(audioq, &pkt, 1) < 0)
        {
            return -1;
        }
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
        while(audio_pkt_size > 0)
        {
            data_size = buf_size;
            len1 = avcodec_decode_audio2(aCodecCtx, (int16_t *)audio_buf, &data_size,audio_pkt_data, audio_pkt_size);
            if(len1 < 0)
            {
                audio_pkt_size = 0;
                break;
            }
            audio_pkt_data += len1;
            audio_pkt_size -= len1;
            if(data_size <= 0)
            {
                continue;
            }
            return data_size;
        }
        if(pkt.data)
            av_free_packet(&pkt);
   }
}

void audio_callback(void *userdata, Uint8 *stream, int len)
{
    AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
    int len1, audio_size;
    static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;
    while(len > 0)
    {
        if(audio_buf_index >= audio_buf_size)
        {
            audio_size = audio_decode_frame(aCodecCtx, audio_buf,sizeof(audio_buf));
            if(audio_size < 0)
            {
                audio_buf_size = 1024;
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
        SDL_MixAudio(stream, (uint8_t * )audio_buf + audio_buf_index, len1, volume);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
}

/***
 ***DecodeVideo类的成员
 ***/
DecodeVideo::DecodeVideo()
{
    bufferRGB = NULL;
    mutex = NULL;
}
DecodeVideo::~DecodeVideo()
{

}
void DecodeVideo::setAVCodecContext(AVCodecContext*ctx)
{
    pCodecCtx = ctx;
    width = pCodecCtx->width;
    height= pCodecCtx->height;
    pix_fmt = pCodecCtx->pix_fmt;
    if (bufferRGB != NULL)
    {
        av_free(bufferRGB);
        bufferRGB = NULL;
    }
    int numBytes = avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width,pCodecCtx->height);
    bufferRGB = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
}

void DecodeVideo::run()
{
    int frameFinished = 0;
    AVFrame *pFrame = avcodec_alloc_frame();
    SDL_LockMutex(mutex);
    avcodec_decode_video(pCodecCtx, pFrame, &frameFinished,packet.data,packet.size);
    SDL_UnlockMutex(mutex);

    AVFrame *pFrameRGB;
    pFrameRGB = avcodec_alloc_frame();
    avpicture_fill((AVPicture *)pFrameRGB, bufferRGB, PIX_FMT_RGB24,pCodecCtx->width, pCodecCtx->height);
    SwsContext *convert_ctx = sws_getContext(width,height,pix_fmt,width,height,PIX_FMT_RGB24,SWS_BICUBIC, NULL,NULL,NULL);
    sws_scale(convert_ctx,(const uint8_t*  const*)pFrame->data,pFrame->linesize,0,height,pFrameRGB->data,pFrameRGB->linesize);
    QImage tmpImage((uchar *)bufferRGB,width,height,QImage::Format_RGB888);
    QImage image  = tmpImage.copy();
    av_free(pFrameRGB);
    sws_freeContext(convert_ctx);
    emit readOneFrame(image);

    av_free_packet(&packet);
}

/***
 ***VideoPlayer类的成员
 ***/
VideoPlayer::VideoPlayer()
{
    initAvcodec();
    audioq = new PacketQueue;
    packet_queue_init(audioq);

    mutex = SDL_CreateMutex();
    decodeVideoMutex = SDL_CreateMutex();

    aCodecCtx  = NULL;
    pFormatCtx = NULL;
    eventloop  = NULL;
    curState = StoppedState;
    curType = NoneType;

    decodeVideoThread = new DecodeVideo;
    decodeVideoThread_2 = new DecodeVideo;
    decodeVideoThread_3 = new DecodeVideo;
    connect(decodeVideoThread,SIGNAL(readOneFrame(QImage)),this,SIGNAL(readOneFrame(QImage)));
    connect(decodeVideoThread_2,SIGNAL(readOneFrame(QImage)),this,SIGNAL(readOneFrame(QImage)));
    connect(decodeVideoThread_3,SIGNAL(readOneFrame(QImage)),this,SIGNAL(readOneFrame(QImage)));

    setVolume(128);
}

VideoPlayer::~VideoPlayer()
{

}

void VideoPlayer::run()
{
    eventloop = new QEventLoop;
    QTimer playtimer; //控制播放的定时器
    connect(&playtimer,SIGNAL(timeout()),this,SLOT(readPacket()),Qt::DirectConnection);
    playtimer.start(10);
    eventloop->exec();
    delete eventloop;
    eventloop = NULL;
}

void VideoPlayer::initAvcodec()
{
    av_register_all();
    avcodec_init();
    avcodec_register_all();
    avdevice_register_all();
}

bool VideoPlayer::openVideo(char *filename)
{
    videoStream = -1;
    audioStream = -1;
    if(av_open_input_file(&pFormatCtx, filename, NULL, 0, NULL)!=0)
    {
        fprintf(stderr, "Couldn't open file\n");
        return false;  //Couldn't open file
    }
    if(av_find_stream_info(pFormatCtx)<0)
    {
        fprintf(stderr, "Couldn't find stream information\n");
        return false ; // Couldn't find stream information
    }
    //dump_format(pFormatCtx, 0, filename, 0);  //输出视频信息到终端
    int i;
    for(i=0; i<pFormatCtx->nb_streams; i++)
    {
        if(pFormatCtx->streams[i]->codec->codec_type==CODEC_TYPE_VIDEO && videoStream < 0)
        {
            videoStream=i;
        }
        if(pFormatCtx->streams[i]->codec->codec_type==CODEC_TYPE_AUDIO && audioStream < 0)
        {
            audioStream=i;
        }
    }

    if(audioStream==-1 && videoStream==-1)
    {
        closeVideo();
        fprintf(stderr, "Didn't find a audio stream\n");
        return false; // Didn't find a audio stream
    }

    if (videoStream != -1)
    {
        // Get a pointer to the codec context for the video stream
        pCodecCtx=pFormatCtx->streams[videoStream]->codec;
        // Find the decoder for the video stream
        AVCodec *pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
        if(pCodec==NULL)
        {
            fprintf(stderr, "Unsupported codec!\n");
            return false; // Codec not found
        }
        // Open codec
        if(avcodec_open(pCodecCtx, pCodec)<0)
        {
            fprintf(stderr, "Could not open audio codec!\n");
            return false; // Could not open audio codec
        }
        curType = VideoType;
    }
    else
    {
        curType = AudioType;
    }

    if (audioStream != -1)
    {
        aCodecCtx = pFormatCtx->streams[audioStream]->codec;
        AVCodec *aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
        if(!aCodec)
        {
            fprintf(stderr, "Unsupported codec!\n");
            return false;
        }
        if(avcodec_open(aCodecCtx, aCodec)<0)
        {
            fprintf(stderr, "Could not open video codec!\n");
            return false; // Could not open video codec
        }
    }

    totaltime = pFormatCtx->duration;
    return true;
}

void VideoPlayer::closeVideo()
{
    if (aCodecCtx != NULL)
    {
        avcodec_close(aCodecCtx);
    }
    if (pFormatCtx != NULL)
    {
        av_close_input_file(pFormatCtx);
    }
    aCodecCtx  = NULL;
    pFormatCtx = NULL;
    curType = NoneType;
}

bool VideoPlayer::openSDL()
{
    if (aCodecCtx == NULL)
    {
        return false;
    }
    SDL_LockAudio();
    SDL_AudioSpec spec;
    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = aCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = 1024;//SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = aCodecCtx;
    if(SDL_OpenAudio(&wanted_spec, &spec) < 0)
    {
        fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
        return false;
    }
    SDL_UnlockAudio();
    SDL_PauseAudio(0);

    return true;
}

bool VideoPlayer::closeSDL()
{
    clearQuene();
    SDL_LockAudio();
    SDL_CloseAudio();
    SDL_UnlockAudio();
}

void VideoPlayer::clearQuene()
{
    packet_queue_clear(audioq);
}

void VideoPlayer::readPacket()
{
    if (pFormatCtx == NULL) return;
    SDL_LockMutex(mutex);
    currenttime+=10;
    if (currenttime >= nextPacket.dts)
    {
        if(nextPacket.stream_index == videoStream)
        {
            if (!decodeVideoThread->isRunning())
            {
                decodeVideoThread->setPacket(nextPacket);
                decodeVideoThread->start();
            }
            else if (!decodeVideoThread_2->isRunning())
            {
                decodeVideoThread_2->setPacket(nextPacket);
                decodeVideoThread_2->start();
            }
            else if (!decodeVideoThread_3->isRunning())
            {
                decodeVideoThread_3->setPacket(nextPacket);
                decodeVideoThread_3->start();
            }
            else
            {
                //qDebug()<<"running...";
                //提高性能在此多添加几个线程即可
            }
        }
        else if(nextPacket.stream_index==audioStream)
        {
             packet_queue_put(audioq, &nextPacket);
             emit updateTime(currenttime);
        }
        if(av_read_frame(pFormatCtx, &nextPacket) < 0)
        {//整个视频文件读取完毕
            stop();
            currenttime = totaltime;
            emit updateTime(currenttime);
            emit finished();
        }
    }
    SDL_UnlockMutex(mutex);
}

void VideoPlayer::setSource(QString str)
{
    stop();
    char ch[1024];
    strcpy(ch,(const char*)str.toLocal8Bit());
    if (openVideo(ch))
    {
        currenttime = 0;
        av_read_frame(pFormatCtx, &nextPacket);
        if (curType == VideoType)
        {
            decodeVideoThread->setAVCodecContext(pCodecCtx);
            decodeVideoThread->setMutex(decodeVideoMutex);
            decodeVideoThread_2->setAVCodecContext(pCodecCtx);
            decodeVideoThread_2->setMutex(decodeVideoMutex);
            decodeVideoThread_3->setAVCodecContext(pCodecCtx);
            decodeVideoThread_3->setMutex(decodeVideoMutex);
        }
    }
    else
    {
        fprintf(stderr,"open %s erro!\n",ch);
    }
}

void VideoPlayer::play()
{
    if (isRunning()) return;
    if (pFormatCtx != NULL)
    {
        openSDL();
        start();
        curState = PlayingState;
        emit stateChanged(curState);
    }
}

void VideoPlayer::pause()
{
    if (eventloop == NULL) return;
    eventloop->exit();
    closeSDL();
    curState = PausedState;
    emit stateChanged(curState);
}

void VideoPlayer::stop()
{
    if (eventloop == NULL) return;
    eventloop->exit();
    closeVideo();
    closeSDL();

    curState = StoppedState;
    emit stateChanged(curState);
}

void VideoPlayer::seek(qint64 time)
{
    SDL_LockMutex(mutex);
    if (pFormatCtx != NULL)
    {
        clearQuene();

        int stream_index;
        if  (videoStream >= 0) stream_index = videoStream;
          else if(audioStream >= 0) stream_index = audioStream;

        if (time < pFormatCtx->start_time) time=pFormatCtx->start_time;
        if (time > totaltime) time = totaltime;

        int target= av_rescale_q(time, AV_TIME_BASE_Q,pFormatCtx->streams[stream_index]->time_base);
        av_seek_frame(pFormatCtx,stream_index,target,AVSEEK_FLAG_FRAME); //AV_TIME_BASE
        currenttime = target;
        if(av_read_frame(pFormatCtx, &nextPacket)>=0)
        {
            if (nextPacket.dts>0)
                currenttime = nextPacket.dts;
            else currenttime = nextPacket.pts - 30;
        }
    }

    SDL_UnlockMutex(mutex);
    emit updateTime(currenttime);
}

void VideoPlayer::setVolume(int value)
{
    volume = value;
}


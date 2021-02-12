extern "C" {
#  include <libavcodec/avcodec.h>
#  include <libavformat/avformat.h>
#  include <libswscale/swscale.h>
#  include <libavutil/avutil.h>
}

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#  define av_frame_alloc  avcodec_alloc_frame
#  define av_frame_free   avcodec_free_frame
#endif

#include <iostream>
#include <iomanip>
#include <string>

// ImageMagick libraries
#include <Magick++.h>

#include "parameters.h"

namespace processor {
  class videoProcessor {

  public:
    videoProcessor         ( void );
    videoProcessor         ( bool, bool=false, bool=false, bool=true, double=5.0, double=0.024 );
    ~videoProcessor        ( void );

    int  openFile          ( std::string, bool=true );
#ifdef C_USAGE
    int  openFile          ( char *, bool=true );
#endif
    int  getCodecInfo      ( void );
    bool processFile       ( int=0, int=0 );
    bool processFrame      ( void );
    bool encodeFrame       ( void );
    void setupEncoder      ( void );
    bool processImage      (const Magick::PixelPacket *);
    bool SaveFrameAsPPM    ( AVFrame *, char * );
    bool makeMagickImage   ( void );

  private:
    int videoStream;
    int iFrame;
    float frames_per_second;
    float seconds_per_frame;
    long number_of_frames;
    double duration, gap, noise, noise_per_pixel;

    unsigned int sample_rate;

    std::string fileName;
    std::string outFile;

    FILE *of;
    bool encoder_initialized, verbose, encode, inMemImage, write_edl;

    AVFormatContext *pFormatCtx;
    AVCodecContext  *pCodecCtx, *pCodecCtxOrig, *codecCtx;
    AVCodec         *pCodec, *codec;
    AVFrame         *pFrame;

    struct SwsContext *sws_ctx;
  };

} // End of namespace processor

#include "videoProcessor.h"

using namespace std;
using namespace Magick;

#ifndef C_USAGE
parameters *p;
#else
extern parameters *p;
#endif

namespace processor {
videoProcessor::videoProcessor( void ) {

#ifndef C_USAGE
  p = new parameters("image.rcp");
#endif

  // If we've got a parameters class, read in the values
  if ( p != 0x0 ) { 

    this->verbose         = p->getBool("VERBOSE");
    this->encode          = p->getBool("ENCODE");
    this->inMemImage      = p->getBool("IN_MEMORY_IMAGE");
    this->write_edl       = p->getBool("WRITE_EDL") && !this->encode;

    this->gap             = p->getDouble("GAP");
    this->noise_per_pixel = p->getDouble("NOISE_PER_PIXEL");

    if ( this->noise_per_pixel == 0.0 ) {
      this->noise = p->getDouble("NOISE");
      if ( this->noise == 0.0 ) {
	cerr << "Nonsense noise level n = 0! No way Jose\n";
	exit(1);
      }
    } else
      this->noise = 0.0;

  } else { // Otherwise, set the defaults
    this->verbose         = false;
    this->encode          = false;
    this->inMemImage      = true;
    this->write_edl       = true;

    this->gap             = 5.0;
    this->noise_per_pixel = 0.024;
    this->noise           = 0.0;
  }


  this->sample_rate         = 90;           // Sample rate for h.264 is 90 kHz
  this->encoder_initialized = false;        // The encoder isn't set up yet

  // Initialize the pointers
  pFormatCtx = 0x0;
  pCodecCtx = pCodecCtxOrig = codecCtx = 0x0;
  pCodec = codec = 0x0;
  pFrame = 0x0;
  of = 0x0;

  cout << setprecision(4);

  // Register the available formats & codecs with the library
  av_register_all();

  // Initialize the Magick++ image system
  InitializeMagick("");

  if ( this->verbose ) {
    cout << "Encode = " << this->encode << "\n";
    cout << "In Mem Images = " << this->inMemImage << "\n";
    cout << "Write EDL = " << this->write_edl << "\n";
    cout << "Gap = " << this->gap << "\n";
    cout << "Noise/pixel = " << this->noise_per_pixel << "\n";
    cout << "noise = " << this->noise << "\n";
    cout << "Sample rate = " << this->sample_rate << "\n";
  }

  if ( p != 0x0 )
    delete p;

  return;
}

videoProcessor::videoProcessor( bool setInMemImage, bool setVerbose, bool setEncode, bool setEdl, double setGap, double setNoisePerPixel ) {

  this->verbose             = setVerbose;
  this->encode              = setEncode;
  this->inMemImage          = setInMemImage;
  this->write_edl           = setEdl && !setEncode;
  
  this->gap                 = setGap;
  this->noise_per_pixel     = setNoisePerPixel;

  if ( this->noise_per_pixel == 0.0 ) {
    cerr << "Nonsense noise level n = 0! No way Jose\n";
    exit(1);
  }
  this->noise               = 0.0;

  this->sample_rate         = 90;           // Sample rate for h.264 is 90 kHz
  this->encoder_initialized = false;        // The encoder isn't set up yet

  // Initialize the pointers
  pFormatCtx = 0x0;
  pCodecCtx = pCodecCtxOrig = codecCtx = 0x0;
  pCodec = codec = 0x0;
  pFrame = 0x0;
  of = 0x0;

  cout << setprecision(4);

  // Register the available formats & codecs with the library
  av_register_all();

  // Initialize the Magick++ image system
  InitializeMagick("");

  if ( this->verbose ) {
    cout << "Encode = " << this->encode << "\n";
    cout << "In Mem Images = " << this->inMemImage << "\n";
    cout << "Write EDL = " << this->write_edl << "\n";
    cout << "Gap = " << this->gap << "\n";
    cout << "Noise/pixel = " << this->noise_per_pixel << "\n";
    cout << "noise = " << this->noise << "\n";
    cout << "Sample rate = " << this->sample_rate << "\n";
  }

  return;
}

videoProcessor::~videoProcessor( void ) {

  // If the input file is open, close it
  if ( pFormatCtx ) {
    if ( verbose )
      cout << "closing  file format context pFormatCtx\n";
    avformat_close_input(&pFormatCtx);
  }

  // Free the codecs
  if ( pCodecCtx ) {
    if ( verbose )
      cout << "Closing codec context pCodecCtx\n";
    avcodec_close(pCodecCtx);
  }
  if ( pCodecCtxOrig ) {
    if ( verbose )
      cout << "Closing codec context pCodecCtxOrig\n";
    avcodec_close(pCodecCtxOrig);
  }

  // and the YUV frame
  if ( pFrame ) {
    if ( verbose )
      cout << "Freeing the memory in pFrame\n";
    av_free(pFrame);
  }

  if ( of ) {
    if ( verbose )
      cout << "Closing " << fileName << "\n";
    fclose(of);
  }

  if ( verbose )
    cout << "Clean up done. videoProcessor shutting down\n";

  return;
}

#ifdef C_USAGE
int videoProcessor::openFile( char * file, bool getCodec ) {
  return openFile( string(file), getCodec );
}
#endif

int videoProcessor::openFile( string file, bool getCodec ) {

  fileName = file;
  outFile = "edited_" + file;

  // Initialize the format pointer
  if ( pFormatCtx )
    avformat_close_input(&pFormatCtx);

  pFormatCtx = NULL;

  // Open video file (fills the format pointer structure)
  if ( avformat_open_input(&pFormatCtx, fileName.c_str(), NULL, NULL) != 0 ) {
    cerr << "Unable to open " << fileName << "\n";
    return -2;
  }

  // Retrieve stream information from the open file
  // Should populate pFormatCtx[m]->streams with information
  if ( avformat_find_stream_info(pFormatCtx, NULL) < 0 ) {
    cerr << "Unable to read stream information from " << fileName << "\n";
    return -3;
  }

  // Walk the streams in the file until we find a video stream
  pCodecCtxOrig = pCodecCtx = NULL;

  // Find the first video stream
  videoStream=-1;
  for ( uint i=0; i<pFormatCtx->nb_streams; i++ ) {
    if( pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ) {
      videoStream=i;
      break;
    }
  }

  // Make sure we've got an actual video stream.... 
  if( videoStream == -1 ) {
    cerr << "Unable to find a video stream in " << fileName << "\n";
    return -4;
  }

  // Close the codecs if they're open
  if ( pCodecCtx )
    avcodec_close(pCodecCtx);
  if ( pCodecCtxOrig )
    avcodec_close(pCodecCtxOrig);

  // Get a pointer to the codec context for the video stream
  // (Do both to make sure the memory is allocated or the 
  // copy_context below will choke)
  pCodecCtx = pCodecCtxOrig = 
    pFormatCtx->streams[videoStream]->codec;

  AVStream *st = pFormatCtx->streams[videoStream];
  frames_per_second = av_q2d(st->avg_frame_rate);
  seconds_per_frame = 1.0f / frames_per_second;

  if (pFormatCtx->duration != AV_NOPTS_VALUE) {
    duration = (pFormatCtx->duration + 5000.0) / AV_TIME_BASE;
    number_of_frames = frames_per_second * duration;

    if ( verbose ) {
      // Dump information about file onto standard error
      av_dump_format(pFormatCtx, 0, fileName.c_str(), 0);

      cout << "duration = " << duration << " s"
	   << ", # frames = " << number_of_frames 
	   << ", frame duration = " << seconds_per_frame << " s"
	   << ", frame rate = " << frames_per_second << " Hz"
	   << "\n";
    }
  }

  iFrame = 0;

  // If we're auto getting the code, do it here
  if ( getCodec )
    return (getCodecInfo()>0);


  return true;
}

int videoProcessor::getCodecInfo( void ) {

  // Find the decoder for the video stream
  pCodec = avcodec_find_decoder(pCodecCtx->codec_id);

  if ( !pCodec ) {
    cerr << "Unsupported codec!\n";
    return -5;
  }

  // Copy context from the codec
  pCodecCtx = avcodec_alloc_context3(pCodec);

  // And store it also in the backup context
  if ( avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0 ) {
    cerr << "Couldn't copy codec context\n";
    return -6;
  }

  // Open codec
  if( avcodec_open2(pCodecCtx, pCodec, NULL) < 0 ) {
    cerr << "Unable to open codec\n";
    return -7;
  }

  // If we're also encnoding, set up the encoder 
  // here since we've got the codec parameters
  if ( encode )
    setupEncoder();

  // Now that we understand the codec.... allocate the memory
  pFrame=av_frame_alloc();
  if( !pFrame ) {
    cerr << "Unable to allocate a frame\n";
    return -8;
  }

  return 1;
}

void videoProcessor::setupEncoder( void ) {

  if ( !encode )
    return;

  // If there's already an open file, close it
  if ( of )
    fclose(of);
    
  // And open the output file for writing
  of = fopen(outFile.c_str(), "wb");
  if (!of) {
    cerr << "could not open " << outFile << " for output\n";
    encoder_initialized = false;
    return;
  }
  
  // If there's an active codec context around, flush it
  if ( codecCtx )
    avcodec_close(codecCtx);

  codec = avcodec_find_encoder(pCodec->id);
  if ( !codec ) {
    cerr << "Unable to find codec " << pCodec->id << "\n";
    encoder_initialized = false;
    return;
  }

  // Allocate the context & copy the parameters from the original
  codecCtx               = avcodec_alloc_context3(codec);
  codecCtx->bit_rate     = pCodecCtx->bit_rate;
  codecCtx->width        = pCodecCtx->width;
  codecCtx->height       = pCodecCtx->height;
  codecCtx->time_base    = pCodecCtx->time_base;
  codecCtx->gop_size     = pCodecCtx->gop_size;
  codecCtx->max_b_frames = pCodecCtx->max_b_frames;
  codecCtx->pix_fmt      = pCodecCtx->pix_fmt;

  // Open the codec & context
  if (avcodec_open2(codecCtx, codec, NULL) < 0) {
    cerr << "could not open codec " << pCodec->id << "\n";
    encoder_initialized = false;
    return;
  }

  encoder_initialized = true;
  return;
}

bool videoProcessor::encodeFrame( void ) {

  static int errors = 0;
  static ulong frame_number = 0;

  if ( !encoder_initialized )
    return false;

  AVPacket pkt;
  int got_output = 0, ret = 0;

  av_init_packet(&pkt);
  pkt.data = NULL;    // packet data will be allocated by the encoder
  pkt.size = 0;

  AVPixelFormat f = codecCtx->pix_fmt;
  int w = codecCtx->width, h = codecCtx->height;

  // Initialize a new frame
  AVFrame *frame = av_frame_alloc(); //avcodec_alloc_frame();

  // Allocate a buffer
  uint8_t* picture_buf = (uint8_t *)av_malloc(avpicture_get_size(f, w, h));

  // And copy the original frame to the temp
  avpicture_fill ((AVPicture *)frame, picture_buf, f, w, h);
  av_picture_copy((AVPicture*)frame, (AVPicture*)pFrame, f, w, h);

  // Set the parameters
  frame->pts = seconds_per_frame * sample_rate * (++frame_number);
  frame->key_frame = 0;
  frame->format = pFrame->format;
  frame->width  = pFrame->width;
  frame->height = pFrame->height;

  // encode the new frame
  ret = avcodec_encode_video2(codecCtx, &pkt, frame, &got_output);

  // And free the allocated memory
  av_free( picture_buf );
  av_free( frame );

  if (ret < 0) {
    cerr << "error encoding frame\n";
    if ( ++errors > 10 ) {
      encoder_initialized = false;
      cerr << "Too many errors encoding video.... shutting down encoder\n";
    }
    return false;
  }

  if (got_output) {
    errors = 0;
    //printf("encoded frame %3d (size=%5d)\n", iFrame, pkt.size);
    fwrite(pkt.data, 1, pkt.size, of);
    av_free_packet(&pkt);
    return true;
  }

  return false;
}

bool videoProcessor::processFile( int startFrame, int numFrames ) {

  int frameFinished;
  AVPacket packet;

  if ( numFrames <= 0 )
    numFrames = number_of_frames;

  // Read in one complete frame
  while ( av_read_frame(pFormatCtx, &packet) >= 0 ) {

    // Is this a packet from the video stream?
    if ( packet.stream_index == videoStream ) {

      // Decode video frame
      avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
    
      // Did we get a video frame?
      if ( frameFinished ) {

	if ( false || startFrame >= 0 ) {
	  if ( iFrame >= startFrame && iFrame < startFrame+numFrames )
	    makeMagickImage();

	  if ( iFrame >= startFrame+numFrames )
	    break;
	}
	iFrame++;

      } // End if(frameFinished)
    } // End if(packet.stream_index==videoStream)
    
    // Free the packet that was allocated by av_read_frame
    av_free_packet(&packet);

  } // End while(av_read_frame(pFormatCtx[m], &packet)>=0)

  processImage(NULL);

  return true;
}

bool videoProcessor::processFrame( void ) {

  AVPacket packet;
  int frameFinished = 0;

  av_init_packet(&packet);

  while ( !frameFinished ) {

    // Read in a packet from the file
    if ( av_read_frame(pFormatCtx, &packet) >= 0 ) {

      // If this is a packet from the video stream decode it
      if ( packet.stream_index == videoStream )
	avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

    } else {

      // Free the packet that was allocated by av_read_frame
      av_free_packet(&packet);

      // Close up the image processing
      processImage(NULL);

      // And notify our caller that we're done with this file
      return false;

    }

  } // End while ( !frameFinished )

  // Make the frame into an image
  makeMagickImage();

  iFrame++;

  // Free the packet that was allocated by av_read_frame
  av_free_packet(&packet);

  return true;
}

bool videoProcessor::makeMagickImage( void ) {

  static AVFrame *destFrame = NULL;
  static SwsContext *swsCtx = NULL;
  static uint8_t *buff;
  char szFilename[64];
  static AVPixelFormat pix_fmt = (inMemImage) ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB24;

  if ( !destFrame ) {
      if ( verbose )
	cout << "Allocating new frame \n";

      destFrame = av_frame_alloc(); //avcodec_alloc_frame();
      int numBytes = avpicture_get_size(pix_fmt, pCodecCtx->width, pCodecCtx->height);
      buff = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
      avpicture_fill((AVPicture *) destFrame, buff, pix_fmt, pCodecCtx->width, pCodecCtx->height);

    }
    if ( !swsCtx ) {
      if ( verbose )
	cout << "Creating context for software scaler\n";

      swsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
			      pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, 
			      pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
    }
  
    sws_scale(swsCtx, pFrame->data, pFrame->linesize, 0,
	      pCodecCtx->height, destFrame->data,
	      destFrame->linesize);

  Image *image;
  if ( inMemImage )
    image = new Image(pCodecCtx->width, pCodecCtx->height, "RGBA", CharPixel, destFrame->data[0]);
  else {
    sprintf(szFilename, "/dev/shm/frame%d.pgm", iFrame);
    SaveFrameAsPPM( destFrame, szFilename );
    image = new Image(szFilename);
  }

  bool keep = processImage( image->getConstPixels(0,0,pCodecCtx->width,pCodecCtx->height) );

  if ( encoder_initialized && keep )    
    encodeFrame();

  if ( image )
    delete image;

  if ( !inMemImage )
    remove(szFilename);

  return true;
}

bool videoProcessor::processImage(const PixelPacket *pixel_cache) {

    static bool keeper = true;
    static double start = 0.0, end = 0.0;
    static double one_over_max = 1.0 / 65535.0;

    if ( this->noise == 0.0 )
      this->noise = this->noise_per_pixel * pCodecCtx->width * pCodecCtx->height;

    if ( !pixel_cache ) {
      if ( !keeper ) {
	if ( write_edl )
	  cout << start << "\t" << seconds_per_frame * number_of_frames << "\t0\n";
	start = end = 0.0;
	keeper = true;
	return false;
      }
      return true;
    }

    double saturation = 0.0;
    for ( int row=0; row<pCodecCtx->height; row++ ) {
      for ( int col=0; col<pCodecCtx->width; col++ ) {
	saturation += one_over_max * pixel_cache[pCodecCtx->width * row + col].green;
      }
    }

    if ( saturation > this->noise ) {

      if ( !keeper ) {
	end = seconds_per_frame * iFrame;
	if ( end - start < this->gap )
	  end = 0;
	else {
	  keeper = true;
	  if ( start != 0 )
	    start--;
	  if ( end < duration-1 )
	    end++;
	  if ( write_edl )
	    cout << start << "\t" << end << "\t0\n";
	  start = 0;
	}
      }

    } else {
      
      if ( keeper ) {
	start = seconds_per_frame * iFrame;
	if ( iFrame == 0 || start - end > this->gap ) {
	  keeper = false;
	  end = 0;
	} else
	  start = 0;
      }

    }

  return keeper;
}

bool videoProcessor::SaveFrameAsPPM(AVFrame *pFrame, char *szFilename) {
  FILE *pFile;
 
  // Open file
  pFile=fopen(szFilename, "wb");
  if(pFile==NULL)
    return false;

  // Write header
  fprintf(pFile, "P6\n%d %d\n255\n", pCodecCtx->width, pCodecCtx->height);

  // Write pixel data
  for(int y=0; y<pCodecCtx->height; y++)
    fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, pCodecCtx->width*3, pFile);

  // Close file
  fclose(pFile);

  return true;
}

} // end namespace processor

#ifndef C_USAGE
/**************************** Enable the python bindings ***********************/
#include <boost/python.hpp>

using namespace boost::python;
using namespace processor;

//int (videoProcessor::*of1)(std::string, bool ) = &videoProcessor::openFile;
//int (videoProcessor::*of2)(char *,      bool ) = &videoProcessor::openFile;

BOOST_PYTHON_MEMBER_FUNCTION_OVERLOADS(processFile_overloads, videoProcessor::processFile, 0, 2)
BOOST_PYTHON_MEMBER_FUNCTION_OVERLOADS(openFile_overloads, videoProcessor::openFile, 1, 2)

BOOST_PYTHON_MODULE(processor) {

  class_<videoProcessor>("videoProcessor", init<>())
    .def(init<bool,optional<bool,bool,bool,double,double>>())
    //.def("openFile", of1)
    //.def("openFile", of2)
    .def("openFile", &videoProcessor::openFile, openFile_overloads())
    .def("getCodecInfo", &videoProcessor::getCodecInfo)
    .def("processFile", &videoProcessor::processFile, processFile_overloads())
    .def("processFrame", &videoProcessor::processFrame)
    .def("encodeFrame", &videoProcessor::encodeFrame)
    .def("setupEncoder", &videoProcessor::setupEncoder)
    .def("processImage", &videoProcessor::processImage)
    .def("SaveFrameAsPPM", &videoProcessor::SaveFrameAsPPM)
    .def("makeMagickImage", &videoProcessor::makeMagickImage)
    ;
}
/*******************************************************************************/
#endif

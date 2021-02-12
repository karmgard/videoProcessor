#!/usr/bin/python

import processor

vp = processor.videoProcessor()

file = "movies/run_3.mp4"

# Open the video, and (by default bool=True) => get codec, info,  etc
if vp.openFile(file) >= 0 :
    #vp.processFile(-1)       # Process the entire file (start, start+num)
                              # -1 => read each frame but don't process pixels
    while vp.processFrame() : # Process the file frame by frame
        continue

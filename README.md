#videoProcessor

This is a library, written in C++, for analyzing a video stream taken in the 
test beam area at CERN in 2016. The video shows the results of placing a small
ionizing radiation detector in the lead beam (pb 82+!). The idea here is to 
take the videos frame-by-frame and estimate the energy based on pixel color. The
C++ code is compiled against the ffmpeg libraries to handle the video processing
and is built into a Python library with Jamroot.
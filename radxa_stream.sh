#!/bin/bash
# gst-launch-1.0 v4l2src device=/dev/video0 ! \
# video/x-raw,width=1280,height=720,framerate=30/1 ! \
# mpph265enc ! \
# h265parse ! \
# filesink location=/tmp/ruby/fifocam1
gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,width=1280,height=720,framerate=30/1 ! mpph264enc  ! h264parse ! filesink location=/tmp/ruby/fifocam1

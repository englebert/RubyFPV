#!/bin/bash
# gst-launch-1.0 v4l2src device=/dev/video0 ! \
# video/x-raw,width=1280,height=720,framerate=30/1 ! \
# mpph265enc ! \
# h265parse ! \
# filesink location=/tmp/ruby/fifocam1
##gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,width=1280,height=720,framerate=30/1 ! mpph264enc  ! h264parse ! filesink location=/tmp/ruby/fifocam1
# sleep 600
# bin/bash ./radxa_stream.sh -dbg -n -cd H264 -fl -w 1280 -h 720 -fps 60 -g 15 -b 14400000 -ih -sl 4 -pf high -lev 4.2 -if both -br 50 -co 0 -sa -40 -sh 0 -drc off -ex off -awb off -mm backlit -ag 2.0 -awbg 1.4,1.5 -log -t 0 -o -
# echo "$@" >> /dev/shm/debug.log
# sleep 600
killall radxa_streamer
./radxa_streamer $@

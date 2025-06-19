#!/bin/bash
ps -eaf | grep ruby | grep -v vim | awk '{print "kill -9 "$2}' | bash

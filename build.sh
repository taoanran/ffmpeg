#!/bin/bash
pushd SDL-1.2.15
./configure; make;sudo make install
popd

pushd ffmpeg
./ffmpeg/configure; make;sudo make install
popd

echo "build end !!!!!!!!!!!!!!"

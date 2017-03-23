#!/bin/sh
cd ..
mkdir build
cd build
cmake .. -G "Unix Makefiles" -DTEST=1
make
#!/bin/bash

# Call this script from Project Root Directory.
# Requires elevated rights to install ECM
set -e

cd third_party/syntax-highlighting
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF -DBUILD_TESTING=OFF 

cmake --build . --parallel $(nproc)
sudo cmake --build . --target install
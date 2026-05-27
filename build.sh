#!/bin/sh
export IDF_PATH="${IDF_PATH:-$HOME/esp/v5.4.3/esp-idf}"
. "$IDF_PATH/export.sh"  # get_idf
rm -rf build
idf.py build

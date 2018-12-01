#!/bin/bash

set -x

for t in tests/*.bin; do
    dot=${t%.*}.dot
    png=${t%.*}.png
    ./cfg -i $t -o $dot
    dot -Tpng $dot > $png
done

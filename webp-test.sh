#!/bin/bash
# image size 320x240 as 5x4 64x64 pixel blocks
for y in {0,1,2,3}; do
for x in {0,1,2,3,4}; do
cwebp -short -segments 1 -size 230 -pass 9 -crop $((64*x)) $((64*y)) 64 64 test.jpg -o test.webp
ssdv -i 0 -p $((16*$y+$x)) -c TEST test.webp test.ssdv
ssdv -d test.ssdv newtest.webp
dwebp -ppm newtest.webp -o test$x.ppm
done
convert +append test*ppm testout$y.jpg
done
convert -append testout*jpg result.jpg

#!/bin/bash
# image size 320x128 as 8 320x16 pixel blocks
for y in {0,1,2,3,4,5,6,7}; do
cwebp -short -segments 1 -size 230 -pass 9 -crop 0 $((16*y)) 320 16 test.jpg -o test.webp
ssdv -i 0 -p $((y)) -c TEST test.webp test.ssdv
ssdv -d test.ssdv newtest.webp
dwebp -ppm newtest.webp -o test$y.ppm
done
convert -append test*ppm testout.jpg

#!/bin/bash
for y in {0,1,2,3,4}; do
for x in {0,1,2,3,4,5}; do
cwebp -short -segments 2 -size 230 -pass 5 -crop $((48*x)) $((48*y)) 48 48 test.jpg -o test.webp
ssdv -i 0 -p $((16*$y+$x)) -c TEST test.webp test.ssdv
ssdv -d test.ssdv newtest.webp
dwebp -ppm newtest.webp -o test$x.ppm
done
convert +append test*ppm testout$y.jpg
done
convert -append testout*jpg result.jpg

#!/usr/bin bash

mkdir -p my_output
mkdir -p answer
make > /dev/null

echo "-Check-"
for i in $(seq -f "%02g" 1 16)
do
        echo "-Testing trace$i-"
        make test$i > my_output/$i.txt
        make rtest$i > answer/$i.txt
        sed -i 's/tshref/tsh/' answer/$i.txt
        sed -i 's/rtest/test/' answer/$i.txt
        sed -i 's/(\b[0-9]\+\b)/(00000)/' answer/$i.txt
        sed -i 's/(\b[0-9]\+\b)/(00000)/' my_output/$i.txt
        sed -i 's/\b[0-9]\+\b pts/00000 pts/' answer/$i.txt
        sed -i 's/\b[0-9]\+\b pts/00000 pts/' my_output/$i.txt
        diff my_output/$i.txt answer/$i.txt && echo "Correct"
done

rm -rf my_output
rm -rf answer
#! /usr/bin/bash

DATE=`date +%Y%m%d_%H%M`
for show in ht3 cbf sma cae ; do
    echo ${show}
    /shots/psr/pst/bin/st-search -s ${show} --live --newer 1 --output path --match /pix/ |tee stsearch.${DATE}.${show}.txt
done

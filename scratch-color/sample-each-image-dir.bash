#!/usr/bin/bash

file_list_file=$1

while read d ; do
#    echo `ls -1 $d/* | grep -v ':' | head -1`
#    echo `ls -1 $d/*/* | grep -v ':' | grep -v cannot | head -1`
#    echo `ls -R -1 $d/* | head -1`
    a=`ls -1 $d/*.{exr,tif,tiff,jpg,jpeg,dpx,iff,png} | head -1 | grep -v cannot`
    if [ "$a" != "" ] ; do
        echo $a
    done
done<${file_list_file}

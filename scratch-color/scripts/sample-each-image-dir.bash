#!/bin/bash

file_list_file=$1

while read d ; do
    a=`ls -1 $d/*.{exr,tif,tiff,jpg,jpeg,dpx,iff,png} | head -1 | grep -v cannot`
    if [ "$a" != "" ] ; do
        echo $a
    done
done<${file_list_file}

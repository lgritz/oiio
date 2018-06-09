#!/usr/bin/env python

# First arg is a filename that lists all the image files, one per line.
# Second arg is the name of the file that we are appending to.
#
# Shuffle all the files randomly.
#
# For each input file listed, we append 5 items for each line of the output:
#   filename, in double quotes
#   color space name, in double quotes
#   R histogram, as a tuple of 64 int values
#   G histogram
#   B histogram
#
# stderr will get echo of the filenames as they are processed
#


from __future__ import print_function
import os
import sys
import random
import OpenImageIO as oiio
from OpenImageIO import ImageBuf, ImageBufAlgo

bins = 64

if len(sys.argv) != 2 :
    print ("Bad command line, need exactly one argument")
    exit(1)

file_list_file = sys.argv[1]

with open(sys.argv[1]) as file :
    lines = file.read().splitlines()

total = len(lines)

random.shuffle(lines)
processed = 0

for filename in lines :
    if filename == '' :
        continue
    fullimg = ImageBuf(filename)

    # Force a read to float, then downsize by a factor of 2. Part of the
    # reason we do that is to generate intermediate values.
    fullimg.read (force=True, convert=oiio.FLOAT)
    img = ImageBufAlgo.resize (fullimg, filtername="box", filterwidth=2.0,
                               roi=oiio.ROI(0,fullimg.spec().width/2,
                                            0,fullimg.spec().height/2))

    ignore_empty = True if img.nchannels > 3 else False

    rhist = ImageBufAlgo.histogram (img, 0, bins=bins, ignore_empty=ignore_empty)
    ghist = ImageBufAlgo.histogram (img, 1, bins=bins, ignore_empty=ignore_empty)
    bhist = ImageBufAlgo.histogram (img, 2, bins=bins, ignore_empty=ignore_empty)
    cspace = img.spec().get_string_attribute("oiio:ColorSpace")

    processed += 1
    print ('"{}" "{}" {} {} {}'.format(filename, cspace, rhist, ghist, bhist))
    print ('{}/{}: "{}"  {}'.format(processed, total, filename, cspace),
           file=sys.stderr)

    # The next section generates a linear copy if the original was sRGB
    if cspace == 'sRGB' :
        ImageBufAlgo.colorconvert (img, img, "sRGB", "linear")
        rhist = ImageBufAlgo.histogram (img, 0, bins=bins, ignore_empty=ignore_empty)
        ghist = ImageBufAlgo.histogram (img, 1, bins=bins, ignore_empty=ignore_empty)
        bhist = ImageBufAlgo.histogram (img, 2, bins=bins, ignore_empty=ignore_empty)
        cspace = img.spec().get_string_attribute("oiio:ColorSpace")

    print ('"{}" "{}" {} {} {}'.format(filename, cspace, rhist, ghist, bhist))
    print ('{}/{}: "{}"  {}'.format(processed, total, filename, cspace),
           file=sys.stderr)



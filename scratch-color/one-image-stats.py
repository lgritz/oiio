#!/usr/bin/env python

from __future__ import print_function
import os
import sys
import OpenImageIO as oiio
from OpenImageIO import ImageBuf, ImageBufAlgo

if len(sys.argv) != 2 :
    print ("Bad command line, need exactly one argument")
    exit(1)

filename = sys.argv[1]

img = ImageBuf(filename)
ignore_empty = True if img.nchannels > 3 else False

rhist = ImageBufAlgo.histogram (img, 0, bins=256, ignore_empty=ignore_empty)
ghist = ImageBufAlgo.histogram (img, 1, bins=256, ignore_empty=ignore_empty)
bhist = ImageBufAlgo.histogram (img, 2, bins=256, ignore_empty=ignore_empty)

print ('"{}"'.format(filename))
print (img.spec().get_string_attribute("oiio:ColorSpace"))
print (rhist)
print (ghist)
print (bhist)
# if os.path.isfile(fname)

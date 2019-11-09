#!/usr/bin/env python

from __future__ import print_function
from __future__ import absolute_import
import numpy
import os
from datetime import datetime
import OpenImageIO as oiio

Failure = False


def check_imagebuf (buf, result) :
    if not result or buf.has_error :
        print ('Error: ', buf.geterror())
        global Failure
        Failure = True


def filesizestr (filename) :
    size = os.path.getsize(filename)
    if size >= (1<<30) :
        return '{:.2f} GB'.format(float(size>>16)/float(1<<14))
    if size >= (1<<20) :
        return '{:.1f} MB'.format(float(size)/float(1<<20))
    if size >= (1<<10) :
        return '{:.1f} KB'.format(float(size)/float(1<<10))
    return '{} bytes'.format(size)



def test_huge (filename, formatname, res, chans, dtype, eps) :
    print ("  Testing big {} ({} x {}, ch {}, {}): {}".format(
           formatname, res, res, chans, dtype, filename))

    # Create an image of the right size (and uint8, to save RAM) and fill
    # it with a gradient.
    spec = oiio.ImageSpec(res, res, chans, 'uint8')  # Uint8 for space reasons
    gradient = oiio.ImageBuf(spec)
    ok = oiio.ImageBufAlgo.fill(gradient, ( 0.0, 0.0, 0.0, 1.0 ),
                                ( 1.0, 0.0, 0.0, 1.0 ),
                                ( 0.0, 1.0, 0.0, 1.0 ),
                                ( 1.0, 1.0, 0.0, 1.0 ))
    # Write the file
    if ok :
        print('    Writing... ', end='')
        starttime = datetime.now()
        ok = gradient.write(filename, dtype, formatname)
        if ok and os.path.exists(filename) :
            print('  wrote file size {} in {}  '.format(
                  filesizestr(filename), datetime.now()-starttime), end='')
        else :
            print('  no file written  ', end='')
        check_imagebuf(gradient, ok)
        if ok :
            print('OK')

    # Read it back, force conversion back to uint8 to save RAM
    if ok :
        print('    Reading... ', end='')
        starttime = datetime.now()
        inbuf = oiio.ImageBuf(filename)
        ok = inbuf.read(0, 0, True, 'uint8')
        print('  read {}  '.format(datetime.now()-starttime), end='')
        check_imagebuf(inbuf, ok)
        if ok :
            maxerr = 0
            cr = oiio.ImageBufAlgo.compare(gradient, inbuf, eps, 0.0, roi=oiio.ROI(1,2,1,2))
            maxerr = max(maxerr, cr.maxerror)
            cr = oiio.ImageBufAlgo.compare(gradient, inbuf, eps, 0.0, roi=oiio.ROI(res-2,res-1,res-2,res-1))
            maxerr = max(maxerr, cr.maxerror)
            if maxerr > eps :
                print ('Error: read/write images do not match, max err was', maxerr)
                ok = False
        if ok :
            print('OK')
        else :
            global Failure
            Failure = True

    # Delete the temp file
    if ok and os.path.exists(filename) :
        os.remove(filename)




######################################################################
# main test starts here

try:
    print ('Testing writing huge files in all formats:')
    all_fmts = oiio.get_string_attribute('extension_list').split(';')
    for e in all_fmts :
        fmtexts = e.split(':')
        formatname = fmtexts[0];
        # Skip "formats" that aren't amenable to this kind of testing
        if formatname in ('null', 'socket', 'field3d', 'openvdb') :
            continue
        extensions = fmtexts[1].split(',')

        out = oiio.ImageOutput.create(formatname)
        if not out :
            print ('  [skipping', formatname, '-- no writer]')
            discard = oiio.geterror()
            continue

        filename = "big.{}".format(extensions[0])

        res = (1 << 16)
        chans = 3
        dtype = 'uint8'
        eps = 0.01

        # special cases
        if formatname == 'zfile' :
            chans = 1  # zfile restricted to one channel
        if formatname == 'webp' :
            chans = 4
            res = min(res, 1<<13)
        if formatname == 'ico' :
            # ICO has a puny maxmimum size by design
            res = min(res, 256)
        if formatname == 'gif' :
            # Cap res, our GIF encoder has horrible perf for huge images.
            res = min(res, 2048)
            eps = 1.0  # And 256 color palette makes huge errors
        if formatname == 'jpeg2000' :
            # Cap res, openjpeg is super slow for high res
            res = min(res, 1 << 13)
            #eps = 1.1  # And 256 color palette makes huge errors
        if formatname == 'heif' :
            # libheif doesn't support >= 16k
            res = min(res, 16384 - 2)
        if formatname == 'iff' :
            res = min(res, 8092) # IFF doesn't support > 8k
        if formatname == 'zfile' :
            res = min(res, 32767) # maximum zfile resolution
        if formatname == 'targa' :
            res = min(res, 32768) # keep it well under 4GB, format limit
        if formatname == 'bmp' :
            res = min(res, 32768) # doesn't support over 4GB file size

        test_huge (filename, formatname, res, chans, dtype, eps)

    print ('\nDone.')
except Exception as detail:
    print ('Unknown exception:', detail)
    Failure = True

if Failure :
    exit(1)
else:
    exit(0)


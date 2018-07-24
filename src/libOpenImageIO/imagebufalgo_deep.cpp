/*
  Copyright 2013 Larry Gritz and the other authors and contributors.
  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of the software's owners nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  (This is the Modified BSD License)
*/


#include <OpenEXR/half.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagebufalgo_util.h>
#include <OpenImageIO/deepdata.h>
#include <OpenImageIO/dassert.h>
#include <OpenImageIO/thread.h>
#include <OpenImageIO/timer.h>
#include "imageio_pvt.h"


OIIO_NAMESPACE_BEGIN


// FIXME -- NOT CORRECT!  This code assumes sorted, non-overlapping samples.
// That is not a valid assumption in general. We will come back to fix this.
template<class DSTTYPE>
static bool
flatten_ (ImageBuf &dst, const ImageBuf &src, 
          ROI roi, int nthreads)
{
    ImageBufAlgo::parallel_image (roi, nthreads, [=,&dst,&src](ROI roi){
        const ImageSpec &srcspec (src.spec());
        const DeepData *dd = src.deepdata();
        int nc = srcspec.nchannels;
        int AR_channel = dd->AR_channel();
        int AG_channel = dd->AG_channel();
        int AB_channel = dd->AB_channel();
        int Z_channel = dd->Z_channel();
        int Zback_channel = dd->Zback_channel();
        int R_channel = srcspec.channelindex ("R");
        int G_channel = srcspec.channelindex ("G");
        int B_channel = srcspec.channelindex ("B");
        float *val = ALLOCA (float, nc);
        float &ARval (val[AR_channel]);
        float &AGval (val[AG_channel]);
        float &ABval (val[AB_channel]);
        DeepData tmpdd;
        tmpdd.init (1, nc, dd->all_channeltypes(), dd->all_channelnames());
        for (ImageBuf::Iterator<DSTTYPE> r (dst, roi);  !r.done();  ++r) {
            int x = r.x(), y = r.y(), z = r.z();
            tmpdd.copy_deep_pixel (0, *dd, src.pixelindex (x, y, z, true));
            tmpdd.sort (0);
            int samps = tmpdd.samples (0);
            // Clear accumulated values for this pixel (0 for colors, big for Z)
            memset (val, 0, nc*sizeof(float));
            if (Z_channel >= 0 && samps == 0)
                val[Z_channel] = 1.0e30;
            if (Zback_channel >= 0 && samps == 0)
                val[Zback_channel] = 1.0e30;
            for (int s = 0;  s < samps;  ++s) {
                float AR = ARval, AG = AGval, AB = ABval;  // make copies
                float alpha = (AR + AG + AB) / 3.0f;
                if (alpha >= 1.0f)
                    break;
                for (int c = 0;  c < nc;  ++c) {
                    float v = tmpdd.deep_value (0, c, s);
                    if (c == Z_channel || c == Zback_channel)
                        val[c] *= alpha;  // because Z are not premultiplied
                    float a;
                    if (c == R_channel)
                        a = AR;
                    else if (c == G_channel)
                        a = AG;
                    else if (c == B_channel)
                        a = AB;
                    else
                        a = alpha;
                    val[c] += (1.0f - a) * v;
                }
            }

            for (int c = roi.chbegin;  c < roi.chend;  ++c)
                r[c] = val[c];
        }
    });
    return true;
}


bool
ImageBufAlgo::flatten (ImageBuf &dst, const ImageBuf &src,
                       ROI roi, int nthreads)
{
    pvt::LoggedTimer logtime("IBA::flatten");
    if (! src.deep()) {
        // For some reason, we were asked to flatten an already-flat image.
        // So just copy it.
        return dst.copy (src);
    }

    // Construct an ideal spec for dst, which is like src but not deep.
    ImageSpec force_spec = src.spec();
    force_spec.deep = false;
    force_spec.channelformats.clear();

    if (! IBAprep (roi, &dst, &src, NULL, &force_spec,
                   IBAprep_SUPPORT_DEEP | IBAprep_DEEP_MIXED))
        return false;
    if (dst.spec().deep) {
        dst.error ("Cannot flatten to a deep image");
        return false;
    }

    const DeepData *dd = src.deepdata();
    if (dd->AR_channel() < 0 || dd->AG_channel() < 0 || dd->AB_channel() < 0) {
        dst.error ("No alpha channel could be identified");
        return false;
    }

    bool ok;
    OIIO_DISPATCH_TYPES (ok, "flatten", flatten_, dst.spec().format,
                         dst, src, roi, nthreads);
    return ok;
}



ImageBuf
ImageBufAlgo::flatten (const ImageBuf &src, ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = flatten (result, src, roi, nthreads);
    if (!ok && !result.has_error())
        result.error ("ImageBufAlgo::flatten error");
    return result;
}



bool
ImageBufAlgo::deepen (ImageBuf &dst, const ImageBuf &src, float zvalue,
                      ROI roi, int nthreads)
{
    pvt::LoggedTimer logtime("IBA::deepen");
    if (src.deep()) {
        // For some reason, we were asked to deepen an already-deep image.
        // So just copy it.
        return dst.copy (src);
        // FIXME: once paste works for deep files, this should really be
        // return paste (dst, roi.xbegin, roi.ybegin, roi.zbegin, roi.chbegin,
        //               src, roi, nthreads);
    }

    // Construct an ideal spec for dst, which is like src but deep.
    const ImageSpec &srcspec (src.spec());
    int nc = srcspec.nchannels;
    int zback_channel = -1;
    ImageSpec force_spec = srcspec;
    force_spec.deep = true;
    force_spec.set_format (TypeDesc::FLOAT);
    force_spec.channelformats.clear();
    for (int c = 0; c < nc; ++c) {
        if (force_spec.channelnames[c] == "Z")
            force_spec.z_channel = c;
        else if (force_spec.channelnames[c] == "Zback")
            zback_channel = c;
    }
    bool add_z_channel = (force_spec.z_channel < 0);
    if (add_z_channel) {
        // No z channel? Make one.
        force_spec.z_channel = force_spec.nchannels++;
        force_spec.channelnames.emplace_back("Z");
    }

    if (! IBAprep (roi, &dst, &src, NULL, &force_spec,
                   IBAprep_SUPPORT_DEEP | IBAprep_DEEP_MIXED))
        return false;
    if (! dst.deep()) {
        dst.error ("Cannot deepen to a flat image");
        return false;
    }

    float *pixel = OIIO_ALLOCA (float, nc);

    // First, figure out which pixels get a sample and which do not
    for (int z = roi.zbegin; z < roi.zend; ++z)
    for (int y = roi.ybegin; y < roi.yend; ++y)
    for (int x = roi.xbegin; x < roi.xend; ++x) {
        bool has_sample = false;
        src.getpixel (x, y, z, pixel);
        for (int c = 0; c < nc; ++c)
            if (c != force_spec.z_channel && c != zback_channel
                  && pixel[c] != 0.0f) {
                has_sample = true;
                break;
            }
        if (! has_sample && ! add_z_channel)
            for (int c = 0; c < nc; ++c)
                if ((c == force_spec.z_channel || c == zback_channel)
                    && (pixel[c] != 0.0f && pixel[c] < 1e30)) {
                    has_sample = true;
                    break;
                }
        if (has_sample)
            dst.set_deep_samples (x, y, z, 1);
    }

    // Now actually set the values
    for (int z = roi.zbegin; z < roi.zend; ++z)
    for (int y = roi.ybegin; y < roi.yend; ++y)
    for (int x = roi.xbegin; x < roi.xend; ++x) {
        if (dst.deep_samples (x, y, z) == 0)
            continue;
        for (int c = 0; c < nc; ++c)
            dst.set_deep_value (x, y, z, c, 0 /*sample*/,
                                src.getchannel (x, y, z, c));
        if (add_z_channel)
            dst.set_deep_value (x, y, z, nc, 0, zvalue);
    }

    bool ok = true;
    // FIXME -- the above doesn't split into threads. Someday, it should
    // be refactored like this:
    // OIIO_DISPATCH_COMMON_TYPES2 (ok, "deepen", deepen_,
    //                              dst.spec().format, srcspec.format,
    //                              dst, src, add_z_channel, z, roi, nthreads);
    return ok;
}



ImageBuf
ImageBufAlgo::deepen (const ImageBuf &src, float zvalue,
                      ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = deepen (result, src, zvalue, roi, nthreads);
    if (!ok && !result.has_error())
        result.error ("ImageBufAlgo::deepen error");
    return result;
}



bool
ImageBufAlgo::deep_merge (ImageBuf &dst, const ImageBuf &A,
                          const ImageBuf &B, bool occlusion_cull,
                          ROI roi, int nthreads)
{
    pvt::LoggedTimer logtime("IBA::deep_merge");
    if (! A.deep() || ! B.deep()) {
        // For some reason, we were asked to merge a flat image.
        dst.error ("deep_merge can only be performed on deep images");
        return false;
    }
    if (! IBAprep (roi, &dst, &A, &B, NULL,
                   IBAprep_SUPPORT_DEEP | IBAprep_REQUIRE_MATCHING_CHANNELS))
        return false;
    if (! dst.deep()) {
        dst.error ("Cannot deep_merge to a flat image");
        return false;
    }

    // First, set the capacity of the dst image to reserve enough space for
    // the segments of both source images, including any splits that may
    // occur.
    DeepData &dstdd (*dst.deepdata());
    const DeepData &Add (*A.deepdata());
    const DeepData &Bdd (*B.deepdata());
    int Azchan = Add.Z_channel();
    int Azbackchan = Add.Zback_channel();
    int Bzchan = Bdd.Z_channel();
    int Bzbackchan = Bdd.Zback_channel();
    for (int z = roi.zbegin; z < roi.zend; ++z)
    for (int y = roi.ybegin; y < roi.yend; ++y)
    for (int x = roi.xbegin; x < roi.xend; ++x) {
        int dstpixel = dst.pixelindex (x, y, z, true);
        int Apixel = A.pixelindex (x, y, z, true);
        int Bpixel = B.pixelindex (x, y, z, true);
        int Asamps = Add.samples(Apixel);
        int Bsamps = Bdd.samples(Bpixel);
        int nsplits = 0;
        int self_overlap_splits = 0;
        for (int s = 0; s < Asamps; ++s) {
            float src_z = Add.deep_value (Apixel, Azchan, s);
            float src_zback = Add.deep_value (Apixel, Azbackchan, s);
            for (int d = 0; d < Bsamps; ++d) {
                float dst_z = Bdd.deep_value (Bpixel, Bzchan, d);
                float dst_zback = Bdd.deep_value (Bpixel, Bzbackchan, d);
                if (src_z > dst_z && src_z < dst_zback)
                    ++nsplits;
                if (src_zback > dst_z && src_zback < dst_zback)
                    ++nsplits;
                if (dst_z > src_z && dst_z < src_zback)
                    ++nsplits;
                if (dst_zback > src_z && dst_zback < src_zback)
                    ++nsplits;
            }
            // Check for splits src vs src -- in case they overlap!
            for (int ss = s; ss < Asamps; ++ss) {
                float src_z2 = Add.deep_value (Apixel, Azchan, ss);
                float src_zback2 = Add.deep_value (Apixel, Azbackchan, ss);
                if (src_z2 > src_z && src_z2 < src_zback)
                    ++self_overlap_splits;
                if (src_zback2 > src_z && src_zback2 < src_zback)
                    ++self_overlap_splits;
                if (src_z > src_z2 && src_z < src_zback2)
                    ++self_overlap_splits;
                if (src_zback > src_z2 && src_zback < src_zback2)
                    ++self_overlap_splits;
            }
        }
        // Check for splits dst vs dst -- in case they overlap!
        for (int d = 0; d < Bsamps; ++d) {
            float dst_z = Bdd.deep_value (Bpixel, Bzchan, d);
            float dst_zback = Bdd.deep_value (Bpixel, Bzbackchan, d);
            for (int dd = d; dd < Bsamps; ++dd) {
                float dst_z2 = Bdd.deep_value (Bpixel, Bzchan, dd);
                float dst_zback2 = Bdd.deep_value (Bpixel, Bzbackchan, dd);
                if (dst_z2 > dst_z && dst_z2 < dst_zback)
                    ++self_overlap_splits;
                if (dst_zback2 > dst_z && dst_zback2 < dst_zback)
                    ++self_overlap_splits;
                if (dst_z > dst_z2 && dst_z < dst_zback2)
                    ++self_overlap_splits;
                if (dst_zback > dst_z2 && dst_zback < dst_zback2)
                    ++self_overlap_splits;
            }
        }

        dstdd.set_capacity (dstpixel, Asamps+Bsamps+nsplits+self_overlap_splits);
    }

    bool ok = ImageBufAlgo::copy (dst, A, TypeDesc::UNKNOWN, roi, nthreads);

    for (int z = roi.zbegin; z < roi.zend; ++z)
    for (int y = roi.ybegin; y < roi.yend; ++y)
    for (int x = roi.xbegin; x < roi.xend; ++x) {
        int dstpixel = dst.pixelindex (x, y, z, true);
        int Bpixel = B.pixelindex (x, y, z, true);
        DASSERT (dstpixel >= 0);
        // OIIO_UNUSED_OK int oldcap = dstdd.capacity (dstpixel);
        dstdd.merge_deep_pixels (dstpixel, Bdd, Bpixel);
        // DASSERT (oldcap == dstdd.capacity(dstpixel) &&
        //          "Broken: we did not preallocate enough capacity");
        if (occlusion_cull)
            dstdd.occlusion_cull (dstpixel);
    }
    return ok;
}



ImageBuf
ImageBufAlgo::deep_merge (const ImageBuf &A, const ImageBuf &B,
                          bool occlusion_cull, ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = deep_merge (result, A, B, occlusion_cull, roi, nthreads);
    if (!ok && !result.has_error())
        result.error ("ImageBufAlgo::deep_merge error");
    return result;
}



bool
ImageBufAlgo::deep_holdout (ImageBuf &dst, const ImageBuf &src,
                            const ImageBuf &holdout,
                            ROI roi, int nthreads)
{
    pvt::LoggedTimer logtime("IBA::deep_holdout");
    if (! src.deep() || ! holdout.deep()) {
        dst.error ("deep_holdout can only be performed on deep images");
        return false;
    }
    if (! IBAprep (roi, &dst, &src, &holdout, NULL, &src.spec(),
                   IBAprep_SUPPORT_DEEP))
        return false;
    if (! dst.deep()) {
        dst.error ("Cannot deep_holdout into a flat image");
        return false;
    }

    DeepData &dstdd (*dst.deepdata());
    // First, reserve enough space in dst, to reduce the number of
    // allocations we'll do later.
    {
        ImageBuf::ConstIterator<float> s (src, roi);
        for (ImageBuf::Iterator<float> r (dst, roi); !r.done(); ++r, ++s) {
            if (r.exists() && s.exists()) {
                int dstpixel = dst.pixelindex (r.x(), r.y(), r.z(), true);
                dstdd.set_capacity (dstpixel, s.deep_samples());
            }
        }
    }

    // Now we compute each pixel
    OIIO_UNUSED_OK
    int dst_ARchan = dstdd.AR_channel();
    OIIO_UNUSED_OK
    int dst_AGchan = dstdd.AG_channel();
    OIIO_UNUSED_OK
    int dst_ABchan = dstdd.AB_channel();
    int dst_Zchan = dstdd.Z_channel();
    int dst_ZBackchan = dstdd.Zback_channel();
    const DeepData &srcdd (*src.deepdata());
    const DeepData &holdoutdd (*holdout.deepdata());
    int holdout_Zchan = holdoutdd.Z_channel();
    int holdout_ARchan = holdoutdd.AR_channel();
    int holdout_AGchan = holdoutdd.AG_channel();
    int holdout_ABchan = holdoutdd.AB_channel();

    // Figure out which chans need adjustment. Exclude non-color chans
    bool *adjustchan = OIIO_ALLOCA (bool, dstdd.channels());
    for (int c = 0; c < dstdd.channels(); ++c) {
        adjustchan[c] = (c != dst_Zchan && c != dst_ZBackchan &&
                         dstdd.channeltype(c) != TypeDesc::UINT32);
    }

    // Because we want to split holdout against dst, we need a temporary
    // holdout pixel. Make a deepdata that's one pixel big for this purpose.
    DeepData holdouttmp;
    holdouttmp.init (1, holdoutdd.channels(), holdoutdd.all_channeltypes(),
                     holdoutdd.all_channelnames());

    for (ImageBuf::Iterator<float> r (dst, roi);  !r.done();  ++r) {
        // Start by copying src pixel to result. If there's no src
        // samples, we're done.
        int x = r.x(), y = r.y(), z = r.z();
        int srcpixel = src.pixelindex (x, y, z, true);
        int dstpixel = dst.pixelindex (x, y, z, true);
        if (srcpixel < 0 || dstpixel < 0 || ! srcdd.samples(srcpixel))
            continue;
        dstdd.copy_deep_pixel (dstpixel, srcdd, srcpixel);
        dstdd.sort (dstpixel);
        // FIXME - fully tidy it here
        bool debug = (dstpixel == 3);

        // Copy the holdout image pixel into our scratch space. If there
        // are no samples in the holdout image, we're done.
        holdouttmp.copy_deep_pixel (0, holdoutdd, holdout.pixelindex (x, y, z, true));
        if (holdouttmp.samples(0) == 0)
            continue;
        // ASSERT (holdouttmp.is_tidy(0));
        holdouttmp.sort (0);

        // Now walk the lists and adjust opacities
        int holdoutsamps = holdouttmp.samples(0);
        int dstsamples = dstdd.samples (dstpixel);

        float holdout_opacity = 0.0f;  // accumulated holdout opacity
        float result_opacity = 0.0f;    // accumulated result opacity (without holdout)
        float adjusted_cum_opacity = 0.0f;
        float last_adjusted_cum_opacity = 0.0f;
        bool kill = false;
        bool reset_next_holdout = false;
        bool kill_holdouts = false;

        // printf ("Walking...\n");
        for (int d = 0, h = 0; d < dstsamples; ) {
            if (debug) printf ("d=%d h=%d\n", d, h);
            // d and h are the sample numbers of the next sample to consider
            // for the dst and holdout, respectively.

            // If we've passed full holdout, subsequent intput samples are
            // hidden.
            if (holdout_opacity >= 0.9999f) {
                dstdd.erase_samples (dstpixel, d, dstsamples-d);
                // printf ("    Fully held out, removing samples after %d\n", d);
                break;
            }
            // If we've passed full opacity, subsequent holdout samples are
            // irrelevant.
            if (kill || result_opacity >= 0.9999f) {
                dstdd.erase_samples (dstpixel, d, dstsamples-d);
                // printf ("    Hit opaque, removing samples after %d\n", d);
                break;
            }

            float dz = dstdd.deep_value (dstpixel, dst_Zchan, d);
            float hz = h < holdoutsamps ? holdouttmp.deep_value (0, holdout_Zchan, h) : 1e38;

            // If there's a holdout sample in front, adjust the accumulated
            // holdout opacity and advance the holdout sample.
            if (h < holdoutsamps && hz <= dz) {
                if (kill_holdouts) {
                    ++h;
                    continue;
                }
                if (reset_next_holdout) {
                    holdout_opacity = 0.0f;
                    reset_next_holdout = false;
                }
                float alpha = (holdouttmp.deep_value (0, holdout_ARchan, h) +
                               holdouttmp.deep_value (0, holdout_AGchan, h) +
                               holdouttmp.deep_value (0, holdout_ABchan, h)) / 3.0f;
                holdout_opacity += (1.0f-holdout_opacity) * alpha;
                if (debug)
                printf ("    holdout in front alpha=%g, cum holdout opacity is %g\n", alpha, holdout_opacity);
                ++h;
                continue;
            }

            // If we have no more holdout samples, or if the next holdout
            // sample is behind the next dst sample, adjust the dest sample
            // values by the accumulated holdout alpha, and move to the next
            // one.
            float alpha = (dstdd.deep_value (dstpixel, dst_ARchan, d) +
                           dstdd.deep_value (dstpixel, dst_AGchan, d) +
                           dstdd.deep_value (dstpixel, dst_ABchan, d)) / 3.0f;
            OIIO_UNUSED_OK float last_result_opacity = result_opacity;
            result_opacity += (1.0f-result_opacity) * alpha;
            if (alpha > 0.9999f)
                kill = true;
            if (debug) printf ("    source in front alpha=%g, cum holdout opacity is %g\n", alpha, holdout_opacity);

#if 1
            // Try another way of looking at it

#if 1
    // We want the new cumulative opacity to be (1-holdout_opacity) * old cum opacity
    //
    // cumA[i] = cumA[i-1] + (1-cumA[i-1])*A[i]
    // A[i] = (cumA[i] - cumA[i-1]) / (1-cumA[i-1])
    //
    // cumA'[i] = (1-holdout_opacity) * ( cumA[i-1] + (1-cumA[i-1])*A[i] )
    // A'[i] = (cumA'[i] - cumA'[i-1]) / (1-cumA'[i-1])

    // This strategy appears to match Nuke on the pig image. But fails
    // my simple unit test. So I have no idea.
            adjusted_cum_opacity = (1.0f - holdout_opacity) * (last_result_opacity + (1.0f-last_result_opacity)*alpha);
            if (debug) printf ("    adjusted_cum_opacity = %g\n", adjusted_cum_opacity);
            OIIO_UNUSED_OK float adjusted_alpha = OIIO::clamp ((adjusted_cum_opacity - last_adjusted_cum_opacity) / (1.f - last_adjusted_cum_opacity), 0.0f, 1.0f);
            if (debug) printf ("    last_adjusted_cum_opacity = %g\n", last_adjusted_cum_opacity);
            if (debug) printf ("    adjusted_lca - last_adjusted_cum_opacity = %f\n", (adjusted_cum_opacity - last_adjusted_cum_opacity));
#else
    // 
    // or?
    // cumA'[i] = (1-holdout_opacity) * cumA[i]
    //          = cumA'[i-1] + (1-cumA'[i-1])*A'[i]
    // so
    // A'[i] = (cumA'[i] - cumA'[i-1]) / (1-cumA'[i-1])
    // A'[i] = ((1-holdout_opacity) * cumA[i]) - cumA'[i-1]) / (1-cumA'[i-1])
            float adjusted_alpha = (((1.0f-holdout_opacity) * result_opacity) - last_adjusted_cum_opacity) / (1.0f-last_adjusted_cum_opacity);
            // last_result_opacity = result_opacity;
#endif
            float ascale = adjusted_alpha / alpha;
            if (debug) printf ("    adjusted_alpha = %g\n", adjusted_alpha);
            last_adjusted_cum_opacity = adjusted_cum_opacity;
            if (debug) printf ("    ascale = %g\n", ascale);

#endif

            for (int c = 0, nc = dstdd.channels(); c < nc; ++c) {
                if (adjustchan[c]) {
                    float v = dstdd.deep_value (dstpixel, c, d);
                    dstdd.set_deep_value (dstpixel, c, d, v*ascale);
                }
            }

            ++d;
        }
    }
    return true;
}



ImageBuf
ImageBufAlgo::deep_holdout (const ImageBuf &src, const ImageBuf &holdout,
                            ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = deep_holdout (result, src, holdout, roi, nthreads);
    if (!ok && !result.has_error())
        result.error ("ImageBufAlgo::deep_holdout error");
    return result;
}



bool
ImageBufAlgo::deep_cull (ImageBuf &dst, const ImageBuf &src,
                         const ImageBuf &holdout, ROI roi, int nthreads)
{
    if (! src.deep() || ! holdout.deep()) {
        dst.error ("deep_cull can only be performed on deep images");
        return false;
    }
    if (! IBAprep (roi, &dst, &src, &holdout, NULL, &src.spec(),
                   IBAprep_SUPPORT_DEEP))
        return false;
    if (! dst.deep()) {
        dst.error ("Cannot deep_cull into a flat image");
        return false;
    }

    DeepData &dstdd (*dst.deepdata());
    const DeepData &srcdd (*src.deepdata());
    // First, reserve enough space in dst, to reduce the number of
    // allocations we'll do later.
    {
        ImageBuf::ConstIterator<float> s (src, roi);
        for (ImageBuf::Iterator<float> r (dst, roi); !r.done(); ++r, ++s) {
            if (r.exists() && s.exists()) {
                int dstpixel = dst.pixelindex (r.x(), r.y(), r.z(), true);
                dstdd.set_capacity (dstpixel, s.deep_samples());
            }
        }
    }
    // Now we compute each pixel: We copy the src pixel to dst, then split
    // any samples that span the holdout's opaque depth threshold, and then
    // delete any samples that lie beyond the threshold.
    const DeepData &holdoutdd (*holdout.deepdata());
    for (ImageBuf::Iterator<float> r (dst, roi);  !r.done();  ++r) {
        if (!r.exists())
            continue;
        int x = r.x(), y = r.y(), z = r.z();
        int srcpixel = src.pixelindex (x, y, z, true);
        int dstpixel = dst.pixelindex (x, y, z, true);
        if (srcpixel < 0 || srcdd.samples(srcpixel) == 0)
            continue;
        dstdd.copy_deep_pixel (dstpixel, srcdd, srcpixel);
        int holdoutpixel = holdout.pixelindex (x, y, z, true);
        if (holdoutpixel < 0)
            continue;
        float zholdout = holdoutdd.opaque_z (holdoutpixel);
        // Eliminate the samples that are entirely beyond the depth
        // threshold. Do this before the split; that makes it less likely
        // that the split will force a re-allocation.
        dstdd.cull_behind (dstpixel, zholdout);
        // Now split any samples that straddle the z, and do another discard
        // if the split really occurred.
        if (dstdd.split (dstpixel, zholdout))
            dstdd.cull_behind (dstpixel, zholdout);
    }
    return true;
}



ImageBuf
ImageBufAlgo::deep_cull (const ImageBuf &src, const ImageBuf &holdout,
                         ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = deep_cull (result, src, holdout, roi, nthreads);
    if (!ok && !result.has_error())
        result.error ("ImageBufAlgo::deep_cull error");
    return result;
}


OIIO_NAMESPACE_END

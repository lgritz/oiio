/*
  Copyright 2016 Larry Gritz and the other authors and contributors.
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


//
// Task: take "images" A and B, and compute R = A*A + B.
//
// Do this a whole bunch of different ways and benchmark.
//


#include <iostream>

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagebufalgo_util.h>
#include <OpenImageIO/thread.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/sysutil.h>
#include <OpenImageIO/timer.h>
#include <OpenImageIO/argparse.h>
#include <OpenImageIO/simd.h>
#include <OpenImageIO/unittest.h>

#if USE_BOOST_COMPUTE
# include <boost/compute.hpp>
#endif

OIIO_NAMESPACE_USING;

static int iterations = 100;
static int numthreads = Sysutil::physical_concurrency();
static int ntrials = 5;
static bool verbose = false;
static bool wedge = false;
static bool allgpus = false;

static spin_mutex print_mutex;  // make the prints not clobber each other

static int xres = 1920, yres = 1080, channels = 3;
static int npixels = xres * yres;
static int size = npixels * channels;
static ImageBuf imgA, imgB, imgR;



static void
test_arrays (ROI roi)
{
    const float *a = (const float *)imgA.localpixels(); ASSERT(a);
    const float *b = (const float *)imgB.localpixels(); ASSERT(b);
    float *r = (float *)imgR.localpixels(); ASSERT(r);
    for (int x = 0; x < size; ++x)
        r[x] = a[x] * a[x] + b[x];
}



static void
test_arrays_like_image (ROI roi)
{
    const float *a = (const float *)imgA.localpixels(); ASSERT(a);
    const float *b = (const float *)imgB.localpixels(); ASSERT(b);
    float *r = (float *)imgR.localpixels(); ASSERT(r);
    int nchannels = imgA.nchannels();
    for (int y = 0; y < yres; ++y) {
        for (int x = 0; x < xres; ++x) {
            int i = (y*xres + x) * nchannels;
            for (int c = 0; c < nchannels; ++c)
                r[i+c] = a[i+c] * a[i+c] + b[i+c];
        }
    }
}


static void
test_arrays_like_image_multithread (ROI roi)
{
    const float *a = (const float *)imgA.localpixels(); ASSERT(a);
    const float *b = (const float *)imgB.localpixels(); ASSERT(b);
    float *r = (float *)imgR.localpixels(); ASSERT(r);
    int nchannels = imgA.nchannels();
    for (int y = roi.ybegin; y < roi.yend; ++y) {
        for (int x = roi.xbegin; x < roi.xend; ++x) {
            int i = (y*xres + x) * nchannels;
            for (int c = 0; c < nchannels; ++c)
                r[i+c] = a[i+c] * a[i+c] + b[i+c];
        }
    }
}

static void
test_arrays_like_image_multithread_wrapper (ROI roi)
{
    ImageBufAlgo::parallel_image (test_arrays_like_image_multithread, roi, numthreads);
}



static void
test_arrays_simd4 (ROI roi)
{
    const float *a = (const float *)imgA.localpixels(); ASSERT(a);
    const float *b = (const float *)imgB.localpixels(); ASSERT(b);
    float *r = (float *)imgR.localpixels(); ASSERT(r);
    int x, end4 = size - (size&3);
    for (x = 0; x < end4; x += 4, a += 4, b += 4, r += 4) {
        simd::float4 a_simd(a), b_simd(b);
        *(simd::float4 *)r = a_simd * a_simd + b_simd;
    }
    for ( ; x < size; ++x, ++a, ++b, ++r) {
        *r = a[0]*a[0] + b[0];
    }
}



static void
test_arrays_like_image_simd (ROI roi)
{
    const float *a = (const float *)imgA.localpixels(); ASSERT(a);
    const float *b = (const float *)imgB.localpixels(); ASSERT(b);
    float *r = (float *)imgR.localpixels(); ASSERT(r);
    int nchannels = imgA.nchannels();
    for (int y = 0; y < yres; ++y) {
        for (int x = 0; x < xres; ++x) {
            int i = (y*xres + x) * nchannels;
            simd::float4 a_simd, b_simd, r_simd;
            a_simd.load (a+i, 3);
            b_simd.load (b+i, 3);
            r_simd = a_simd * a_simd + b_simd;
            r_simd.store (r+i, 3);
        }
    }
}


static void
test_arrays_like_image_simd_multithread (ROI roi)
{
    const float *a = (const float *)imgA.localpixels(); ASSERT(a);
    const float *b = (const float *)imgB.localpixels(); ASSERT(b);
    float *r = (float *)imgR.localpixels(); ASSERT(r);
    int nchannels = imgA.nchannels();
    for (int y = roi.ybegin; y < roi.yend; ++y) {
        for (int x = roi.xbegin; x < roi.xend; ++x) {
            int i = (y*xres + x) * nchannels;
            simd::float4 a_simd, b_simd, r_simd;
            a_simd.load (a+i, 3);
            b_simd.load (b+i, 3);
            r_simd = a_simd * a_simd + b_simd;
            r_simd.store (r+i, 3);
        }
    }
}


static void
test_arrays_like_image_simd_multithread_wrapper (ROI roi)
{
    ImageBufAlgo::parallel_image (test_arrays_like_image_simd_multithread, roi, 0);
}



static void
test_IBA (ROI roi, int threads)
{
    ImageBufAlgo::mad (imgR, imgA, imgA, imgB, roi, threads);
}



#if USE_BOOST_COMPUTE
namespace compute = boost::compute;
compute::device compute_device = compute::system::default_device();
compute::context compute_ctx;
compute::command_queue compute_queue;


static void
print_compute_info (compute::device device)
{
    std::cout << "  device: \"" << device.name() << "\"\n";
    std::cout << "    vendor \"" << device.vendor() << "\"";
    std::cout << ", profile \"" << device.profile() << "\"\n";
    std::cout << "    version \"" << device.version() << "\"";
    std::cout << ", driver_version \"" << device.driver_version() << "\"\n";
    std::cout << "    memory:  global "
              << Strutil::memformat (device.get_info<cl_ulong>(CL_DEVICE_GLOBAL_MEM_SIZE));
    std::cout << ", local "
              << Strutil::memformat (device.get_info<cl_ulong>(CL_DEVICE_LOCAL_MEM_SIZE));
    std::cout << ", constant "
              << Strutil::memformat (device.get_info<cl_ulong>(CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE));
    std::cout << "\n";
    std::cout << "    " << device.compute_units() << " compute units, "
              << "clock freq = " << device.clock_frequency() << ", ";
    std::cout << "float vec width = " << device.preferred_vector_width<float>() << "\n";
    std::cout << "  extensions: " << Strutil::join(device.extensions(), ", ") << "\n";
}



static void
test_boost_compute_with_copy (ROI roi)
{
    namespace compute = boost::compute;
    const float *a = (const float *)imgA.localpixels(); ASSERT(a);
    const float *b = (const float *)imgB.localpixels(); ASSERT(b);
    float *r = (float *)imgR.localpixels(); ASSERT(r);
    compute::vector<float> d_a (size, compute_ctx);
    compute::vector<float> d_b (size, compute_ctx);
    compute::vector<float> d_r (size, compute_ctx);
    compute::copy (a, a+size, d_a.begin(), compute_queue);
    compute::copy (b, b+size, d_b.begin(), compute_queue);
    compute::transform (d_a.begin(), d_a.end(), d_a.begin(), d_r.begin(),
                        compute::multiplies<float>(), compute_queue);
    compute::transform (d_r.begin(), d_r.end(), d_b.begin(), d_r.begin(),
                        compute::plus<float>(), compute_queue);
    compute::copy (d_r.begin(), d_r.end(), r, compute_queue);
    compute_queue.finish();
}


static void
test_boost_compute_mapped (ROI roi)
{
    namespace compute = boost::compute;
    const float *a = (const float *)imgA.localpixels(); ASSERT(a);
    const float *b = (const float *)imgB.localpixels(); ASSERT(b);
    float *r = (float *)imgR.localpixels(); ASSERT(r);
    compute::mapped_view<float> d_a (a, size, compute_ctx);
    compute::mapped_view<float> d_b (b, size, compute_ctx);
    compute::mapped_view<float> d_r (r, size, compute_ctx);
    compute::transform (d_a.begin(), d_a.end(), d_a.begin(), d_r.begin(),
                        compute::multiplies<float>(), compute_queue);
    compute::transform (d_r.begin(), d_r.end(), d_b.begin(), d_r.begin(),
                        compute::plus<float>(), compute_queue);
    compute_queue.finish();
}


static void
test_boost_compute_math_only (compute::vector<float> &d_a,
                              compute::vector<float> &d_b,
                              compute::vector<float> &d_r,
                              ROI roi)
{
    namespace compute = boost::compute;
    // const float *a = (const float *)imgA.localpixels(); ASSERT(a);
    // const float *b = (const float *)imgB.localpixels(); ASSERT(b);
    // float *r = (float *)imgR.localpixels(); ASSERT(r);
    // compute::vector<float> d_a (size, compute_ctx);
    // compute::vector<float> d_b (size, compute_ctx);
    // compute::vector<float> d_r (size, compute_ctx);
    // compute::copy (a, a+size, d_a.begin(), compute_queue);
    // compute::copy (b, b+size, d_b.begin(), compute_queue);
    compute::transform (d_a.begin(), d_a.end(), d_a.begin(), d_r.begin(),
                        compute::multiplies<float>(), compute_queue);
    compute::transform (d_r.begin(), d_r.end(), d_b.begin(), d_r.begin(),
                        compute::plus<float>(), compute_queue);
    // compute::copy (d_r.begin(), d_r.end(), r, compute_queue);
    compute_queue.finish();
}


#endif



void
test_compute ()
{
    double time;

    ROI roi (0, xres, 0, yres, 0, 1, 0, channels);

    std::cout << "Test straightforward as 1D array of float: ";
    ImageBufAlgo::zero (imgR);
    time = time_trial (OIIO::bind (test_arrays, roi), ntrials, iterations) / iterations;
    std::cout << Strutil::format ("%.1f Mvals/sec", (size/1.0e6)/time) << std::endl;
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,0), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,1), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,2), 0.50, 0.001);
    // imgR.write ("ref.exr");

    std::cout << "Test array iterated like an image: ";
    ImageBufAlgo::zero (imgR);
    time = time_trial (OIIO::bind (test_arrays_like_image, roi), ntrials, iterations) / iterations;
    std::cout << Strutil::format ("%.1f Mvals/sec", (size/1.0e6)/time) << std::endl;
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,0), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,1), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,2), 0.50, 0.001);

    std::cout << "Test array iterated like an image, multithreaded: ";
    ImageBufAlgo::zero (imgR);
    time = time_trial (OIIO::bind (test_arrays_like_image_multithread_wrapper, roi), ntrials, iterations) / iterations;
    std::cout << Strutil::format ("%.1f Mvals/sec", (size/1.0e6)/time) << std::endl;
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,0), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,1), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,2), 0.50, 0.001);

    std::cout << "Test array as 1D, using SIMD: ";
    ImageBufAlgo::zero (imgR);
    time = time_trial (OIIO::bind (test_arrays_simd4, roi), ntrials, iterations) / iterations;
    std::cout << Strutil::format ("%.1f Mvals/sec", (size/1.0e6)/time) << std::endl;
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,0), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,1), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,2), 0.50, 0.001);

    std::cout << "Test array iterated like an image, using SIMD: ";
    ImageBufAlgo::zero (imgR);
    time = time_trial (OIIO::bind (test_arrays_like_image_simd, roi), ntrials, iterations) / iterations;
    std::cout << Strutil::format ("%.1f Mvals/sec", (size/1.0e6)/time) << std::endl;
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,0), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,1), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,2), 0.50, 0.001);

    std::cout << "Test array iterated like an image, using SIMD, multithreaded: ";
    ImageBufAlgo::zero (imgR);
    time = time_trial (OIIO::bind (test_arrays_like_image_simd_multithread_wrapper, roi), ntrials, iterations) / iterations;
    std::cout << Strutil::format ("%.1f Mvals/sec", (size/1.0e6)/time) << std::endl;
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,0), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,1), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,2), 0.50, 0.001);

    std::cout << "Test ImageBufAlgo::mad 1 thread: ";
    ImageBufAlgo::zero (imgR);
    time = time_trial (OIIO::bind (test_IBA, roi, 1),
                       ntrials, iterations) / iterations;
    std::cout << Strutil::format ("%.1f Mvals/sec", (size/1.0e6)/time) << std::endl;
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,0), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,1), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,2), 0.50, 0.001);

    std::cout << "Test ImageBufAlgo::mad multi-thread " << numthreads << ": ";
    ImageBufAlgo::zero (imgR);
    time = time_trial (OIIO::bind (test_IBA, roi, numthreads),
                       ntrials, iterations) / iterations;
    std::cout << Strutil::format ("%.1f Mvals/sec", (size/1.0e6)/time) << std::endl;
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,0), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,1), 0.25, 0.001);
    OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,2), 0.50, 0.001);

#if USE_BOOST_COMPUTE
    std::cout << "OpenCL via Boost.Compute info: \n";
    std::cout << "Platforms:";
    for (size_t i = 0; i < compute::system::platform_count(); ++i)
        std::cout << " " << compute::system::platforms()[i].name();
    std::cout << "\n";
    for (size_t d = 0; d < compute::system().device_count(); ++d) {
        compute_device = compute::system::devices()[d];
        if (! allgpus && compute_device != compute::system::default_device())
            continue;
        compute_ctx = compute::context(compute_device);
        compute_queue = compute::command_queue (compute_ctx, compute_device);
        std::cout << "Device " << d << " " << compute_device.name();
        if (compute_device == compute::system::default_device())
            std::cout << " (DEFAULT)";
        std::cout << "\n";
        print_compute_info (compute_device);
        compute::vector<float> d_a (size, compute_ctx);
        compute::vector<float> d_b (size, compute_ctx);
        compute::vector<float> d_r (size, compute_ctx);
        compute::copy ((const float *)imgA.localpixels(),
                       (const float *)imgA.localpixels()+size, d_a.begin(),
                       compute_queue);
        compute::copy ((const float *)imgB.localpixels(),
                       (const float *)imgB.localpixels()+size, d_b.begin(),
                       compute_queue);

        std::cout << "    Test OpenCL via Boost.Compute, with copy in/out: ";
        ImageBufAlgo::zero (imgR);
        time = time_trial (OIIO::bind (test_boost_compute_with_copy, roi),
                           ntrials, iterations) / iterations;
        std::cout << Strutil::format ("%.1f Mvals/sec", (size/1.0e6)/time) << std::endl;
        OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,0), 0.25, 0.001);
        OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,1), 0.25, 0.001);
        OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,2), 0.50, 0.001);

        std::cout << "    Test OpenCL via Boost.Compute, with mapped view: ";
        ImageBufAlgo::zero (imgR);
        time = time_trial (OIIO::bind (test_boost_compute_mapped, roi),
                                       ntrials, iterations) / iterations;
        std::cout << Strutil::format ("%.1f Mvals/sec", (size/1.0e6)/time) << std::endl;
        OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,0), 0.25, 0.001);
        OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,1), 0.25, 0.001);
        OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,2), 0.50, 0.001);

        std::cout << "    Test OpenCL via Boost.Compute, math only: ";
        ImageBufAlgo::zero (imgR);
        time = time_trial (OIIO::bind (test_boost_compute_math_only,
                                       OIIO::ref(d_a), OIIO::ref(d_b),
                                       OIIO::ref(d_r), roi),
                           ntrials, iterations) / iterations;
        std::cout << Strutil::format ("%.1f Mvals/sec", (size/1.0e6)/time) << std::endl;
        // OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,0), 0.25, 0.001);
        // OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,1), 0.25, 0.001);
        // OIIO_CHECK_EQUAL_THRESH (imgR.getchannel(xres/2,yres/2,0,2), 0.50, 0.001);
    }
#endif
}



static void
getargs (int argc, char *argv[])
{
    bool help = false;
    ArgParse ap;
    ap.options ("compute_test\n"
                OIIO_INTRO_STRING "\n"
                "Usage:  compute_test [options]",
                // "%*", parse_files, "",
                "--help", &help, "Print help message",
                "-v", &verbose, "Verbose mode",
                "--threads %d", &numthreads,
                    ustring::format("Number of threads (default: %d)", numthreads).c_str(),
                "--iterations %d", &iterations,
                    ustring::format("Number of iterations (default: %d)", iterations).c_str(),
                "--trials %d", &ntrials, "Number of trials",
                "--allgpus", &allgpus, "Run OpenCL tests on all devices, not just default",
                "--wedge", &wedge, "Do a wedge test",
                NULL);
    if (ap.parse (argc, (const char**)argv) < 0) {
        std::cerr << ap.geterror() << std::endl;
        ap.usage ();
        exit (EXIT_FAILURE);
    }
    if (help) {
        ap.usage ();
        exit (EXIT_FAILURE);
    }
}



int main (int argc, char *argv[])
{
#if !defined(NDEBUG) || defined(OIIO_TRAVIS) || defined(OIIO_CODECOV)
    // For the sake of test time, reduce the default iterations for DEBUG,
    // CI, and code coverage builds. Explicit use of --iters or --trials
    // will override this, since it comes before the getargs() call.
    iterations /= 10;
    ntrials = 1;
#endif

    getargs (argc, argv);

    // Initialize
    imgA.reset (ImageSpec (xres, yres, channels, TypeDesc::FLOAT));
    imgB.reset (ImageSpec (xres, yres, channels, TypeDesc::FLOAT));
    imgR.reset (ImageSpec (xres, yres, channels, TypeDesc::FLOAT));
    float red[3]  = { 1, 0, 0 };
    float green[3] = { 0, 1, 0 };
    float blue[3]  = { 0, 0, 1 };
    float black[3] = { 0, 0, 0 };
    ImageBufAlgo::fill (imgA, red, green, red, green);
    ImageBufAlgo::fill (imgB, blue, blue, black, black);
    // imgA.write ("A.exr");
    // imgB.write ("B.exr");

    test_compute ();

    return unit_test_failures;
}

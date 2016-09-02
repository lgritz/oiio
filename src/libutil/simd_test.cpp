/*
Copyright (c) 2014 Larry Gritz et al.
All Rights Reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of Sony Pictures Imageworks nor the names of its
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
*/


#include <sstream>

#include <OpenEXR/half.h>
#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathMatrix.h>

#include <OpenImageIO/simd.h>
#include <OpenImageIO/unittest.h>
#include <OpenImageIO/typedesc.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/argparse.h>
#include <OpenImageIO/timer.h>
#include <OpenImageIO/ustring.h>
#include <OpenImageIO/fmath.h>



OIIO_NAMESPACE_USING;

using namespace OIIO::simd;


static int iterations = 10;
static int ntrials = 5;
static bool verbose = false;
static size_t benchsize = 1000000;


static void
getargs (int argc, char *argv[])
{
    bool help = false;
    ArgParse ap;
    ap.options ("simd_test\n"
                OIIO_INTRO_STRING "\n"
                "Usage:  simd_test [options]",
                // "%*", parse_files, "",
                "--help", &help, "Print help message",
                "-v", &verbose, "Verbose mode",
                "--iterations %d", &iterations,
                    ustring::format("Number of iterations (default: %d)", iterations).c_str(),
                "--trials %d", &ntrials, "Number of trials",
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




template<typename X, typename Y>
inline void
OIIO_CHECK_SIMD_EQUAL_impl (const X& x, const Y& y,
                            const char *xstr, const char *ystr,
                            const char *file, int line)
{
    if (! all(x == y)) {
        std::cout << __FILE__ << ":" << __LINE__ << ":\n"
                  << "FAILED: " << xstr << " == " << ystr << "\n"
                  << "\tvalues were '" << x << "' and '" << y << "'\n";
        ++unit_test_failures;
    }
}


#define xOIIO_CHECK_SIMD_EQUAL(x,y) \
            OIIO_CHECK_SIMD_EQUAL_impl(x,y,#x,#y,__FILE__,__LINE__)
#define OIIO_CHECK_SIMD_EQUAL(x,y)                                      \
    (all ((x) == (y)) ? ((void)0)                                       \
         : ((std::cout << __FILE__ << ":" << __LINE__ << ":\n"          \
             << "FAILED: " << #x << " == " << #y << "\n"                \
             << "\tvalues were '" << (x) << "' and '" << (y) << "'\n"), \
            (void)++unit_test_failures))


#define OIIO_CHECK_SIMD_EQUAL_THRESH(x,y,eps)                           \
    (all (abs((x)-(y)) < (eps)) ? ((void)0)                             \
         : ((std::cout << __FILE__ << ":" << __LINE__ << ":\n"          \
             << "FAILED: " << #x << " == " << #y << "\n"                \
             << "\tvalues were '" << (x) << "' and '" << (y) << "'\n"), \
            (void)++unit_test_failures))



#if OIIO_CPLUSPLUS_VERSION >= 11  /* So easy with lambdas */

template <typename FUNC, typename T>
void benchmark (string_view funcname, size_t n, FUNC func, T x,
                size_t work=SimdElements<T>::size)
{
    auto repeat_func = [&](){
        // Unroll the loop 8 times
        for (size_t i = 0; i < n; i += work*8) {
            auto r = func(x); DoNotOptimize (r);
            r = func(x); DoNotOptimize (r);
            r = func(x); DoNotOptimize (r);
            r = func(x); DoNotOptimize (r);
            r = func(x); DoNotOptimize (r);
            r = func(x); DoNotOptimize (r);
            r = func(x); DoNotOptimize (r);
            r = func(x); DoNotOptimize (r);
        }
    };
    float time = time_trial (repeat_func, ntrials, iterations) / iterations;
    std::cout << Strutil::format ("  %s: %7.1f Mvals/sec, (%.1f Mcalls/sec)\n",
                                  funcname, (n/1.0e6)/time,
                                  ((n/work)/1.0e6)/time);
}


template <typename FUNC, typename T, typename U>
void benchmark2 (string_view funcname, size_t n, FUNC func, T x, U y,
                 size_t work=SimdElements<T>::size)
{
    auto repeat_func = [&](){
        // Unroll the loop 8 times
        for (size_t i = 0; i < n; i += work*8) {
            auto r = func(x, y); DoNotOptimize (r);
            r = func(x, y); DoNotOptimize (r);
            r = func(x, y); DoNotOptimize (r);
            r = func(x, y); DoNotOptimize (r);
            r = func(x, y); DoNotOptimize (r);
            r = func(x, y); DoNotOptimize (r);
            r = func(x, y); DoNotOptimize (r);
            r = func(x, y); DoNotOptimize (r);
        }
    };
    float time = time_trial (repeat_func, ntrials, iterations) / iterations;
    std::cout << Strutil::format ("  %s: %7.1f Mvals/sec, (%.1f Mcalls/sec)\n",
                                  funcname, (n/1.0e6)/time,
                                  ((n/work)/1.0e6)/time);
}

#else

// No support of lambdas, just skip the benchmarks
#define benchmark(x)
    #define benchmark2(x)

#endif



template<typename VEC>
inline VEC mkvec (typename VEC::value_t a, typename VEC::value_t b,
                  typename VEC::value_t c, typename VEC::value_t d=0,
                  typename VEC::value_t e=0, typename VEC::value_t f=0,
                  typename VEC::value_t g=0, typename VEC::value_t h=0)
{
    return VEC(a,b,c,d,e,f,g,h);
}


template<> inline int4 mkvec (int a, int b, int c, int d,
                              int e, int f, int g, int h) {
    return int4(a,b,c,d);
}

template<> inline float4 mkvec (float a, float b, float c, float d,
                                float e, float f, float g, float h) {
    return float4(a,b,c,d);
}

template<> inline float3 mkvec (float a, float b, float c, float d,
                                float e, float f, float g, float h) {
    return float3(a,b,c);
}



inline Imath::V3f
norm_imath (const Imath::V3f &a) {
    return a.normalized();
}

inline Imath::V3f
norm_imath_simd (float3 a) {
    return a.normalized().V3f();
}

inline Imath::V3f
norm_imath_simd_fast (float3 a) {
    return a.normalized_fast().V3f();
}

inline float3
norm_simd_fast (float3 a) {
    return a.normalized_fast();
}

inline float3
norm_simd (float3 a) {
    return a.normalized();
}


inline Imath::M44f inverse_imath (const Imath::M44f &M)
{
    return M.inverse();
}


inline matrix44 inverse_simd (const matrix44 &M)
{
    return M.inverse();
}



template<typename VEC>
void test_loadstore ()
{
    typedef typename VEC::value_t ELEM;
    std::cout << "test_loadstore " << VEC::type_name() << "\n";
    VEC C1234 = mkvec<VEC>(1, 2, 3, 4, 5, 6, 7, 8);
    // VEC C0 (0);
    ELEM partial[] = { 101, 102, 103, 104, 105, 106, 107, 108 };
    for (int i = 1; i <= VEC::elements; ++i) {
        VEC a (ELEM(0));
        a.load (partial, i);
        for (int j = 0; j < VEC::elements; ++j)
            OIIO_CHECK_EQUAL (a[j], j<i ? partial[j] : ELEM(0));
        std::cout << "  partial load " << i << " : " << a << "\n";
        ELEM stored[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        C1234.store (stored, i);
        for (int j = 0; j < VEC::elements; ++j)
            OIIO_CHECK_EQUAL (stored[j], j<i ? ELEM(j+1) : ELEM(0));
        std::cout << "  partial store " << i << " :";
        for (int c = 0; c < VEC::elements; ++c)
            std::cout << ' ' << stored[c];
        std::cout << std::endl;
    }

    {
    // Check load from integers
    unsigned short us1234[] = {1, 2, 3, 4, 5, 6, 7, 8};
    short s1234[]           = {1, 2, 3, 4, 5, 6, 7, 8};
    unsigned char uc1234[]  = {1, 2, 3, 4, 5, 6, 7, 8};
    char c1234[]            = {1, 2, 3, 4, 5, 6, 7, 8};
    OIIO_CHECK_SIMD_EQUAL (VEC(us1234), C1234);
    OIIO_CHECK_SIMD_EQUAL (VEC( s1234), C1234);
    OIIO_CHECK_SIMD_EQUAL (VEC(uc1234), C1234);
    OIIO_CHECK_SIMD_EQUAL (VEC( c1234), C1234);
    }
}



void
test_int4_to_uint16s ()
{
    int4 i (0xffff0001, 0xffff0002, 0xffff0003, 0xffff0004);
    unsigned short s[4];
    i.store (s);
    OIIO_CHECK_EQUAL (s[0], 1);
    OIIO_CHECK_EQUAL (s[1], 2);
    OIIO_CHECK_EQUAL (s[2], 3);
    OIIO_CHECK_EQUAL (s[3], 4);
}



void
test_int4_to_uint8s ()
{
    int4 i (0xffffff01, 0xffffff02, 0xffffff03, 0xffffff04);
    unsigned char c[4];
    i.store (c);
    OIIO_CHECK_EQUAL (int(c[0]), 1);
    OIIO_CHECK_EQUAL (int(c[1]), 2);
    OIIO_CHECK_EQUAL (int(c[2]), 3);
    OIIO_CHECK_EQUAL (int(c[3]), 4);
}



template<typename VEC>
void test_component_access ()
{
    typedef typename VEC::value_t ELEM;
    std::cout << "test_component_access " << VEC::type_name() << "\n";

    const ELEM vals[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    VEC a = mkvec<VEC>(0, 1, 2, 3, 4, 5, 6, 7);
    for (int i = 0; i < VEC::elements; ++i)
        OIIO_CHECK_EQUAL (a[i], vals[i]);
    OIIO_CHECK_EQUAL (a.x(), 0);
    OIIO_CHECK_EQUAL (a.y(), 1);
    OIIO_CHECK_EQUAL (a.z(), 2);
    if (SimdElements<VEC>::size > 3)
        OIIO_CHECK_EQUAL (a.w(), 3);
    OIIO_CHECK_EQUAL (extract<0>(a), 0);
    OIIO_CHECK_EQUAL (extract<1>(a), 1);
    OIIO_CHECK_EQUAL (extract<2>(a), 2);
    if (SimdElements<VEC>::size > 3)
        OIIO_CHECK_EQUAL (extract<3>(a), 3);
    OIIO_CHECK_SIMD_EQUAL (insert<0>(a, ELEM(42)), mkvec<VEC>(42,1,2,3,4,5,6,7));
    OIIO_CHECK_SIMD_EQUAL (insert<1>(a, ELEM(42)), mkvec<VEC>(0,42,2,3,4,5,6,7));
    OIIO_CHECK_SIMD_EQUAL (insert<2>(a, ELEM(42)), mkvec<VEC>(0,1,42,3,4,5,6,7));
    if (SimdElements<VEC>::size > 3)
        OIIO_CHECK_SIMD_EQUAL (insert<3>(a, ELEM(42)), mkvec<VEC>(0,1,2,42,4,5,6,7));
    VEC t;
    t = a; t.set_x(42); OIIO_CHECK_SIMD_EQUAL (t, mkvec<VEC>(42,1,2,3,4,5,6,7));
    t = a; t.set_y(42); OIIO_CHECK_SIMD_EQUAL (t, mkvec<VEC>(0,42,2,3,4,5,6,7));
    t = a; t.set_z(42); OIIO_CHECK_SIMD_EQUAL (t, mkvec<VEC>(0,1,42,3,4,5,6,7));
    if (SimdElements<VEC>::size > 3) {
        t = a; t.set_w(42); OIIO_CHECK_SIMD_EQUAL (t, mkvec<VEC>(0,1,2,42,4,5,6,7));
    }

    VEC b (vals);
    for (int i = 0; i < VEC::elements; ++i)
        OIIO_CHECK_EQUAL (b[i], vals[i]);
    OIIO_CHECK_EQUAL (extract<0>(b), 0);
    OIIO_CHECK_EQUAL (extract<1>(b), 1);
    OIIO_CHECK_EQUAL (extract<2>(b), 2);
    if (SimdElements<VEC>::size > 3)
        OIIO_CHECK_EQUAL (extract<3>(b), 3);

    benchmark2 ("operator[i]", benchsize,
                [&](const VEC& v, int i){ return v[i]; },  b, 2, 1);
    benchmark2 ("operator[2]", benchsize,
                [&](const VEC& v, int i){ return v[2]; },  b, 2, 1);
    benchmark2 ("extract<2> ", benchsize,
                [&](const VEC& v, int i){ return extract<2>(v); },  b, 2, 1);
    benchmark2 ("insert<2> ", benchsize,
                [&](const VEC& v, ELEM i){ return insert<2>(v, i); }, b, ELEM(1), 1);
}



template<>
void test_component_access<bool4> ()
{
    typedef bool4 VEC;
    typedef VEC::value_t ELEM;
    std::cout << "test_component_access " << VEC::type_name() << "\n";

    VEC a (false, true, true, true);
    OIIO_CHECK_EQUAL (bool(a[0]), false);
    OIIO_CHECK_EQUAL (bool(a[1]), true);
    OIIO_CHECK_EQUAL (bool(a[2]), true);
    OIIO_CHECK_EQUAL (bool(a[3]), true);
    OIIO_CHECK_EQUAL (extract<0>(a), false);
    OIIO_CHECK_EQUAL (extract<1>(a), true);
    OIIO_CHECK_EQUAL (extract<2>(a), true);
    OIIO_CHECK_EQUAL (extract<3>(a), true);
    OIIO_CHECK_SIMD_EQUAL (insert<0>(a, ELEM(true)), VEC(true,true,true,true));
    OIIO_CHECK_SIMD_EQUAL (insert<1>(a, ELEM(false)), VEC(false,false,true,true));
    OIIO_CHECK_SIMD_EQUAL (insert<2>(a, ELEM(false)), VEC(false,true,false,true));
    OIIO_CHECK_SIMD_EQUAL (insert<3>(a, ELEM(false)), VEC(false,true,true,false));
}



template<>
void test_component_access<bool8> ()
{
    typedef bool8 VEC;
    typedef VEC::value_t ELEM;
    std::cout << "test_component_access " << VEC::type_name() << "\n";

    VEC a (false, true, true, true, false, false, true, true);
    OIIO_CHECK_EQUAL (bool(a[0]), false);
    OIIO_CHECK_EQUAL (bool(a[1]), true);
    OIIO_CHECK_EQUAL (bool(a[2]), true);
    OIIO_CHECK_EQUAL (bool(a[3]), true);
    OIIO_CHECK_EQUAL (bool(a[4]), false);
    OIIO_CHECK_EQUAL (bool(a[5]), false);
    OIIO_CHECK_EQUAL (bool(a[6]), true);
    OIIO_CHECK_EQUAL (bool(a[7]), true);
    OIIO_CHECK_EQUAL (extract<0>(a), false);
    OIIO_CHECK_EQUAL (extract<1>(a), true);
    OIIO_CHECK_EQUAL (extract<2>(a), true);
    OIIO_CHECK_EQUAL (extract<3>(a), true);
    OIIO_CHECK_EQUAL (extract<4>(a), false);
    OIIO_CHECK_EQUAL (extract<5>(a), false);
    OIIO_CHECK_EQUAL (extract<6>(a), true);
    OIIO_CHECK_EQUAL (extract<7>(a), true);
    OIIO_CHECK_SIMD_EQUAL (insert<0>(a, ELEM(true)),
                           VEC(true, true, true, true, false, false, true, true));
    OIIO_CHECK_SIMD_EQUAL (insert<1>(a, ELEM(false)),
                           VEC(false, false, true, true, false, false, true, true));
    OIIO_CHECK_SIMD_EQUAL (insert<2>(a, ELEM(false)),
                           VEC(false, true, false, true, false, false, true, true));
    OIIO_CHECK_SIMD_EQUAL (insert<3>(a, ELEM(false)),
                           VEC(false, true, true, false, false, false, true, true));
    OIIO_CHECK_SIMD_EQUAL (insert<4>(a, ELEM(true)),
                           VEC(false, true, true, true, true, false, true, true));
    OIIO_CHECK_SIMD_EQUAL (insert<5>(a, ELEM(true)),
                           VEC(false, true, true, true, false, true, true, true));
    OIIO_CHECK_SIMD_EQUAL (insert<6>(a, ELEM(false)),
                           VEC(false, true, true, true, false, false, false, true));
    OIIO_CHECK_SIMD_EQUAL (insert<7>(a, ELEM(false)),
                           VEC(false, true, true, true, false, false, true, false));
}



template<typename T> inline T do_add (const T &a, const T &b) { return a+b; }
template<typename T> inline T do_sub (const T &a, const T &b) { return a-b; }
template<typename T> inline T do_mul (const T &a, const T &b) { return a*b; }
template<typename T> inline T do_div (const T &a, const T &b) { return a/b; }


template<typename VEC>
void test_arithmetic ()
{
    typedef typename VEC::value_t ELEM;
    std::cout << "test_arithmetic " << VEC::type_name() << "\n";

    VEC a = VEC::Iota (1.0f, 3.0f);
    VEC b = VEC::Iota (1.0f, 1.0f);
    VEC add(ELEM(0)), sub(ELEM(0)), mul(ELEM(0)), div(ELEM(0));
    ELEM bsum(ELEM(0));
    for (int i = 0; i < VEC::elements; ++i) {
        add[i] = a[i] + b[i];
        sub[i] = a[i] - b[i];
        mul[i] = a[i] * b[i];
        div[i] = a[i] / b[i];
        bsum += b[i];
    }
    OIIO_CHECK_SIMD_EQUAL (a+b, add);
    OIIO_CHECK_SIMD_EQUAL (a-b, sub);
    OIIO_CHECK_SIMD_EQUAL (a*b, mul);
    OIIO_CHECK_SIMD_EQUAL (a/b, div);
    OIIO_CHECK_SIMD_EQUAL (a*ELEM(2), a*VEC(ELEM(2)));
    { VEC r = a; r += b; OIIO_CHECK_SIMD_EQUAL (r, add); }
    { VEC r = a; r -= b; OIIO_CHECK_SIMD_EQUAL (r, sub); }
    { VEC r = a; r *= b; OIIO_CHECK_SIMD_EQUAL (r, mul); }
    { VEC r = a; r /= b; OIIO_CHECK_SIMD_EQUAL (r, div); }
    { VEC r = a; r *= ELEM(2); OIIO_CHECK_SIMD_EQUAL (r, a*ELEM(2)); }

    OIIO_CHECK_EQUAL (reduce_add(b), bsum);
    OIIO_CHECK_SIMD_EQUAL (vreduce_add(b), VEC(bsum));
    OIIO_CHECK_EQUAL (reduce_add(VEC(1.0f)), SimdElements<VEC>::size);

    benchmark2 ("operator+", benchsize, do_add<VEC>, a, b);
    benchmark2 ("operator-", benchsize, do_sub<VEC>, a, b);
    benchmark2 ("operator*", benchsize, do_mul<VEC>, a, b);
    benchmark2 ("operator/", benchsize, do_div<VEC>, a, b);
}



#if 0
template<>
void test_arithmetic<float3> ()
{
    typedef float3 VEC;
    typedef typename VEC::value_t ELEM;
    std::cout << "test_arithmetic " << VEC::type_name() << "\n";

    VEC a (10, 11, 12);
    VEC b (1, 2, 3);
    OIIO_CHECK_SIMD_EQUAL (a+b, VEC(11,13,15));
    OIIO_CHECK_SIMD_EQUAL (a-b, VEC(9,9,9));
    OIIO_CHECK_SIMD_EQUAL (a*b, VEC(10,22,36));
    OIIO_CHECK_SIMD_EQUAL (a/b, VEC(a[0]/b[0],a[1]/b[1],a[2]/b[2]));
    OIIO_CHECK_EQUAL (reduce_add(b), ELEM(6));
    OIIO_CHECK_SIMD_EQUAL (vreduce_add(b), VEC(ELEM(6)));
}
#endif


template<typename VEC>
void test_fused ()
{
    typedef typename VEC::value_t ELEM;
    std::cout << "test_fused " << VEC::type_name() << "\n";

    VEC a (10, 11, 12, 13);
    VEC b (1, 2, 3, 4);
    VEC c (0.5, 1.5, 2.5, 3.5);
    OIIO_CHECK_SIMD_EQUAL (madd (a, b, c), a*b+c);
    OIIO_CHECK_SIMD_EQUAL (msub (a, b, c), a*b-c);
    OIIO_CHECK_SIMD_EQUAL (nmadd (a, b, c), -(a*b)+c);
    OIIO_CHECK_SIMD_EQUAL (nmsub (a, b, c), -(a*b)-c);
}



template<typename T> T do_and (const T& a, const T& b) { return a & b; }
template<typename T> T do_or  (const T& a, const T& b) { return a | b; }
template<typename T> T do_xor (const T& a, const T& b) { return a ^ b; }
template<typename T> T do_compl (const T& a) { return ~a; }
template<typename T> T do_andnot (const T& a, const T& b) { return andnot(a,b); }



template<typename VEC>
void test_bitwise_int ()
{
    typedef typename VEC::value_t ELEM;
    std::cout << "test_bitwise " << VEC::type_name() << "\n";

    VEC a (0x12341234);
    VEC b (0x11111111);
    OIIO_CHECK_SIMD_EQUAL (a & b, VEC(0x10101010));
    OIIO_CHECK_SIMD_EQUAL (a | b, VEC(0x13351335));
    OIIO_CHECK_SIMD_EQUAL (a ^ b, VEC(0x03250325));
    OIIO_CHECK_SIMD_EQUAL (~(a), VEC(0xedcbedcb));
    OIIO_CHECK_SIMD_EQUAL (andnot (b, a), (~(b)) & a);
    OIIO_CHECK_SIMD_EQUAL (andnot (b, a), VEC(0x02240224));
    benchmark2 ("operator&", benchsize, do_and<VEC>, a, b);
    benchmark2 ("operator|", benchsize, do_or<VEC>, a, b);
    benchmark2 ("operator^", benchsize, do_xor<VEC>, a, b);
    benchmark  ("operator!", benchsize, do_compl<VEC>, a);
    benchmark2 ("andnot",    benchsize, do_andnot<VEC>, a, b);
}



template<typename VEC>
void test_bitwise_bool ()
{
    typedef int ELEM;
    std::cout << "test_bitwise " << VEC::type_name() << "\n";

    bool A[]   = { true,  true,  false, false, false, false, true,  true  };
    bool B[]   = { true,  false, true,  false, true,  false, true,  false };
    bool AND[] = { true,  false, false, false, false, false, true,  false };
    bool OR[]  = { true,  true,  true,  false, true,  false, true,  true  };
    bool XOR[] = { false, true,  true,  false, true,  false, false, true  };
    bool NOT[] = { false, false, true,  true,  true,  true,  false, false  };
    VEC a(A), b(B), rand(AND), ror(OR), rxor(XOR), rnot(NOT);
    OIIO_CHECK_SIMD_EQUAL (a & b, rand);
    OIIO_CHECK_SIMD_EQUAL (a | b, ror);
    OIIO_CHECK_SIMD_EQUAL (a ^ b, rxor);
    OIIO_CHECK_SIMD_EQUAL (~a, rnot);
    benchmark2 ("operator&", benchsize, do_and<VEC>, a, b);
    benchmark2 ("operator|", benchsize, do_or<VEC>, a, b);
    benchmark2 ("operator^", benchsize, do_xor<VEC>, a, b);
    benchmark  ("operator!", benchsize, do_compl<VEC>, a);
}



template<class T, class B> B do_lt (const T& a, const T& b) { return a < b; }
template<class T, class B> B do_gt (const T& a, const T& b) { return a > b; }
template<class T, class B> B do_le (const T& a, const T& b) { return a <= b; }
template<class T, class B> B do_ge (const T& a, const T& b) { return a >= b; }
template<class T, class B> B do_eq (const T& a, const T& b) { return a == b; }
template<class T, class B> B do_ne (const T& a, const T& b) { return a != b; }



template<typename VEC>
void test_comparisons ()
{
    typedef typename VEC::value_t ELEM;
    typedef typename VEC::vbool_t vbool_t;
    std::cout << "test_comparisons " << VEC::type_name() << "\n";

    VEC a = VEC::Iota();
    bool lt2[] = { 1, 1, 0, 0, 0, 0, 0, 0 };
    bool gt2[] = { 0, 0, 0, 1, 1, 1, 1, 1 };
    bool le2[] = { 1, 1, 1, 0, 0, 0, 0, 0 };
    bool ge2[] = { 0, 0, 1, 1, 1, 1, 1, 1 };
    bool eq2[] = { 0, 0, 1, 0, 0, 0, 0, 0 };
    bool ne2[] = { 1, 1, 0, 1, 1, 1, 1, 1 };
    OIIO_CHECK_SIMD_EQUAL (a < 2, vbool_t(lt2));
    OIIO_CHECK_SIMD_EQUAL (a > 2, vbool_t(gt2));
    OIIO_CHECK_SIMD_EQUAL (a <= 2, vbool_t(le2));
    OIIO_CHECK_SIMD_EQUAL (a >= 2, vbool_t(ge2));
    OIIO_CHECK_SIMD_EQUAL (a == 2, vbool_t(eq2));
    OIIO_CHECK_SIMD_EQUAL (a != 2, vbool_t(ne2));
    VEC b (ELEM(2));
    OIIO_CHECK_SIMD_EQUAL (a < b, vbool_t(lt2));
    OIIO_CHECK_SIMD_EQUAL (a > b, vbool_t(gt2));
    OIIO_CHECK_SIMD_EQUAL (a <= b, vbool_t(le2));
    OIIO_CHECK_SIMD_EQUAL (a >= b, vbool_t(ge2));
    OIIO_CHECK_SIMD_EQUAL (a == b, vbool_t(eq2));
    OIIO_CHECK_SIMD_EQUAL (a != b, vbool_t(ne2));
    benchmark2 ("operator< ", benchsize, do_lt<VEC,vbool_t>, a, b);
    benchmark2 ("operator> ", benchsize, do_gt<VEC,vbool_t>, a, b);
    benchmark2 ("operator<=", benchsize, do_le<VEC,vbool_t>, a, b);
    benchmark2 ("operator>=", benchsize, do_ge<VEC,vbool_t>, a, b);
    benchmark2 ("operator==", benchsize, do_eq<VEC,vbool_t>, a, b);
    benchmark2 ("operator!=", benchsize, do_ne<VEC,vbool_t>, a, b);
}



template<typename VEC>
void test_shuffle ()
{
    std::cout << "test_shuffle " << VEC::type_name() << "\n";

    VEC a (0, 1, 2, 3);
    OIIO_CHECK_SIMD_EQUAL ((shuffle<3,2,1,0>(a)), VEC(3,2,1,0));
    OIIO_CHECK_SIMD_EQUAL ((shuffle<0,0,2,2>(a)), VEC(0,0,2,2));
    OIIO_CHECK_SIMD_EQUAL ((shuffle<1,1,3,3>(a)), VEC(1,1,3,3));
    OIIO_CHECK_SIMD_EQUAL ((shuffle<0,1,0,1>(a)), VEC(0,1,0,1));
}



template<typename VEC>
void test_swizzle ()
{
    std::cout << "test_swizzle " << VEC::type_name() << "\n";

    VEC a (0, 1, 2, 3);
    VEC b (10, 11, 12, 13);
    OIIO_CHECK_SIMD_EQUAL (AxyBxy(a,b), VEC(0,1,10,11));
    OIIO_CHECK_SIMD_EQUAL (AxBxAyBy(a,b), VEC(0,10,1,11));
    OIIO_CHECK_SIMD_EQUAL (b.xyz0(), VEC(10,11,12,0));
    OIIO_CHECK_SIMD_EQUAL (b.xyz1(), VEC(10,11,12,1));
}



template<typename VEC>
void test_blend ()
{
    std::cout << "test_blend " << VEC::type_name() << "\n";
    typedef typename VEC::value_t ELEM;
    typedef typename VEC::vbool_t vbool_t;

    VEC a = VEC::Iota (1);
    VEC b = VEC::Iota (10);
    vbool_t f(false), t(true);
    bool tf_values[] = { true, false, true, false, true, false, true, false };
    vbool_t tf ((bool *)tf_values);

    OIIO_CHECK_SIMD_EQUAL (blend (a, b, f), a);
    OIIO_CHECK_SIMD_EQUAL (blend (a, b, t), b);

    ELEM r1[] = { 10, 2, 12, 4, 14, 6, 16, 8 };
    OIIO_CHECK_SIMD_EQUAL (blend (a, b, tf), VEC(r1));

    OIIO_CHECK_SIMD_EQUAL (blend0 (a, f), VEC::Zero());
    OIIO_CHECK_SIMD_EQUAL (blend0 (a, t), a);
    ELEM r2[] = { 1, 0, 3, 0, 5, 0, 7, 0 };
    OIIO_CHECK_SIMD_EQUAL (blend0 (a, tf), VEC(r2));

    OIIO_CHECK_SIMD_EQUAL (blend0not (a, f), a);
    OIIO_CHECK_SIMD_EQUAL (blend0not (a, t), VEC::Zero());
    ELEM r3[] = { 0, 2, 0, 4, 0, 6, 0, 8 };
    OIIO_CHECK_SIMD_EQUAL (blend0not (a, tf), VEC(r3));

    benchmark ("blend", benchsize,
               [&](int){ return blend(a,b,tf); }, 0);
}



template<typename VEC>
void test_transpose ()
{
    std::cout << "test_transpose " << VEC::type_name() << "\n";

    VEC a (0, 1, 2, 3);
    VEC b (4, 5, 6, 7);
    VEC c (8, 9, 10, 11);
    VEC d (12, 13, 14, 15);

    OIIO_CHECK_SIMD_EQUAL (AxBxCxDx(a,b,c,d), VEC(0,4,8,12));

    std::cout << " before transpose:\n";
    std::cout << "\t" << a << "\n";
    std::cout << "\t" << b << "\n";
    std::cout << "\t" << c << "\n";
    std::cout << "\t" << d << "\n";
    transpose (a, b, c, d);
    std::cout << " after transpose:\n";
    std::cout << "\t" << a << "\n";
    std::cout << "\t" << b << "\n";
    std::cout << "\t" << c << "\n";
    std::cout << "\t" << d << "\n";
    OIIO_CHECK_SIMD_EQUAL (a, VEC(0,4,8,12));
    OIIO_CHECK_SIMD_EQUAL (b, VEC(1,5,9,13));
    OIIO_CHECK_SIMD_EQUAL (c, VEC(2,6,10,14));
    OIIO_CHECK_SIMD_EQUAL (d, VEC(3,7,11,15));
}



void test_shift ()
{
    std::cout << "test_shift\n";
    int4 i (1, 2, 4, 8);
    OIIO_CHECK_SIMD_EQUAL (i << 2, int4(4, 8, 16, 32));

    int a = 1<<31, b = -1, c = 0xffff, d = 3;
    int4 hard (a, b, c, d);
    OIIO_CHECK_SIMD_EQUAL (hard >> 1, int4(a>>1, b>>1, c>>1, d>>1));
    OIIO_CHECK_SIMD_EQUAL (srl(hard,1), int4(unsigned(a)>>1, unsigned(b)>>1,
                                             unsigned(c)>>1, unsigned(d)>>1));
    std::cout << Strutil::format ("  [%x] >>  1 == [%x]\n", hard, hard>>1);
    std::cout << Strutil::format ("  [%x] srl 1 == [%x]\n", hard, srl(hard,4));
    OIIO_CHECK_SIMD_EQUAL (hard >> 4, int4(a>>4, b>>4, c>>4, d>>4));
    OIIO_CHECK_SIMD_EQUAL (srl(hard,4), int4(unsigned(a)>>4, unsigned(b)>>4,
                                             unsigned(c)>>4, unsigned(d)>>4));
    std::cout << Strutil::format ("  [%x] >>  4 == [%x]\n", hard, hard>>4);
    std::cout << Strutil::format ("  [%x] srl 4 == [%x]\n", hard, srl(hard,4));

    i = int4(1,2,4,8);
    i <<= 1;
    OIIO_CHECK_SIMD_EQUAL (i, int4(2,4,8,16));
    i = int4(1,2,4,8);
    i >>= 1;
    OIIO_CHECK_SIMD_EQUAL (i, int4(0,1,2,4));
}



template<typename VEC>
void test_vectorops ()
{
    typedef typename VEC::value_t ELEM;
    std::cout << "test_vectorops " << VEC::type_name() << "\n";

    VEC a = mkvec<VEC> (10, 11, 12, 13);
    VEC b = mkvec<VEC> (1, 2, 3, 4);
    OIIO_CHECK_EQUAL (dot(a,b), ELEM(10+22+36+52));
    OIIO_CHECK_EQUAL (dot3(a,b), ELEM(10+22+36));
    OIIO_CHECK_SIMD_EQUAL (vdot(a,b), VEC(10+22+36+52));
    OIIO_CHECK_SIMD_EQUAL (vdot3(a,b), VEC(10+22+36));
}



template<>
void test_vectorops<float3> ()
{
    typedef float3 VEC;
    typedef typename VEC::value_t ELEM;
    std::cout << "test_vectorops " << VEC::type_name() << "\n";

    VEC a = mkvec<VEC> (10, 11, 12);
    VEC b = mkvec<VEC> (1, 2, 3);
    OIIO_CHECK_EQUAL (dot(a,b), ELEM(10+22+36));
    OIIO_CHECK_EQUAL (dot3(a,b), ELEM(10+22+36));
    OIIO_CHECK_SIMD_EQUAL (vdot(a,b), VEC(10+22+36));
    OIIO_CHECK_SIMD_EQUAL (vdot3(a,b), VEC(10+22+36));
}



void test_constants ()
{
    std::cout << "test_constants\n";

    OIIO_CHECK_SIMD_EQUAL (bool4::False(), bool4(false));
    OIIO_CHECK_SIMD_EQUAL (bool4::True(), bool4(true));

    OIIO_CHECK_SIMD_EQUAL (int4::Zero(), int4(0));
    OIIO_CHECK_SIMD_EQUAL (int4::One(), int4(1));
    OIIO_CHECK_SIMD_EQUAL (int4::NegOne(), int4(-1));

    OIIO_CHECK_SIMD_EQUAL (float4::Zero(), float4(0.0f));
    OIIO_CHECK_SIMD_EQUAL (float4::One(), float4(1.0f));

    OIIO_CHECK_SIMD_EQUAL (float3::Zero(), float3(0.0f));
    OIIO_CHECK_SIMD_EQUAL (float3::One(), float3(1.0f));
}



// Miscellaneous one-off stuff not caught by other tests
void test_special ()
{
    std::cout << "test_special\n";
    {
        // Make sure a float4 constructed from saturated unsigned short,
        // short, unsigned char, or char values, then divided by the float
        // max, exactly equals 1.0.
        short s32767[] = {32767, 32767, 32767, 32767};
        unsigned short us65535[] = {65535, 65535, 65535, 65535};
        char c127[] = {127, 127, 127, 127};
        unsigned char uc255[] = {255, 255, 255, 255};
        OIIO_CHECK_SIMD_EQUAL (float4(us65535)/float4(65535.0), float4(1.0f));
        OIIO_CHECK_SIMD_EQUAL (float4(us65535)*float4(1.0f/65535.0), float4(1.0f));
        OIIO_CHECK_SIMD_EQUAL (float4(s32767)/float4(32767.0), float4(1.0f));
        OIIO_CHECK_SIMD_EQUAL (float4(s32767)*float4(1.0f/32767.0), float4(1.0f));
        OIIO_CHECK_SIMD_EQUAL (float4(uc255)/float4(255.0), float4(1.0f));
        OIIO_CHECK_SIMD_EQUAL (float4(uc255)*float4(1.0f/255.0), float4(1.0f));
        OIIO_CHECK_SIMD_EQUAL (float4(c127)/float4(127.0), float4(1.0f));
        OIIO_CHECK_SIMD_EQUAL (float4(c127)*float4(1.0f/127.0), float4(1.0f));
    }
}



void test_mathfuncs ()
{
    std::cout << "test_mathfuncs\n";
    float4 A (-1.0f, 0.0f, 1.0f, 4.5f);
    float4 expA (0.367879441171442f, 1.0f, 2.718281828459045f, 90.0171313005218f);
    OIIO_CHECK_SIMD_EQUAL (exp(A), expA);
    OIIO_CHECK_SIMD_EQUAL_THRESH (log(expA), A, 1e-6f);
    OIIO_CHECK_SIMD_EQUAL (fast_exp(A),
                float4(fast_exp(A[0]), fast_exp(A[1]), fast_exp(A[2]), fast_exp(A[3])));
    OIIO_CHECK_SIMD_EQUAL (fast_log(expA),
                float4(fast_log(expA[0]), fast_log(expA[1]), fast_log(expA[2]), fast_log(expA[3])));
    OIIO_CHECK_SIMD_EQUAL_THRESH (fast_pow_pos(float4(2.0f), A),
                           float4(0.5f, 1.0f, 2.0f, 22.62741699796952f), 0.0001f);

    OIIO_CHECK_SIMD_EQUAL (safe_div(float4(1.0f,2.0f,3.0f,4.0f), float4(2.0f,0.0f,2.0f,0.0f)),
                           float4(0.5f,0.0f,1.5f,0.0f));
    OIIO_CHECK_SIMD_EQUAL (hdiv(float4(1.0f,2.0f,3.0f,2.0f)), float3(0.5f,1.0f,1.5f));
    OIIO_CHECK_SIMD_EQUAL (sqrt(float4(1.0f,4.0f,9.0f,16.0f)), float4(1.0f,2.0f,3.0f,4.0f));
    OIIO_CHECK_SIMD_EQUAL (rsqrt(float4(1.0f,4.0f,9.0f,16.0f)), float4(1.0f)/float4(1.0f,2.0f,3.0f,4.0f));
    OIIO_CHECK_SIMD_EQUAL_THRESH (rsqrt_fast(float4(1.0f,4.0f,9.0f,16.0f)),
                                  float4(1.0f)/float4(1.0f,2.0f,3.0f,4.0f), 0.0005f);
    OIIO_CHECK_SIMD_EQUAL (float3(1.0f,2.0f,3.0f).normalized(),
                           float3(norm_imath(Imath::V3f(1.0f,2.0f,3.0f))));
    OIIO_CHECK_SIMD_EQUAL_THRESH (float3(1.0f,2.0f,3.0f).normalized_fast(),
                                  float3(norm_imath(Imath::V3f(1.0f,2.0f,3.0f))), 0.0005);
}



void test_metaprogramming ()
{
    std::cout << "test_metaprogramming\n";
    OIIO_CHECK_EQUAL (SimdSize<float4>::size, 4);
    OIIO_CHECK_EQUAL (SimdSize<float3>::size, 4);
    OIIO_CHECK_EQUAL (SimdSize<int4>::size, 4);
    OIIO_CHECK_EQUAL (SimdSize<bool4>::size, 4);
    OIIO_CHECK_EQUAL (SimdSize<float>::size, 1);
    OIIO_CHECK_EQUAL (SimdSize<int>::size, 1);

    OIIO_CHECK_EQUAL (SimdElements<float4>::size, 4);
    OIIO_CHECK_EQUAL (SimdElements<float3>::size, 3);
    OIIO_CHECK_EQUAL (SimdElements<int4>::size, 4);
    OIIO_CHECK_EQUAL (SimdElements<bool4>::size, 4);
    OIIO_CHECK_EQUAL (SimdElements<float>::size, 1);
    OIIO_CHECK_EQUAL (SimdElements<int>::size, 1);

    OIIO_CHECK_EQUAL (float4::elements, 4);
    OIIO_CHECK_EQUAL (float3::elements, 3);
    OIIO_CHECK_EQUAL (int4::elements, 4);
    OIIO_CHECK_EQUAL (bool4::elements, 4);
    // OIIO_CHECK_EQUAL (is_same<float4::value_t,float>::value, true);
    // OIIO_CHECK_EQUAL (is_same<float3::value_t,float>::value, true);
    // OIIO_CHECK_EQUAL (is_same<int4::value_t,int>::value, true);
    // OIIO_CHECK_EQUAL (is_same<bool4::value_t,int>::value, true);
}



// Transform a point by a matrix using regular Imath
inline Imath::V3f
transformp_imath (const Imath::V3f &v, const Imath::M44f &m)
{
    Imath::V3f r;
    m.multVecMatrix (v, r);
    return r;
}

// Transform a point by a matrix using simd ops on Imath types.
inline Imath::V3f
transformp_imath_simd (const Imath::V3f &v, const Imath::M44f &m)
{
    return simd::transformp(m,v).V3f();
}

// Transform a simd point by an Imath matrix using SIMD
inline float3
transformp_simd (const float3 &v, const Imath::M44f &m)
{
    return simd::transformp (m, v);
}

// Transform a point by a matrix using regular Imath
inline Imath::V3f
transformv_imath (const Imath::V3f &v, const Imath::M44f &m)
{
    Imath::V3f r;
    m.multDirMatrix (v, r);
    return r;
}



inline bool
mx_equal_thresh (const matrix44 &a, const matrix44 &b, float thresh)
{
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            if (fabsf(a[j][i] - b[j][i]) > thresh)
                return false;
    return true;
}



void test_matrix ()
{
    Imath::V3f P (1.0f, 0.0f, 0.0f);
    Imath::M44f Mtrans (1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  10, 11, 12, 1);
    Imath::M44f Mrot = Imath::M44f().rotate(Imath::V3f(0.0f, M_PI_2, 0.0f));

    std::cout << "Testing matrix ops:\n";
    std::cout << "  P = " << P << "\n";
    std::cout << "  Mtrans = " << Mtrans << "\n";
    std::cout << "  Mrot   = " << Mrot << "\n";
    OIIO_CHECK_EQUAL (simd::transformp(Mtrans, P).V3f(),
                      transformp_imath(P, Mtrans));
    std::cout << "  P translated = " << simd::transformp(Mtrans,P) << "\n";
    OIIO_CHECK_EQUAL (simd::transformv(Mtrans,P).V3f(), P);
    OIIO_CHECK_EQUAL (simd::transformp(Mrot, P).V3f(),
                      transformp_imath(P, Mrot));
    std::cout << "  P rotated = " << simd::transformp(Mrot,P) << "\n";
    OIIO_CHECK_EQUAL (simd::transformvT(Mrot, P).V3f(),
                      transformv_imath(P, Mrot.transposed()));
    std::cout << "  P rotated by the transpose = " << simd::transformv(Mrot,P) << "\n";
    OIIO_CHECK_EQUAL (matrix44(Mrot).transposed().M44f(),
                      Mrot.transposed());
    std::cout << "  Mrot transposed = " << matrix44(Mrot).transposed().M44f() << "\n";
    {
        matrix44 mt (Mtrans), mr (Mrot);
        OIIO_CHECK_EQUAL (mt, mt);
        OIIO_CHECK_EQUAL (mt, Mtrans);
        OIIO_CHECK_EQUAL (Mtrans, mt);
        OIIO_CHECK_NE (mt, mr);
        OIIO_CHECK_NE (mr, Mtrans);
        OIIO_CHECK_NE (Mtrans, mr);
    }
    OIIO_CHECK_ASSERT (mx_equal_thresh (Mtrans.inverse(),
                       matrix44(Mtrans).inverse(), 1.0e-6f));
    OIIO_CHECK_ASSERT (mx_equal_thresh (Mrot.inverse(),
                       matrix44(Mrot).inverse(), 1.0e-6f));
}



#if OIIO_CPLUSPLUS_VERSION >= 11  /* So easy with lambdas */

// Wrappers to resolve the return type ambiguity
inline float fast_exp_float (float x) { return fast_exp(x); }
inline float4 fast_exp_float4 (const float4& x) { return fast_exp(x); }
inline float fast_log_float (float x) { return fast_log(x); }
inline float4 fast_log_float4 (const float4& x) { return fast_log(x); }

float dummy_float[16];
float dummy_float2[16];
float dummy_int[16];

template<typename VEC>
inline int loadstore_vec (int dummy) {
    typedef typename VEC::value_t ELEM;
    ELEM A[VEC::elements], B[VEC::elements];
    VEC v;
    v.load ((ELEM *)A);
    v.store ((ELEM *)B);
    return 0;
}

template<typename VEC, int N>
inline int loadstore_vec_N (int dummy) {
    typedef typename VEC::value_t ELEM;
    ELEM A[VEC::elements], B[VEC::elements];
    VEC v;
    v.load ((ELEM *)A, N);
    v.store ((ELEM *)B, N);
    return 0;
}

template<typename VEC>
inline int construct_one (int dummy) {
    typedef typename VEC::value_t ELEM;
    VEC v (ELEM(1));
    return 0;
}

template<typename VEC>
inline int assign_one (int dummy) {
    typedef typename VEC::value_t ELEM;
    VEC v;
    v = (ELEM(1));
    return 0;
}

template<typename VEC>
inline int assign_onemethod (int dummy) {
    VEC v = VEC::One();
    return 0;
}

template<typename VEC>
inline int assign_zeromethod (int dummy) {
    VEC v = VEC::Zero();
    return 0;
}

template<typename VEC>
inline VEC add_vec (const VEC &a, const VEC &b) {
    return a+b;
}

template<typename VEC>
inline VEC mul_vec (const VEC &a, const VEC &b) {
    return a*b;
}

template<typename VEC>
inline VEC div_vec (const VEC &a, const VEC &b) {
    return a/b;
}

// Add Imath 3-vectors using simd underneath
inline Imath::V3f
add_vec_simd (const Imath::V3f &a, const Imath::V3f &b) {
    return (float3(a)*float3(b)).V3f();
}

inline float dot_imath (const Imath::V3f &v) {
    return v.dot(v);
}
inline float dot_imath_simd (const Imath::V3f &v_) {
    float3 v (v_);
    return simd::dot(v,v);
}
inline float dot_simd (const simd::float3 v) {
    return dot(v,v);
}

inline Imath::M44f
mat_transpose (const Imath::M44f &m) {
    return m.transposed();
}

inline Imath::M44f
mat_transpose_simd (const Imath::M44f &m) {
    return matrix44(m).transposed().M44f();
}


inline float rsqrtf (float f) { return 1.0f / sqrtf(f); }

#endif


void test_timing ()
{
#if OIIO_CPLUSPLUS_VERSION >= 11  /* So easy with lambdas */
    const size_t size = 1000000;
    for (int i = 0; i < 16; ++i) {
        dummy_float[i] = 1.0f;
        dummy_int[i] = 1;
    }
    benchmark ("load/store float4", size, loadstore_vec<float4>, 0);
    benchmark ("load/store float4, 4 comps", size, loadstore_vec_N<float4, 4>, 0);
    benchmark ("load/store float4, 3 comps", size, loadstore_vec_N<float4, 3>, 0);
    benchmark ("load/store float4, 2 comps", size, loadstore_vec_N<float4, 2>, 0);
    benchmark ("load/store float4, 1 comps", size, loadstore_vec_N<float4, 1>, 0);
    benchmark ("load/store float3", size, loadstore_vec<float3>, 0);
    benchmark ("load/store int4", size, loadstore_vec<int4>, 0);
    benchmark ("load/store bool4", size, loadstore_vec<bool4>, 0);
    benchmark ("float4(const)", size, construct_one<float4>, 0);
    benchmark ("float4 = const", size, assign_one<float4>, 0);
    benchmark ("float4 = One()", size, assign_onemethod<float4>, 0);
    benchmark ("float4 = Zero()", size, assign_zeromethod<float4>, 0);

    benchmark2 ("add float", size, add_vec<float>, float(2.51f), float(3.1f));
    benchmark2 ("add float4", size, add_vec<float4>, float4(2.51f), float4(3.1f));
    benchmark2 ("add float3", size, add_vec<float3>, float3(2.51f), float3(3.1f));
    benchmark2 ("add Imath::V3f", size, add_vec<Imath::V3f>, Imath::V3f(2.51f,1.0f,1.0f), Imath::V3f(3.1f,1.0f,1.0f));
    benchmark2 ("add Imath::V3f with simd", size, add_vec_simd, Imath::V3f(2.51f,1.0f,1.0f), Imath::V3f(3.1f,1.0f,1.0f));
    benchmark2 ("add int", size, add_vec<int>, int(2), int(3));
    benchmark2 ("add int4", size, add_vec<int4>, int4(2), int4(3));
    benchmark2 ("mul float", size, mul_vec<float>, float(2.51f), float(3.1f));
    benchmark2 ("mul float4", size, mul_vec<float4>, float4(2.51f), float4(3.1f));
    benchmark2 ("mul float3", size, mul_vec<float3>, float3(2.51f), float3(3.1f));
    benchmark2 ("mul Imath::V3f", size, mul_vec<Imath::V3f>, Imath::V3f(2.51f,0.0f,0.0f), Imath::V3f(3.1f,0.0f,0.0f));
    benchmark2 ("div float", size, div_vec<float>, float(2.51f), float(3.1f));
    benchmark2 ("div float4", size, div_vec<float4>, float4(2.51f), float4(3.1f));
    benchmark2 ("div float3", size, div_vec<float3>, float3(2.51f), float3(3.1f));
    benchmark2 ("div int", size, div_vec<int>, int(2), int(3));
    benchmark2 ("div int4", size, div_vec<int4>, int4(2), int4(3));
    benchmark ("dot Imath::V3f", size, dot_imath, Imath::V3f(2.0f,1.0f,0.0f), 1);
    benchmark ("dot Imath::V3f with simd", size, dot_imath_simd, Imath::V3f(2.0f,1.0f,0.0f), 1);
    benchmark ("dot float3", size, dot_simd, float3(2.0f,1.0f,0.0f), 1);

    Imath::V3f vx (2.51f,1.0f,1.0f);
    Imath::M44f mx (1,0,0,0, 0,1,0,0, 0,0,1,0, 10,11,12,1);
    benchmark2 ("transformp Imath", size, transformp_imath, vx, mx, 1);
    benchmark2 ("transformp Imath with simd", size, transformp_imath_simd, vx, mx, 1);
    benchmark2 ("transformp simd", size, transformp_simd, float3(vx), mx, 1);
    benchmark ("transpose m44", size, mat_transpose, mx, 1);
    benchmark ("transpose m44 with simd", size, mat_transpose_simd, mx, 1);

    benchmark ("expf", size, expf, 0.67f);
    benchmark ("fast_exp", size, fast_exp_float, 0.67f);
    benchmark ("simd::exp", size, simd::exp, float4(0.67f));
    benchmark ("simd::fast_exp", size, fast_exp_float4, float4(0.67f));

    benchmark ("logf", size, logf, 0.67f);
    benchmark ("fast_log", size, fast_log_float, 0.67f);
    benchmark ("simd::log", size, simd::log, float4(0.67f));
    benchmark ("simd::fast_log", size, fast_log_float4, float4(0.67f));
    benchmark2 ("powf", size, powf, 0.67f, 0.67f);
    benchmark2 ("simd fast_pow_pos", size, fast_pow_pos, float4(0.67f), float4(0.67f));
    benchmark ("sqrt", size, sqrtf, 4.0f);
    benchmark ("simd::sqrt", size, simd::sqrt, float4(1.0f,4.0f,9.0f,16.0f));
    benchmark ("rsqrt", size, rsqrtf, 4.0f);
    benchmark ("simd::rsqrt", size, simd::rsqrt, float4(1.0f,4.0f,9.0f,16.0f));
    benchmark ("simd::rsqrt_fast", size, simd::rsqrt_fast, float4(1.0f,4.0f,9.0f,16.0f));
    benchmark ("normalize Imath", size, norm_imath, Imath::V3f(1.0f,4.0f,9.0f));
    benchmark ("normalize Imath with simd", size, norm_imath_simd, Imath::V3f(1.0f,4.0f,9.0f));
    benchmark ("normalize Imath with simd fast", size, norm_imath_simd_fast, Imath::V3f(1.0f,4.0f,9.0f));
    benchmark ("normalize simd", size, norm_simd, float3(1.0f,4.0f,9.0f));
    benchmark ("normalize simd fast", size, norm_simd_fast, float3(1.0f,4.0f,9.0f));
    benchmark ("m44 inverse Imath", size/8, inverse_imath, mx, 1);
    // std::cout << "inv " << matrix44(inverse_imath(mx)) << "\n";
    benchmark ("m44 inverse_simd", size/8, inverse_simd, mx, 1);
    // std::cout << "inv " << inverse_simd(mx) << "\n";
    benchmark ("m44 inverse_simd native simd", size/8, inverse_simd, matrix44(mx), 1);
    // std::cout << "inv " << inverse_simd(mx) << "\n";
#endif
}



int
main (int argc, char *argv[])
{
#if !defined(NDEBUG) || defined(OIIO_CI) || defined(OIIO_CODECOV)
    // For the sake of test time, reduce the default iterations for DEBUG,
    // CI, and code coverage builds. Explicit use of --iters or --trials
    // will override this, since it comes before the getargs() call.
    iterations /= 10;
    ntrials = 1;
#endif

    getargs (argc, argv);

#if defined(OIIO_SIMD_AVX)
    std::cout << "SIMD is AVX " << OIIO_SIMD_AVX << "\n";
#elif defined(OIIO_SIMD_SSE)
    std::cout << "SIMD is SSE " << OIIO_SIMD_SSE << "\n";
#elif defined(OIIO_SIMD_NEON)
    std::cout << "SIMD is NEON " << OIIO_SIMD_NEON << "\n";
#else
    std::cout << "NO SIMD!!\n";
#endif
    Timer timer;

    std::cout << "\n";
    test_loadstore<float4> ();
    test_component_access<float4> ();
    test_arithmetic<float3> ();
    test_arithmetic<float4> ();
    test_comparisons<float4> ();
    test_shuffle<float4> ();
    test_swizzle<float4> ();
    test_blend<float4> ();
    test_transpose<float4> ();
    test_vectorops<float4> ();
    test_fused<float4> ();

    std::cout << "\n";
    test_loadstore<float3> ();
    test_component_access<float3> ();
    // Unnecessary to test these, they just use the float4 ops.
    // test_comparisons<float3> ();
    // test_shuffle<float3> ();
    // test_swizzle<float3> ();
    // test_blend<float3> ();
    // test_transpose<float3> ();
    test_vectorops<float3> ();
    // test_fused<float3> ();

    std::cout << "\n";
    test_loadstore<int4> ();
    test_loadstore<int8> ();
    test_component_access<int4> ();
    test_component_access<int8> ();
    test_arithmetic<int4> ();
    test_arithmetic<int8> ();
    test_bitwise_int<int4> ();
    test_bitwise_int<int8> ();
    test_comparisons<int4> ();
    test_comparisons<int8> ();

    test_shuffle<int4> ();

    test_blend<int4> ();
    test_blend<int8> ();

    test_transpose<int4> ();
    test_int4_to_uint16s ();
    test_int4_to_uint8s ();
    test_shift ();


    std::cout << "\n";
    test_shuffle<bool4> ();
    // test_shuffle<bool8> ();
    test_component_access<bool4> ();
    test_component_access<bool8> ();
    test_bitwise_bool<bool4> ();
    test_bitwise_bool<bool8> ();

    test_constants();
    test_special();
    test_mathfuncs();
    test_metaprogramming();
    test_matrix();

    std::cout << "\nTiming tests:\n";
    test_timing();

    std::cout << "Total time: " << Strutil::timeintervalformat(timer()) << "\n";

    if (unit_test_failures)
        std::cout << "\nERRORS!\n";
    else
        std::cout << "\nOK\n";
    return unit_test_failures;
}

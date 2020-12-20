// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/tiffutils.h>

#include <libheif/heif.h>
// #include <libheif/heif_cxx.h>


OIIO_PLUGIN_NAMESPACE_BEGIN


struct ctx_deleter {
    void operator()(heif_context* c) { heif_context_free(c); }
};

struct heif_image_deleter {
    void operator()(heif_image* i) { heif_image_release(i); }
};

struct heif_encoder_deleter {
    void operator()(heif_encoder* e) { heif_encoder_release(e); }
};



class HeifOutput final : public ImageOutput {
public:
    HeifOutput() {}
    virtual ~HeifOutput() { close(); }
    virtual const char* format_name(void) const override { return "heif"; }
    virtual int supports(string_view feature) const override
    {
        return feature == "alpha" || feature == "exif";
    }
    virtual bool open(const std::string& name, const ImageSpec& spec,
                      OpenMode mode) override;
    virtual bool write_scanline(int y, int z, TypeDesc format, const void* data,
                                stride_t xstride) override;
    virtual bool write_tile(int x, int y, int z, TypeDesc format,
                            const void* data, stride_t xstride,
                            stride_t ystride, stride_t zstride) override;
    virtual bool close() override;

private:
    std::string m_filename;
    std::unique_ptr<heif_context, ctx_deleter> m_hctx;
    std::unique_ptr<heif_image, heif_image_deleter> m_hhimage;
    std::unique_ptr<heif_encoder, heif_encoder_deleter> m_hencoder;
    std::vector<heif_channel> m_hchannels;
    std::vector<uint8_t*> m_hplanes;
    std::vector<int> m_ystrides;
    // std::unique_ptr<heif::Context> m_ctx;
    // heif::ImageHandle m_ihandle;
    // heif::Image m_himage;
    // heif::Encoder m_encoder { heif_compression_HEVC };
    std::vector<unsigned char> scratch;
    std::vector<unsigned char> m_tilebuffer;

    bool checkerr(string_view label, heif_error herr) {
        if (herr.code != heif_error_Ok) {
            error("{} error {}.{} \"{}\"", label, int(herr.code),
                  int(herr.subcode), herr.message);
            return false;
        }
        return true;
    }

};



namespace {

#if 0
class MyHeifWriter final : public heif::Context::Writer {
public:
    MyHeifWriter(Filesystem::IOProxy* ioproxy)
        : m_ioproxy(ioproxy)
    {
    }
    virtual heif_error write(const void* data, size_t size)
    {
        heif_error herr { heif_error_Ok, heif_suberror_Unspecified, "" };
        if (m_ioproxy && m_ioproxy->mode() == Filesystem::IOProxy::Write
            && m_ioproxy->write(data, size) == size) {
            // ok
        } else {
            herr.code    = heif_error_Encoding_error;
            herr.message = "write error";
        }
        return herr;
    }

private:
    Filesystem::IOProxy* m_ioproxy = nullptr;
};
#endif

}  // namespace


OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageOutput*
heif_output_imageio_create()
{
    return new HeifOutput;
}

OIIO_EXPORT const char* heif_output_extensions[] = { "heif", "heic", "heics",
                                                     nullptr };

OIIO_PLUGIN_EXPORTS_END


bool
HeifOutput::open(const std::string& name, const ImageSpec& newspec,
                 OpenMode mode)
{
    if (mode != Create) {
        errorf("%s does not support subimages or MIP levels", format_name());
        return false;
    }

    m_filename = name;
    m_spec = newspec;  // Save spec for later use

    // Check for things heif can't support
    if (m_spec.nchannels != 1 && m_spec.nchannels != 3
        && m_spec.nchannels != 4) {
        errorf("heif can only support 1-, 3- or 4-channel images");
        return false;
    }
    if (m_spec.width < 1 || m_spec.height < 1) {
        errorf("Image resolution must be at least 1x1, you asked for %d x %d",
               m_spec.width, m_spec.height);
        return false;
    }
    if (m_spec.depth < 1)
        m_spec.depth = 1;
    if (m_spec.depth > 1) {
        errorf("%s does not support volume images (depth > 1)", format_name());
        return false;
    }

    m_spec.set_format(TypeUInt8);  // Only uint8 for now

    // try {
        m_hctx.reset(heif_context_alloc());
        heif_image* himg = nullptr;
        heif_error herr;
        // static heif_chroma chromas[/*nchannels*/]
        //     = { heif_chroma_undefined, heif_chroma_monochrome,
        //         heif_chroma_undefined, heif_chroma_interleaved_RGB,
        //         heif_chroma_interleaved_RGBA };
        auto colorspace = m_spec.nchannels == 1 ? heif_colorspace_monochrome
                                                : heif_colorspace_RGB;
        herr = heif_image_create(m_spec.width, m_spec.height,
                                 colorspace,
                                 heif_chroma_444 /*chromas[m_spec.nchannels] */,
                                 &himg);
        m_hhimage.reset(himg);
        if (!checkerr("heif_image_create", herr))
            return false;
        m_hchannels.resize(m_spec.nchannels);
        for (int c = 0; c < m_spec.nchannels; ++c) {
            static heif_channel hchannel[/*channel*/] = {
                heif_channel_R, heif_channel_G, heif_channel_B, heif_channel_Alpha
            };
            m_hchannels[c] = m_spec.nchannels == 1 ? heif_channel_Y : hchannel[c];
        }
        for (int c = 0; c < m_spec.nchannels; ++c) {
            herr = heif_image_add_plane(m_hhimage.get(), m_hchannels[c],
                                        m_spec.width, m_spec.height, 8);
            // FIXME ^^^ this limits us to 8 bits per channel
            if (!checkerr("heif_image_add_plane", herr))
                return false;
        }
        m_hplanes.resize(m_spec.nchannels);
        m_ystrides.resize(m_spec.nchannels);
        for (int c = 0; c < m_spec.nchannels; ++c) {
            m_hplanes[c] = heif_image_get_plane(m_hhimage.get(), m_hchannels[c],
                                                &(m_ystrides[c]));
        }

        heif_encoder* enc;
        herr = heif_context_get_encoder_for_format(m_hctx.get(),
                                                   heif_compression_HEVC, &enc);
        m_hencoder.reset(enc);
        if (!checkerr("heif_context_get_encoder_for_format", herr))
            return false;

    // } catch (const heif::Error& err) {
    //     std::string e = err.get_message();
    //     errorf("%s", e.empty() ? "unknown exception" : e.c_str());
    //     return false;
    // } catch (const std::exception& err) {
    //     std::string e = err.what();
    //     errorf("%s", e.empty() ? "unknown exception" : e.c_str());
    //     return false;
    // }

    // If user asked for tiles -- which this format doesn't support, emulate
    // it by buffering the whole image.
    if (m_spec.tile_width && m_spec.tile_height)
        m_tilebuffer.resize(m_spec.image_bytes());

    return true;
}



bool
HeifOutput::write_scanline(int y, int /*z*/, TypeDesc format, const void* data,
                           stride_t xstride)
{
    data = to_native_scanline(format, data, xstride, scratch);
    // uint8_t* hdata = m_hhimage.get_plane(heif_channel_interleaved, &hystride);
    for (int c = 0; c < m_spec.nchannels; ++c) {
        OIIO::copy_image(1, m_spec.width, 1 /*height*/, 1 /*depth*/,
                         data, 1 /*pixelsize*/, m_spec.pixel_bytes(),
                         m_spec.scanline_bytes(), 0,
                         m_hplanes[c] + m_ystrides[c] * (y - m_spec.y), 1,
                         m_spec.width, m_spec.height);
        // int hystride   = 0;
        // uint8_t* hdata = heif_image_get_plane(m_hhimage.get(),
        //                                       heif_channel_interleaved, &hystride);
    }
    // hdata += hystride * (y - m_spec.y);
    // memcpy(hdata, data, hystride);
    return true;
}



bool
HeifOutput::write_tile(int x, int y, int z, TypeDesc format, const void* data,
                       stride_t xstride, stride_t ystride, stride_t zstride)
{
    // Emulate tiles by buffering the whole image
    return copy_tile_to_image_buffer(x, y, z, format, data, xstride, ystride,
                                     zstride, &m_tilebuffer[0]);
}



bool
HeifOutput::close()
{
    if (!m_hctx) {  // already closed
        return true;
    }

    bool ok = true;
    if (m_spec.tile_width) {
        // We've been emulating tiles; now dump as scanlines.
        OIIO_ASSERT(m_tilebuffer.size());
        ok &= write_scanlines(m_spec.y, m_spec.y + m_spec.height, 0,
                              m_spec.format, &m_tilebuffer[0]);
        m_tilebuffer.clear();
        m_tilebuffer.shrink_to_fit();
    }

    std::vector<char> exifblob;
    // try {
#if 0
        auto compqual = m_spec.decode_compression_metadata("", 75);
        if (compqual.first == "heic") {
            if (compqual.second >= 100)
                m_encoder.set_lossless(true);
            else {
                m_encoder.set_lossless(false);
                m_encoder.set_lossy_quality(compqual.second);
            }
        } else if (compqual.first == "none") {
            m_encoder.set_lossless(true);
        }
        encode_exif(m_spec, exifblob, endian::big);
        m_ihandle = m_ctx->encode_image(m_himage, m_encoder);
        std::vector<char> head { 'E', 'x', 'i', 'f', 0, 0 };
        exifblob.insert(exifblob.begin(), head.begin(), head.end());
        try {
            // m_ctx->add_exif_metadata(m_ihandle, exifblob.data(),
            //                          exifblob.size());
        } catch (const heif::Error& err) {
#ifdef DEBUG
            std::string e = err.get_message();
            Strutil::printf("%s", e.empty() ? "unknown exception" : e.c_str());
#endif
        }
        m_ctx->set_primary_image(m_ihandle);
#endif
#if 0
        Filesystem::IOFile ioproxy(m_filename, Filesystem::IOProxy::Write);
        if (ioproxy.mode() != Filesystem::IOProxy::Write) {
            errorf("Could not open \"%s\"", m_filename);
            ok = false;
        } else {
            MyHeifWriter writer(&ioproxy);
            m_ctx->write(writer);
        }
#else
        heif_error herr;
        herr = heif_context_encode_image(m_hctx.get(), m_hhimage.get(),
                                  m_hencoder.get(), nullptr /*enc options*/,
                                  nullptr /* imagehandle** */);
        if (!checkerr("heif_context_get_encoder_for_format", herr))
            return false;
        m_hencoder.reset();
        herr = heif_context_write_to_file(m_hctx.get(), m_filename.c_str());
        if (!checkerr("heif_context_write_to_file", herr))
            return false;
        // m_ctx->write_to_file(m_filename);
#endif
    // } catch (const heif::Error& err) {
    //     std::string e = err.get_message();
    //     errorf("%s", e.empty() ? "unknown exception" : e.c_str());
    //     return false;
    // } catch (const std::exception& err) {
    //     std::string e = err.what();
    //     errorf("%s", e.empty() ? "unknown exception" : e.c_str());
    //     return false;
    // }

    m_hctx.reset();
    return ok;
}

OIIO_PLUGIN_NAMESPACE_END

/*
  Copyright 2017 Larry Gritz and the other authors and contributors.
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


#include <cstdio>
#include <cstdlib>

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/filesystem.h>
#include "imageio_pvt.h"



OIIO_PLUGIN_NAMESPACE_BEGIN


// Null output just sits there like a lump and returns ok for everything.
class NullOutput final : public ImageOutput {
public:
    NullOutput () { }
    virtual ~NullOutput () { }
    virtual const char * format_name (void) const { return "null"; }
    virtual int supports (string_view feature) const { return true; }
    virtual bool open (const std::string &name, const ImageSpec &spec,
                       OpenMode mode=Create) {
        m_spec = spec;
        return true;
    }
    virtual bool close () { return true; }
    virtual bool write_scanline (int y, int z, TypeDesc format,
                                 const void *data, stride_t xstride) {
        return true;
    }
    virtual bool write_tile (int x, int y, int z, TypeDesc format,
                             const void *data, stride_t xstride,
                             stride_t ystride, stride_t zstride) {
        return true;
    }
};




// Null input emulates a file, but just returns black tiles.
// But we accept REST-like filename designations to set certain parameters,
// such as "myfile.null&RES=1920x1080&CHANNELS=3&TYPE=uint16"
class NullInput final : public ImageInput {
public:
    NullInput () { init(); }
    virtual ~NullInput () { }
    virtual const char * format_name (void) const { return "null"; }
    virtual bool valid_file (const std::string &filename) const;
    virtual int supports (string_view feature) const { return true; }
    virtual bool open (const std::string &name, ImageSpec &newspec);
    virtual bool open (const std::string &name, ImageSpec &newspec,
                       const ImageSpec &config);
    virtual bool close () { return true; }
    virtual int current_subimage (void) const { return m_subimage; }
    virtual int current_miplevel (void) const { return m_miplevel; }
    virtual bool seek_subimage (int subimage, int miplevel);
    virtual bool read_native_scanline (int y, int z, void *data);
    virtual bool read_native_tile (int subimge, int miplevel,
                                   int x, int y, int z, void *data);

private:
    std::string m_filename;          ///< Stash the filename
    int m_subimage;                  ///< What subimage are we looking at?
    int m_miplevel;                  ///< What miplevel are we looking at?
    bool m_mip;                      ///< MIP-mapped?
    std::vector<uint8_t> m_value;    ///< Pixel value (if not black)
    ImageSpec m_topspec;

    // Reset everything to initial state
    void init () {
        m_subimage = -1;
        m_miplevel = -1;
        m_mip = false;
        m_value.clear ();
    }
};



// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageOutput *null_output_imageio_create () {
    return new NullOutput;
}

OIIO_EXPORT int null_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char* null_imageio_library_version () {
    return "null 1.0";
}

OIIO_EXPORT const char * null_output_extensions[] = {
    "null", "nul", NULL
};

OIIO_EXPORT ImageInput *null_input_imageio_create () {
    return new NullInput;
}

OIIO_EXPORT const char * null_input_extensions[] = {
    "null", "nul", NULL
};

OIIO_PLUGIN_EXPORTS_END




bool
NullInput::valid_file (const std::string &name) const
{
    std::map<std::string,std::string> args;
    std::string filename;
    if (! Strutil::get_rest_arguments (name, filename, args))
        return false;
    return Strutil::ends_with (filename, ".null") ||
           Strutil::ends_with (filename, ".nul");
}



bool
NullInput::open (const std::string &name, ImageSpec &newspec)
{
    ImageSpec config (1024, 1024, 3, TypeDesc::UINT8);
    return open (name, newspec, config);
}



static void
parse_res (string_view res, int &x, int &y, int &z)
{
    if (Strutil::parse_int (res, x)) {
        if (Strutil::parse_char (res, 'x') &&
            Strutil::parse_int (res, y)) {
            if (! (Strutil::parse_char(res, 'x') &&
                   Strutil::parse_int(res, z)))
                z = 1;
        } else {
            y = x;
            z = 1;
        }
    }
}



// Add the attribute -- figure out the type
void
parse_param (string_view paramname, string_view val, ImageSpec &spec)
{
    TypeDesc type;   // start out unknown

    // If the param string starts with a type name, that's what it is
    if (size_t typeportion = type.fromstring (paramname)) {
        paramname.remove_prefix (typeportion);
        Strutil::skip_whitespace (paramname);
    }
    // If the value string starts with a type name, that's what it is
    else if (size_t typeportion = type.fromstring (val)) {
        val.remove_prefix (typeportion);
        Strutil::skip_whitespace (val);
    }

    if (type.basetype == TypeDesc::UNKNOWN) {
        // If we didn't find a type name, try to guess
        if (val.size() >= 2 && val.front() == '\"' && val.back() == '\"') {
            // Surrounded by quotes? it's a string (strip off the quotes)
            val.remove_prefix(1); val.remove_suffix(1);
            type = TypeDesc::TypeString;
        } else if (Strutil::string_is<int>(val)) {
            // Looks like an int, is an int
            type = TypeDesc::TypeInt;
        } else if (Strutil::string_is<float>(val)) {
            // Looks like a float, is a float
            type = TypeDesc::TypeFloat;
        } else {
            // Everything else is assumed a string
            type = TypeDesc::TypeString;
        }
    }

    // Read the values and set the attribute
    int n = type.numelements() * type.aggregate;
    if (type.basetype == TypeDesc::INT) {
        std::vector<int> values (n);
        for (int i = 0; i < n; ++i) {
            Strutil::parse_int (val, values[i]);
            Strutil::parse_char (val, ','); // optional
        }
        if (n > 0)
            spec.attribute (paramname, type, &values[0]);
    }
    if (type.basetype == TypeDesc::FLOAT) {
        std::vector<float> values (n);
        for (int i = 0; i < n; ++i) {
            Strutil::parse_float (val, values[i]);
            Strutil::parse_char (val, ','); // optional
        }
        if (n > 0)
            spec.attribute (paramname, type, &values[0]);
    } else if (type.basetype == TypeDesc::STRING) {
        std::vector<ustring> values (n);
        for (int i = 0; i < n; ++i) {
            string_view v;
            Strutil::parse_string (val, v);
            Strutil::parse_char (val, ','); // optional
            values[i] = v;
        }
        if (n > 0)
            spec.attribute (paramname, type, &values[0]);
    }
}



bool
NullInput::open (const std::string &name, ImageSpec &newspec,
                 const ImageSpec &config)
{
    m_filename = name;
    m_subimage = -1;
    m_miplevel = -1;
    m_mip = false;
    m_topspec = config;

    // std::vector<std::pair<string_view,string_view> > args;
    // string_view filename = deconstruct_uri (name, &args);
    std::map<std::string,std::string> args;
    std::string filename;
    if (! Strutil::get_rest_arguments (name, filename, args))
        return false;
    if (filename.empty())
        return false;
    if (! Strutil::ends_with (filename, ".null") &&
        ! Strutil::ends_with (filename, ".nul"))
        return false;

    m_filename = filename;
    m_topspec = ImageSpec (1024, 1024, 4, TypeDesc::UINT8);
    std::vector<float> fvalue;

    for (const auto& a : args) {
        if (a.first == "RES") {
            parse_res (a.second, m_topspec.width, m_topspec.height, m_topspec.depth);
        } else if (a.first == "TILE" || a.first == "TILES") {
            parse_res (a.second, m_topspec.tile_width, m_topspec.tile_height,
                       m_topspec.tile_depth);
        } else if (a.first == "CHANNELS") {
            m_topspec.nchannels = Strutil::from_string<int>(a.second);
        } else if (a.first == "MIP") {
            m_mip = Strutil::from_string<int>(a.second);
        } else if (a.first == "TEX") {
            if (Strutil::from_string<int>(a.second)) {
                if (!m_spec.tile_width) {
                    m_topspec.tile_width = 64;
                    m_topspec.tile_height = 64;
                    m_topspec.tile_depth = 1;
                }
                m_topspec.attribute ("wrapmodes", "black,black");
                m_topspec.attribute ("textureformat", "Plain Texture");
                m_mip = true;
            }
        } else if (a.first == "TYPE") {
            m_topspec.set_format (TypeDesc(a.second));
        } else if (a.first == "PIXEL") {
            Strutil::extract_from_list_string (fvalue, a.second);
            fvalue.resize (m_topspec.nchannels);
        } else if (a.first.size() && a.second.size()) {
            parse_param (a.first, a.second, m_topspec);
        }
    }

    m_topspec.default_channel_names ();
    m_topspec.full_x = m_topspec.x;
    m_topspec.full_y = m_topspec.y;
    m_topspec.full_z = m_topspec.z;
    m_topspec.full_width = m_topspec.width;
    m_topspec.full_height = m_topspec.height;
    m_topspec.full_depth = m_topspec.depth;

    if (fvalue.size()) {
        // Convert float to the native type
        fvalue.resize (m_topspec.nchannels, 0.0f);
        m_value.resize (m_topspec.pixel_bytes());
        convert_types (TypeFloat, fvalue.data(),
                       m_topspec.format, m_value.data(),
                       m_topspec.nchannels);
    }

    bool ok = seek_subimage (0, 0);
    if (ok)
        newspec = spec();
    else
        close();
    return ok;
}



bool
NullInput::seek_subimage (int subimage, int miplevel)
{
    if (subimage == current_subimage() && miplevel == current_miplevel()) {
        return true;
    }

    if (subimage != 0)
        return false;    // We only make one subimage
    m_subimage = subimage;

    if (miplevel > 0 && ! m_mip)
        return false;    // Asked for MIP levels but we aren't makign them

    m_spec = m_topspec;
    for (m_miplevel = 0; m_miplevel < miplevel; ++m_miplevel) {
        if (m_spec.width == 1 && m_spec.height == 1 && m_spec.depth == 1)
            return false;   // Asked for more MIP levels than were available
        m_spec.width = std::max (1, m_spec.width/2);
        m_spec.height = std::max (1, m_spec.height/2);
        m_spec.depth = std::max (1, m_spec.depth/2);
        m_spec.full_width = m_spec.width;
        m_spec.full_height = m_spec.height;
        m_spec.full_depth = m_spec.depth;
    }
    return true;
}



bool
NullInput::read_native_scanline (int y, int z, void *data)
{
    if (m_value.size()) {
        size_t s = m_spec.pixel_bytes();
        for (int x = 0; x < m_spec.width; ++x)
            memcpy ((char *)data + s*x, m_value.data(), s);
    } else {
        memset (data, 0, m_spec.scanline_bytes());
    }
    return true;
}



bool
NullInput::read_native_tile (int subimage, int miplevel,
                             int x, int y, int z, void *data)
{
    if (m_value.size()) {
        size_t s = m_spec.pixel_bytes();
        for (size_t x = 0, e = m_spec.tile_pixels(); x < e; ++x)
            memcpy ((char *)data + s*x, m_value.data(), s);
    } else {
        memset (data, 0, m_spec.tile_bytes());
    }
    return true;
}


OIIO_PLUGIN_NAMESPACE_END


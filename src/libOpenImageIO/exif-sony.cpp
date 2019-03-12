/*
  Copyright 2019 Larry Gritz et al. All Rights Reserved.

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

// clang-format off

// See: https://sno.phy.queensu.ca/~phil/exiftool/TagNames/Sony.html
//

#include <algorithm>
#include <array>
#include <type_traits>

#include "exif.h"

OIIO_NAMESPACE_BEGIN
namespace pvt {


static LabelIndex sony_offon_table[] = {
    { 0, "Off" }, { 1, "On" }
};

static LabelIndex sony_offauto_table[] = {
    { 0, "Off" }, { 1, "Auto" }
};

static LabelIndex sony_quality_table[] = {
    { 0, "RAW" }, { 1, "super fine" }, { 2, "find" }, { 3, "standard" },
    { 4, "economy" }, { 5, "extra fine" }, { 6, "RAW+JPEG" },
    { 7, "compressed RAW" }, { 8, "compressed RAW+JPEG" }
};

static LabelIndex sony_teleconverter_table[] = {
    { 0x0, "None" },
    { 0x4, "Minolta/Sony AF 1.4x APO (D) (0x04)" },
    { 0x5, "Minolta/Sony AF 2x APO (D) (0x05)" },
    { 0x48, "Minolta/Sony AF 2x APO (D)" },
    { 0x50, "Minolta AF 2x APO II" },
    { 0x60, "Minolta AF 2x APO" },
    { 0x88, "Minolta/Sony AF 1.4x APO (D)" },
    { 0x90, "Minolta AF 1.4x APO II" },
    { 0xa0, "Minolta AF 1.4x APO" },
};

static LabelIndex sony_whitebalance_table[] = {
    { 0x0, "Auto" }, { 0x1, "Color Temperature/Color Filter" },
    { 0x10, "Daylight" }, { 0x20, "Cloudy" }, { 0x30, "Shade" },
    { 0x40, "Tungsten" }, { 0x50, "Flash" }, { 0x60, "Fluorescent" },
    { 0x70, "Custom" }, { 0x80, "Underwater" }
};

static LabelIndex sony_pictureeffect_table[] = {
    { 0, "Off" }, { 1, "Toy Camera" }, { 2, "Pop Color" }, { 3, "Posterization" },
    { 4, "Posterization B/W" }, { 5, "Retro Photo" }, { 6, "Soft High Key" },
    { 7, "Partial Color (red)" }, { 8, "Partial Color (green)" },
    { 9, "Partial Color (blue)" }, { 10, "Partial Color (yellow)" },
    { 13, "High Contrast Monochrome" }, { 16, "Toy Camera (normal)" },
    { 17, "Toy Camera (cool)" }, { 18, "Toy Camera (warm)" },
    { 19, "Toy Camera (green)" }, { 20, "Toy Camera (magenta)" },
    { 32, "Soft Focus (low)" }, { 33, "Soft Focus" },
    { 34, "Soft Focus (high)" }, { 48, "Miniature (auto)" },
    { 49, "Miniature (top)" }, { 50, "Miniature (middle horizontal)" },
    { 51, "Miniature (bottom)" }, { 52, "Miniature (left)" },
    { 53, "Miniature (middle vertical)" }, { 54, "Miniature (right)" },
    { 64, "HDR Painting (low)" }, { 65, "HDR Painting" },
    { 66, "HDR Painting (high)" }, { 80, "Rich-tone Monochrome" },
    { 97, "Water Color" }, { 98, "Water Color 2" },
    { 112, "Illustration (low)" }, { 113, "Illustration" },
    { 114, "Illustration (high)" }
};

static LabelIndex sony_softskineffect_table[] = {
    { 0, "Off" }, { 1, "Low" }, { 2, "Mid" }, { 3, "High" },
};

static const ExplanationTableEntry sony_explanations[] = {
    { "Sony:Quality", explain_labeltable, sony_quality_table },
    { "Sony:Teleconverter", explain_labeltable, sony_teleconverter_table },
    { "Sony:WhiteBalance", explain_labeltable, sony_whitebalance_table },
    { "Sony:MultiBurstMode", explain_labeltable, sony_offon_table },
    { "Sony:FrameNoiseReduction", explain_labeltable, sony_offon_table },
    { "Sony:ImageStabilization", explain_labeltable, sony_offon_table },
    { "Sony:PictureEffect", explain_labeltable, sony_pictureeffect_table },
    { "Sony:SoftSkinEffect", explain_labeltable, sony_softskineffect_table },
    { "Sony:VignettingCorrection", explain_labeltable, sony_offauto_table },
    { "Sony:LateralChromaticAberration", explain_labeltable, sony_offauto_table },
    { "Sony:DistortionCorrectionSetting", explain_labeltable, sony_offauto_table },
};



cspan<ExplanationTableEntry>
sony_explanation_table ()
{
    return cspan<ExplanationTableEntry>(sony_explanations);
}


/////////////////////////////////////////////////////////////////////////


inline void
block_to_spec (ImageSpec& spec,                  // spec to put attribs into
               const TIFFDirEntry& dir,          // TIFF dir entry
               cspan<uint8_t> buf,               // raw buffer blob
               cspan<StructLayoutSpec> fields,   // layout table
               int offset_adjustment=0)
{
    const char *data = (const char *) pvt::dataptr (dir, buf, offset_adjustment);
    for (auto&& attr : fields) {
        size_t size = attr.type.size();
        if (attr.type == TypeString && attr.stringlen)
            size = attr.stringlen;
        if (attr.offset+offset_adjustment+size > size_t(buf.size()))
            return;   // Nonsense, it runs past the buffer size
        if (attr.type.basetype == TypeDesc::UINT32 || attr.type.basetype == TypeDesc::INT32
            || attr.type.basetype == TypeDesc::UINT16 || attr.type.basetype == TypeDesc::INT16)
            spec.attribute (attr.name, attr.type, data+attr.offset);
        else if (attr.type == TypeString) {
            string_view s (data+attr.offset, attr.stringlen);
            spec.attribute (attr.name, Strutil::strip(s));
        }
    }
}

#if 0
static LabelIndex sony_sensorinfo_indices[] = {
    { 1, "Sony:SensorWidth" },
    { 2, "Sony:SensorHeight" },
    { 5, "Sony:SensorLeftBorder" },
    { 6, "Sony:SensorTopBorder" },
    { 7, "Sony:SensorRightBorder" },
    { 8, "Sony:SensorBottomBorder" },
    { 9, "Sony:BlackMaskLeftBorder" },
    { 10, "Sony:BlackMaskTopBorder" },
    { 11, "Sony:BlackMaskRightBorder" },
    { 12, "Sony:BlackMaskBottomBorder" },
};

static void
sony_shotinfo_handler (const TagInfo& taginfo, const TIFFDirEntry& dir,
                      cspan<uint8_t> buf, ImageSpec& spec,
                          bool swapendian, int offset_adjustment)
{
    array_to_spec<uint16_t> (spec, dir, buf, sony_sensorinfo_indices, offset_adjustment);
}
#endif

static StructLayoutSpec sony_sensorinfo_indices[] = {
    { 6, "Sony:DateTime", TypeString, 20 },
    { 26, "Sony:ImageHeight", TypeUInt16 },
    { 28, "Sony:ImageWidth", TypeUInt16 },
    { 48, "Sony:FacesDetected", TypeUInt16 },
    // { 48, "Sony:FaceInfoLength", TypeUInt16 },
    { 52, "Sony:MetaVersion", TypeString, 16 },
};

static void
sony_shotinfo_handler (const TagInfo& taginfo, const TIFFDirEntry& dir,
                       cspan<uint8_t> buf, ImageSpec& spec,
                       bool swapendian, int offset_adjustment)
{

    block_to_spec (spec, dir, buf, sony_sensorinfo_indices, offset_adjustment);
}



static const TagInfo sony_maker_tag_table[] = {
//    { 0x0010, "Sony:CameraInfo", TIFF_X },
//    { 0x0020, "Sony:FocusInfo", TIFF_X },
    { 0x0102, "Sony:Quality", TIFF_LONG },
    { 0x0104, "Sony:FlashExposureComp", TIFF_SRATIONAL },
    { 0x0105, "Sony:Teleconverter", TIFF_LONG },
    { 0x0112, "Sony:WhiteBalanceFineTune", TIFF_LONG },
//    { 0x0114, "Sony:CameraSettings", TIFF_X },
    { 0x0115, "Sony:WhiteBalance", TIFF_LONG },
//    { 0x0116, "Sony:ExtraInfo", TIFF_X },
//    { 0x0e00, "Sony:PrintIM", TIFF_X },
//    { 0x1000, "Sony:MultiBurstMode", TIFF_X },
    { 0x1001, "Sony:MultiBurstWidth", TIFF_SHORT },
    { 0x1002, "Sony:MultiBurstHeight", TIFF_SHORT },
//    { 0x1003, "Sony:Panorama", TIFF_X },
//    { 0x2001, "Sony:PreviewImage", TIFF_X },
    { 0x2002, "Sony:Rating", TIFF_LONG },
    { 0x2004, "Sony:Contrast", TIFF_SLONG },
    { 0x2005, "Sony:Saturation", TIFF_SLONG },
    { 0x2006, "Sony:Sharpness", TIFF_SLONG },
    { 0x2007, "Sony:Brightness", TIFF_SLONG },
    { 0x2008, "Sony:LongExposureNoiseReduction", TIFF_LONG },
    { 0x2009, "Sony:HighISONoiseReduction", TIFF_SHORT },
    { 0x200a, "Sony:HDR", TIFF_LONG },
    { 0x200b, "Sony:MultiFrameNoiseReduction", TIFF_LONG },
    { 0x200e, "Sony:PictureEffect", TIFF_SHORT },
    { 0x200f, "Sony:SoftSkinEffect", TIFF_LONG },
    // { 0x2010, "Sony:Tag2010", TIFF_X },
    { 0x2011, "Sony:VignettingCorrection", TIFF_LONG },
    { 0x2012, "Sony:LateralChromaticAberration", TIFF_LONG },
    { 0x2013, "Sony:DistortionCorrectionSetting", TIFF_LONG },
    { 0x2014, "Sony:WBShiftAB_GM", TIFF_LONG, 2 },
    { 0x2016, "Sony:AutoPortraitFramed", TIFF_SHORT },
    { 0x2017, "Sony:FlashAction", TIFF_LONG },
    { 0x201a, "Sony:ElectronicFrontCurtainShutter", TIFF_LONG },
    { 0x201b, "Sony:FocusMode", TIFF_BYTE },
    { 0x201c, "Sony:AFAreaModeSetting", TIFF_BYTE },
    { 0x201d, "Sony:FlexibleSpotPosition", TIFF_SHORT, 2 },
    { 0x201e, "Sony:AFPointSelected", TIFF_BYTE },
//     { 0x2020, "Sony:AFPointsUsed", TIFF_X },
    { 0x2021, "Sony:AFTracking", TIFF_BYTE },
//     { 0x2022, "Sony:FocalPlaneAFPointsUsed", TIFF_X },
    { 0x2023, "Sony:MultiFrameNREffect", TIFF_LONG },
    { 0x2026, "Sony:WBShiftAB_GM_Precise", TIFF_SLONG, 2 },
    { 0x2027, "Sony:FocusLocation", TIFF_SHORT, 4 },
    { 0x2028, "Sony:VariableLowPassFilter", TIFF_SHORT, 2 },
    { 0x2029, "Sony:RAWFileType", TIFF_SHORT },
//     { 0x202a, "Sony:Tag202a", TIFF_X },
    { 0x202b, "Sony:PrioritySetInAWB", TIFF_BYTE },
    { 0x202c, "Sony:MeteringMode2", TIFF_SHORT },
    { 0x202d, "Sony:ExposureStandardAdjustment", TIFF_SRATIONAL },
    { 0x202e, "Sony:Quality2", TIFF_SRATIONAL },
//     { 0x202f, "Sony:PixelShiftInfo", TIFF_X },
    { 0x2031, "Sony:SerialNumber", TIFF_ASCII },
    { 0x3000, "Sony:ShotInfo", TIFF_NOTYPE, 0, sony_shotinfo_handler },
//     { 0x900b, "Sony:Tag900b", TIFF_X },
//     { 0x9050, "Sony:Tag9050", TIFF_X },
//     { 0x9400, "Sony:Tag9400", TIFF_X },
//     { 0x9401, "Sony:Tag9401", TIFF_X },
//     { 0x9402, "Sony:Tag9402", TIFF_X },
//     { 0x9403, "Sony:Tag9403", TIFF_X },
//     { 0x9404, "Sony:Tag9404", TIFF_X },
//     { 0x9405, "Sony:Tag9405", TIFF_X },
//     { 0x9406, "Sony:Tag9406", TIFF_X },
//     { 0x940a, "Sony:Tag940a", TIFF_X },
//     { 0x940c, "Sony:Tag940c", TIFF_X },
//     { 0x940e, "Sony:Tag940e", TIFF_X },
    { 0xb000, "Sony:FileFormat", TIFF_BYTE, 4 },
    { 0xb001, "Sony:ModelID", TIFF_SHORT },
    { 0xb020, "Sony:CreativeStyle", TIFF_ASCII },
    { 0xb021, "Sony:ColorTemperature", TIFF_LONG },
    { 0xb022, "Sony:ColorCompensationFilter", TIFF_LONG },
    { 0xb023, "Sony:SceneMode", TIFF_LONG },
    { 0xb024, "Sony:ZoneMatching", TIFF_LONG },
    { 0xb025, "Sony:DynamicRangeOptimizer", TIFF_LONG },
    { 0xb026, "Sony:ImageStabilization", TIFF_LONG },
    { 0xb027, "Sony:LensType", TIFF_LONG },
//     { 0xb028, "Sony:MinoltaMakerNote", TIFF_X },
    { 0xb029, "Sony:ColorMode", TIFF_LONG },
    { 0xb02a, "Sony:LensSpec", TIFF_BYTE, 8 },
    { 0xb02b, "Sony:FullImageSize", TIFF_LONG, 2 },
    { 0xb040, "Sony:Macro", TIFF_SHORT },
    { 0xb041, "Sony:ExposureMode", TIFF_SHORT },
    { 0xb042, "Sony:FocusMode", TIFF_SHORT },
    { 0xb043, "Sony:AFAreaMode", TIFF_SHORT },
    { 0xb044, "Sony:AfIlluminator", TIFF_SHORT },
    { 0xb047, "Sony:JPEGQuality", TIFF_SHORT },
    { 0xb048, "Sony:FlashLevel", TIFF_SSHORT },
    { 0xb049, "Sony:ReleaseMode", TIFF_SHORT },
    { 0xb04a, "Sony:SequenceNumber", TIFF_SHORT },
    { 0xb04b, "Sony:AntiBlur", TIFF_SHORT },
    { 0xb04e, "Sony:FocusMode", TIFF_SHORT },
    { 0xb04f, "Sony:DynamicRangeOptimizer", TIFF_SHORT },
    { 0xb050, "Sony:HighISONoiseReduction2", TIFF_SHORT },
    { 0xb052, "Sony:IntelligentAuto", TIFF_SHORT },
    { 0xb054, "Sony:WhiteBalance2", TIFF_SHORT },
//     { 0xb000, "Sony:", TIFF_X },
//     { 0xb000, "Sony:", TIFF_X },
//     { 0xb000, "Sony:", TIFF_X },
//     { 0xb000, "Sony:", TIFF_X },
//     { 0xb000, "Sony:", TIFF_X },

//    { SONY_CAMERAINFO, "Sony:CameraInfo",  TIFF_SHORT, 0, sony_camerasettings_handler },
//    { SONY_CAMERAINFO, "Sony:CameraInfo",  TIFF_SHORT, 0, sony_camerasettings_handler },
};



const TagMap& sony_maker_tagmap_ref () {
    static TagMap T ("Sony", sony_maker_tag_table);
    return T;
}



#if 0
// Put a whole bunch of sub-indexed data into the spec into the given TIFF
// tag.
template<typename T>
static void
encode_indexed_tag (int tifftag, TIFFDataType tifftype, // TIFF tag and type
                    cspan<LabelIndex> indices,        // LabelIndex table
                    std::vector<char>& data,          // data blob to add to
                    std::vector<TIFFDirEntry> &dirs,  // TIFF dirs to add to
                    const ImageSpec& spec,            // spec to get attribs from
                    size_t offset_correction)         // offset correction
{
    // array length is determined by highest index value
    std::vector<T> array (indices.back().value + 1, T(0));
    bool anyfound = false;
    for (auto&& attr : indices) {
        if (attr.value < int(array.size())) {
            const ParamValue *param = spec.find_attribute (attr.label);
            if (param) {
                array[attr.value] = T (param->get_int());
                anyfound = true;
            }
        }
    }
    if (anyfound)
        append_tiff_dir_entry (dirs, data, tifftag, tifftype,
                               array.size(), array.data(), offset_correction);
}
#endif



void
encode_sony_makernote (std::vector<char>& data,
                        std::vector<TIFFDirEntry> &makerdirs,
                        const ImageSpec& spec, size_t offset_correction)
{
    // Easy ones that get coded straight from the attribs
    for (const TagInfo& t : sony_maker_tag_table) {
        if (t.handler)   // skip  ones with handlers
            continue;
        if (const ParamValue* param = spec.find_attribute (t.name)) {
            size_t count = t.tiffcount;
            const void* d = param->data();
            if (t.tifftype == TIFF_ASCII) {
                // special case: strings need their real length, plus
                // trailing null, and the data must be the characters.
                d = param->get_ustring().c_str();
                count = param->get_ustring().size() + 1;
            }
            append_tiff_dir_entry (makerdirs, data, t.tifftag, t.tifftype,
                                   count, d, offset_correction);
        }
    }

#if 0
    // Hard ones that need to fill in complicated structures
    encode_indexed_tag<int16_t> (SONY_CAMERASETTINGS, TIFF_SSHORT,
                                 sony_camerasettings_indices,
                                 data, makerdirs, spec, offset_correction);
    encode_indexed_tag<uint16_t> (SONY_FOCALLENGTH, TIFF_SHORT,
                                 sony_focallength_indices,
                                 data, makerdirs, spec, offset_correction);
    encode_indexed_tag<int16_t> (SONY_SHOTINFO, TIFF_SSHORT,
                                 sony_shotinfo_indices,
                                 data, makerdirs, spec, offset_correction);
    encode_indexed_tag<int16_t> (SONY_SHOTINFO, TIFF_SSHORT,
                                 sony_shotinfo_indices,
                                 data, makerdirs, spec, offset_correction);
    encode_indexed_tag<int16_t> (SONY_PANORAMA, TIFF_SSHORT,
                                 sony_panorama_indices,
                                 data, makerdirs, spec, offset_correction);
#endif
}


}  // end namespace pvt
OIIO_NAMESPACE_END

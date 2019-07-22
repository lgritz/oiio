// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


/////////////////////////////////////////////////////////////////////////
/// @file  optparser.h
///
/// @brief Option parser template
/////////////////////////////////////////////////////////////////////////


#pragma once

#include <OpenImageIO/strutil.h>
#include <string>

OIIO_NAMESPACE_BEGIN


/// Parse a string of the form "name=value" and then call
/// system.attribute (name, value), with appropriate type conversions.
template<class C>
inline bool
optparse1(C& system, string_view opt)
{
    auto pieces = Strutil::splitsv(opt, "=", 2);
    if (pieces.size() < 2)
        return false;  // malformed option
    string_view name = pieces[0], value = pieces[1];
    Strutil::trim_whitespace(name);
    if (name.empty())
        return false;
    if (Strutil::string_is_int(value))
        return system.attribute(name, Strutil::stoi(value));
    if (Strutil::string_is_float(value))
        return system.attribute(name, Strutil::stof(value));
    // otherwise treat it as a string

    // trim surrounding double quotes
    if (value.size() >= 2 && (value[0] == '\"' || value[0] == '\'')
        && value[value.size() - 1] == value[0])
        value = value.substr(1, value.size() - 2);

    return system.attribute(name, value);
}



/// Parse a string with comma-separated name=value directives, calling
/// system.attribute(name,value) for each one, with appropriate type
/// conversions.  Examples:
///    optparser(texturesystem, "verbose=1");
///    optparser(texturesystem, "max_memory_MB=32.0");
///    optparser(texturesystem, "a=1,b=2,c=3.14,d=\"a string\"");
template<class C>
inline bool
optparser(C& system, const std::string& optstring)
{
    bool ok    = true;
    size_t len = optstring.length();
    size_t pos = 0;
    while (pos < len) {
        std::string opt;
        char inquote = 0;
        while (pos < len) {
            unsigned char c = optstring[pos];
            if (c == inquote) {
                // Ending a quote
                inquote = 0;
                opt += c;
                ++pos;
            } else if (c == '\"' || c == '\'') {
                // Found a quote
                inquote = c;
                opt += c;
                ++pos;
            } else if (c == ',' && !inquote) {
                // Hit a comma and not inside a quote -- we have an option
                ++pos;  // skip the comma
                break;  // done with option
            } else {
                // Anything else: add to the option
                opt += c;
                ++pos;
            }
        }
        // At this point, opt holds an option
        ok &= optparse1(system, opt);
    }
    return ok;
}


OIIO_NAMESPACE_END

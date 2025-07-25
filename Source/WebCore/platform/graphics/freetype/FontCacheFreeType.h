/*
 * Copyright (C) 2018 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include "FontTaggedSettings.h"
#include "ShouldLocalizeAxisNames.h"
#include <wtf/HashMap.h>

typedef struct FT_FaceRec_* FT_Face;

namespace WebCore {
class FontDescription;

#if ENABLE(VARIATION_FONTS)
struct VariationDefaults {
    String axisName;
    float defaultValue;
    float minimumValue;
    float maximumValue;
};

using VariationDefaultsMap = HashMap<FontTag, VariationDefaults, FourCharacterTagHash, FourCharacterTagHashTraits>;
using VariationsMap = HashMap<FontTag, float, FourCharacterTagHash, FourCharacterTagHashTraits>;

VariationDefaultsMap defaultVariationValues(FT_Face, ShouldLocalizeAxisNames);

String buildVariationSettings(FT_Face, const FontDescription&, const FontCreationContext&);
#endif
};

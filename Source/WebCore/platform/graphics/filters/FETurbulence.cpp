/*
 * Copyright (C) 2004, 2005, 2006, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005 Rob Buis <buis@kde.org>
 * Copyright (C) 2005 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) 2010 Renata Hodovan <reni@inf.u-szeged.hu>
 * Copyright (C) 2011 Gabor Loki <loki@webkit.org>
 * Copyright (C) 2017-2022 Apple Inc. All rights reserved.
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

#include "config.h"
#include "FETurbulence.h"

#include "FETurbulenceSoftwareApplier.h"
#include "Filter.h"
#include <wtf/text/TextStream.h>

namespace WebCore {

Ref<FETurbulence> FETurbulence::create(TurbulenceType type, float baseFrequencyX, float baseFrequencyY, int numOctaves, float seed, bool stitchTiles, DestinationColorSpace colorSpace)
{
    return adoptRef(*new FETurbulence(type, baseFrequencyX, baseFrequencyY, numOctaves, seed, stitchTiles, colorSpace));
}

FETurbulence::FETurbulence(TurbulenceType type, float baseFrequencyX, float baseFrequencyY, int numOctaves, float seed, bool stitchTiles, DestinationColorSpace colorSpace)
    : FilterEffect(FilterEffect::Type::FETurbulence, colorSpace)
    , m_type(type)
    , m_baseFrequencyX(baseFrequencyX)
    , m_baseFrequencyY(baseFrequencyY)
    , m_numOctaves(numOctaves)
    , m_seed(seed)
    , m_stitchTiles(stitchTiles)
{
}

bool FETurbulence::operator==(const FETurbulence& other) const
{
    return FilterEffect::operator==(other)
        && m_type == other.m_type
        && m_baseFrequencyX == other.m_baseFrequencyX
        && m_baseFrequencyY == other.m_baseFrequencyY
        && m_numOctaves == other.m_numOctaves
        && m_seed == other.m_seed
        && m_stitchTiles == other.m_stitchTiles;
}

bool FETurbulence::setType(TurbulenceType type)
{
    if (m_type == type)
        return false;
    m_type = type;
    return true;
}

bool FETurbulence::setBaseFrequencyY(float baseFrequencyY)
{
    if (m_baseFrequencyY == baseFrequencyY)
        return false;
    m_baseFrequencyY = baseFrequencyY;
    return true;
}

bool FETurbulence::setBaseFrequencyX(float baseFrequencyX)
{
    if (m_baseFrequencyX == baseFrequencyX)
        return false;
    m_baseFrequencyX = baseFrequencyX;
    return true;
}

bool FETurbulence::setSeed(float seed)
{
    if (m_seed == seed)
        return false;
    m_seed = seed;
    return true;
}

bool FETurbulence::setNumOctaves(int numOctaves)
{
    if (m_numOctaves == numOctaves)
        return false;
    m_numOctaves = numOctaves;
    return true;
}

bool FETurbulence::setStitchTiles(bool stitch)
{
    if (m_stitchTiles == stitch)
        return false;
    m_stitchTiles = stitch;
    return true;
}

FloatRect FETurbulence::calculateImageRect(const Filter& filter, std::span<const FloatRect>, const FloatRect& primitiveSubregion) const
{
    return filter.maxEffectRect(primitiveSubregion);
}

std::unique_ptr<FilterEffectApplier> FETurbulence::createSoftwareApplier() const
{
    return FilterEffectApplier::create<FETurbulenceSoftwareApplier>(*this);
}

static TextStream& operator<<(TextStream& ts, TurbulenceType type)
{
    switch (type) {
    case TurbulenceType::Unknown:
        ts << "UNKNOWN"_s;
        break;
    case TurbulenceType::Turbulence:
        ts << "TURBULENCE"_s;
        break;
    case TurbulenceType::FractalNoise:
        ts << "NOISE"_s;
        break;
    }
    return ts;
}

TextStream& FETurbulence::externalRepresentation(TextStream& ts, FilterRepresentation representation) const
{
    ts << indent << "[feTurbulence"_s;
    FilterEffect::externalRepresentation(ts, representation);
    
    ts << " type=\""_s << type() << '"';
    ts << " baseFrequency=\""_s << baseFrequencyX() << ", "_s << baseFrequencyY() << '"';
    ts << " seed=\""_s << seed() << '"';
    ts << " numOctaves=\""_s << numOctaves() << '"';
    ts << " stitchTiles=\""_s << stitchTiles() << '"';

    ts << "]\n"_s;
    return ts;
}

} // namespace WebCore

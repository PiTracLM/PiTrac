/* Copyright 2005-2011 Mark Dufour and contributors; License Expat (See LICENSE) */
/* Copyright (C) 2022-2025, Verdant Consultants, LLC. */


#include "colorsys.h"

#include <algorithm>


/**
Conversion functions between RGB and other color systems.

This modules provides two functions for each color system ABC:

  rgb_to_abc(r, g, b) --> a, b, c
  abc_to_rgb(a, b, c) --> r, g, b

All inputs and outputs are triples of floats in the range [0.0...1.0]
(with the exception of I and Q, which covers a slightly larger range).
Inputs outside the valid range may cause exceptions or invalid outputs.

Supported color systems:
RGB: Red, Green, Blue components
YIQ: Luminance, Chrominance (used by composite video signals)
HLS: Hue, Luminance, Saturation
HSV: Hue, Saturation, Value
*/

namespace golf_sim {

    const float colorsys::ONE_THIRD = (1.0f / 3.0f);
    const float colorsys::ONE_SIXTH = (1.0f / 6.0f);
    const float colorsys::TWO_THIRD = (2.0f / 3.0f);


    GsColorTriplet colorsys::rgb_to_yiq(const GsColorTriplet& rgb)
    {
        float r = (float)rgb[0];
        float g = (float)rgb[1];
        float b = (float)rgb[2];

        float y = (float)(((0.3f * r) + (0.59f * g)) + (0.11f * b));
        float i = (float)(((0.6f * r) - (0.28f * g)) - (0.32f * b));
        float q = (float)(((0.21f * r) - (0.52f * g)) + (0.31f * b));

        return (GsColorTriplet(y, i, q));
    }


    GsColorTriplet colorsys::yiq_to_rgb(const GsColorTriplet& yiq)
    {
        float y = yiq[0];
        float i = yiq[1];
        float q = yiq[2];

        float r = (float)std::clamp((double)(y + (0.948262f * i)) + (0.624013f * q), 0., 1.);
        float g = (float)std::clamp((double)(y - (0.276066f * i)) - (0.63981f * q), 0., 1.);
        float b = (float)std::clamp((double)(y - (1.10545f * i)) + (1.72986f * q), 0., 1.);

        return (GsColorTriplet(r, g, b));
    }


    GsColorTriplet colorsys::rgb_to_hls(const GsColorTriplet& rgb)
    {
        float bc, gc, h, l, maxc, minc, rc, s;

        float r = rgb[0];
        float g = rgb[1];
        float b = rgb[2];

        maxc = std::max({ r, g, b });
        minc = std::min({ r, g, b });

        l = ((minc + maxc) / 2.0f);
        if (minc == maxc)
        {
            return (GsColorTriplet(0.0, l, 0.0));
        }
        if (l <= 0.5)
        {
            s = ((maxc - minc) / (maxc + minc));
        }
        else
        {
            s = ((maxc - minc) / ((2.0f - maxc) - minc));
        }

        rc = ((maxc - r) / (maxc - minc));
        gc = ((maxc - g) / (maxc - minc));
        bc = ((maxc - b) / (maxc - minc));

        if (r == maxc) {
            h = (bc - gc);
        }
        else if (g == maxc) {
            h = ((2.0f + rc) - bc);
        }
        else {
            h = ((4.0f + gc) - rc);
        }
        h = fmods((h / 6.0f), 1.0);

        return (GsColorTriplet(h, l, s));
    }

    GsColorTriplet colorsys::hls_to_rgb(const GsColorTriplet& hls)
    {
        float m1, m2;

        float h = (float)hls[0];
        float l = (float)hls[1];
        float s = (float)hls[2];

        if (s == 0.0) {
            return (GsColorTriplet(l, l, l));
        }
        if ((l <= 0.5)) {
            m2 = (l * (1.0f + s));
        }
        else {
            m2 = ((l + s) - (l * s));
        }
        m1 = ((2.0f * l) - m2);
        GsColorTriplet result = (GsColorTriplet(_v(m1, m2, (h + ONE_THIRD)), _v(m1, m2, h), _v(m1, m2, (h - ONE_THIRD))));
        return (result);
    }

    float colorsys::_v(float m1, float m2, float hue) {

        hue = fmods(hue, 1.0);
        if ((hue < ONE_SIXTH)) {
            return (m1 + (((m2 - m1) * hue) * 6.0f));
        }
        if (hue < 0.5) {
            return m2;
        }
        if (hue < TWO_THIRD) {
            return (m1 + (((m2 - m1) * (TWO_THIRD - hue)) * 6.0f));
        }
        return m1;
    }

    GsColorTriplet colorsys::rgb_to_hsv(const GsColorTriplet& rgb)
    {
        float bc, gc, h, maxc, minc, rc, s, v;

        float r = (float)rgb[0];
        float g = (float)rgb[1];
        float b = (float)rgb[2];

        maxc = std::max({ r, g, b });
        minc = std::min({ r, g, b });
        v = maxc;

        if (minc == maxc) {
            return (GsColorTriplet(0.0, 0.0, v));
        }
        s = ((maxc - minc) / maxc);
        rc = ((maxc - r) / (maxc - minc));
        gc = ((maxc - g) / (maxc - minc));
        bc = ((maxc - b) / (maxc - minc));
        if (r == maxc) {
            h = (bc - gc);
        }
        else if (g == maxc) {
            h = ((2.0f + rc) - bc);
        }
        else {
            h = ((4.0f + gc) - rc);
        }
        h = fmods((h / 6.0f), 1.0);

        return (GsColorTriplet(h, s, v));
    }

    GsColorTriplet colorsys::hsv_to_rgb(const GsColorTriplet& hsv)
    {
        float f, p, q, t;
        int i;

        float h = hsv[0];
        float s = hsv[1];
        float v = hsv[2];

        if (s == 0.0) {
            return (GsColorTriplet(v, v, v));
        }
        i = (int)((h * 6.0f));
        f = ((h * 6.0f) - i);
        p = (v * (1.0f - s));
        q = (v * (1.0f - (s * f)));
        t = (v * (1.0f - (s * (1.0f - f))));
        i = (int)fmods((float)i, 6.0f);
        if (i == 0) {
            return (GsColorTriplet(v, t, p));
        }
        if (i == 1) {
            return (GsColorTriplet(q, v, p));
        }
        if (i == 2) {
            return (GsColorTriplet(p, v, t));
        }
        if (i == 3) {
            return (GsColorTriplet(p, q, v));
        }
        if (i == 4) {
            return (GsColorTriplet(t, p, v));
        }
        if (i == 5) {
            return (GsColorTriplet(v, p, q));
        }
        return 0;
    }

    /*** Test Code:
    *
    // Sloppy way to make close-enough comparison
    bool compareVec3f(GsColorTriplet a, GsColorTriplet b, int precision)
    {
        GsColorTriplet diff = a - b;
        float epsilon = pow(10, -precision);
        float mag = abs(diff[0]) + abs(diff[1]) + abs(diff[2]);
        return (mag < epsilon);
    }

    void test_function(int a)
    {
        BOOST_LOG_FUNCTION();
        BOOST_LOG_TRIVIAL(info) << "Test called";

        assert(compareVec3f(colorsys::rgb_to_hsv(Vec3f(0.2, 0.4, 0.4)), Vec3f(0.5, 0.5, 0.4), 3));
        assert(compareVec3f(colorsys::hsv_to_rgb(Vec3f(0.5, 0.5, 0.4)), Vec3f(0.2, 0.4, 0.4), 3));

        assert(compareVec3f(colorsys::hls_to_rgb(Vec3f(1.0, 0.5, 0.7)), Vec3f(0.85, 0.15, 0.15), 3));
        assert(compareVec3f(colorsys::rgb_to_hls(Vec3f(1.0, 0.5, 0.7)), Vec3f(0.93, 0.75, 1.00), 2));
        assert(compareVec3f(colorsys::rgb_to_yiq(GsColorTriplet(1.0, 0.5, 0.7)), GsColorTriplet(0.67, 0.24, 0.17), 2));
        assert(compareVec3f(colorsys::hsv_to_rgb(GsColorTriplet(1.0, 0.5, 0.7)), GsColorTriplet(0.70, 0.35, 0.35), 2));
        assert(compareVec3f(colorsys::rgb_to_hsv(GsColorTriplet(1.0, 0.5, 0.7)), GsColorTriplet(0.93, 0.50, 1.00), 2));
    }
    */

}
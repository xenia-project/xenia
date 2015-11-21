/*
** Copyright (c) 2014-2015 The Khronos Group Inc.
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and/or associated documentation files (the "Materials"),
** to deal in the Materials without restriction, including without limitation
** the rights to use, copy, modify, merge, publish, distribute, sublicense,
** and/or sell copies of the Materials, and to permit persons to whom the
** Materials are furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Materials.
**
** MODIFICATIONS TO THIS FILE MAY MEAN IT NO LONGER ACCURATELY REFLECTS KHRONOS
** STANDARDS. THE UNMODIFIED, NORMATIVE VERSIONS OF KHRONOS SPECIFICATIONS AND
** HEADER INFORMATION ARE LOCATED AT https://www.khronos.org/registry/
**
** THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
** OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
** THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM,OUT OF OR IN CONNECTION WITH THE MATERIALS OR THE USE OR OTHER DEALINGS
** IN THE MATERIALS.
*/

#ifndef GLSLstd450_H
#define GLSLstd450_H

namespace spv {

const int GLSLstd450Version = 99;
const int GLSLstd450Revision = 3;

enum class GLSLstd450 : unsigned {
    Bad = 0,              // Don't use

    Round = 1,
    RoundEven = 2,
    Trunc = 3,
    FAbs = 4,
    SAbs = 5,
    FSign = 6,
    SSign = 7,
    Floor = 8,
    Ceil = 9,
    Fract = 10,

    Radians = 11,
    Degrees = 12,
    Sin = 13,
    Cos = 14,
    Tan = 15,
    Asin = 16,
    Acos = 17,
    Atan = 18,
    Sinh = 19,
    Cosh = 20,
    Tanh = 21,
    Asinh = 22,
    Acosh = 23,
    Atanh = 24,
    Atan2 = 25,

    Pow = 26,
    Exp = 27,
    Log = 28,
    Exp2 = 29,
    Log2 = 30,
    Sqrt = 31,
    InverseSqrt = 32,

    Determinant = 33,
    MatrixInverse = 34,

    Modf = 35,            // second operand needs an OpVariable to write to
    ModfStruct = 36,      // no OpVariable operand
    FMin = 37,
    UMin = 38,
    SMin = 39,
    FMax = 40,
    UMax = 41,
    SMax = 42,
    FClamp = 43,
    UClamp = 44,
    SClamp = 45,
    FMix = 46,
    IMix = 47,
    Step = 48,
    SmoothStep = 49,

    Fma = 50,
    Frexp = 51,            // second operand needs an OpVariable to write to
    FrexpStruct = 52,      // no OpVariable operand
    Ldexp = 53,

    PackSnorm4x8 = 54,
    PackUnorm4x8 = 55,
    PackSnorm2x16 = 56,
    PackUnorm2x16 = 57,
    PackHalf2x16 = 58,
    PackDouble2x32 = 59,
    UnpackSnorm2x16 = 60,
    UnpackUnorm2x16 = 61,
    UnpackHalf2x16 = 62,
    UnpackSnorm4x8 = 63,
    UnpackUnorm4x8 = 64,
    UnpackDouble2x32 = 65,

    Length = 66,
    Distance = 67,
    Cross = 68,
    Normalize = 69,
    FaceForward = 70,
    Reflect = 71,
    Refract = 72,

    FindILsb = 73,
    FindSMsb = 74,
    FindUMsb = 75,

    InterpolateAtCentroid = 76,
    InterpolateAtSample = 77,
    InterpolateAtOffset = 78,

    Count
};

}  // end namespace spv

#endif  // #ifndef GLSLstd450_H

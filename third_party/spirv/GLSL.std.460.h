/*
** Copyright (c) 2014-2016 The Khronos Group Inc.
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

#ifndef GLSLstd460_H
#define GLSLstd460_H

static const int GLSLstd460Version = 100;
static const int GLSLstd460Revision = 1;

enum GLSLstd460 {
    GLSLstd460Bad = 0,              // Don't use

    GLSLstd460Round = 1,
    GLSLstd460RoundEven = 2,
    GLSLstd460Trunc = 3,
    GLSLstd460FAbs = 4,
    GLSLstd460SAbs = 5,
    GLSLstd460FSign = 6,
    GLSLstd460SSign = 7,
    GLSLstd460Floor = 8,
    GLSLstd460Ceil = 9,
    GLSLstd460Fract = 10,

    GLSLstd460Radians = 11,
    GLSLstd460Degrees = 12,
    GLSLstd460Sin = 13,
    GLSLstd460Cos = 14,
    GLSLstd460Tan = 15,
    GLSLstd460Asin = 16,
    GLSLstd460Acos = 17,
    GLSLstd460Atan = 18,
    GLSLstd460Sinh = 19,
    GLSLstd460Cosh = 20,
    GLSLstd460Tanh = 21,
    GLSLstd460Asinh = 22,
    GLSLstd460Acosh = 23,
    GLSLstd460Atanh = 24,
    GLSLstd460Atan2 = 25,

    GLSLstd460Pow = 26,
    GLSLstd460Exp = 27,
    GLSLstd460Log = 28,
    GLSLstd460Exp2 = 29,
    GLSLstd460Log2 = 30,
    GLSLstd460Sqrt = 31,
    GLSLstd460InverseSqrt = 32,

    GLSLstd460Determinant = 33,
    GLSLstd460MatrixInverse = 34,

    GLSLstd460Modf = 35,            // second operand needs an OpVariable to write to
    GLSLstd460ModfStruct = 36,      // no OpVariable operand
    GLSLstd460FMin = 37,
    GLSLstd460UMin = 38,
    GLSLstd460SMin = 39,
    GLSLstd460FMax = 40,
    GLSLstd460UMax = 41,
    GLSLstd460SMax = 42,
    GLSLstd460FClamp = 43,
    GLSLstd460UClamp = 44,
    GLSLstd460SClamp = 45,
    GLSLstd460FMix = 46,
    GLSLstd460IMix = 47,            // Reserved
    GLSLstd460Step = 48,
    GLSLstd460SmoothStep = 49,

    GLSLstd460Fma = 50,
    GLSLstd460Frexp = 51,            // second operand needs an OpVariable to write to
    GLSLstd460FrexpStruct = 52,      // no OpVariable operand
    GLSLstd460Ldexp = 53,

    GLSLstd460PackSnorm4x8 = 54,
    GLSLstd460PackUnorm4x8 = 55,
    GLSLstd460PackSnorm2x16 = 56,
    GLSLstd460PackUnorm2x16 = 57,
    GLSLstd460PackHalf2x16 = 58,
    GLSLstd460PackDouble2x32 = 59,
    GLSLstd460UnpackSnorm2x16 = 60,
    GLSLstd460UnpackUnorm2x16 = 61,
    GLSLstd460UnpackHalf2x16 = 62,
    GLSLstd460UnpackSnorm4x8 = 63,
    GLSLstd460UnpackUnorm4x8 = 64,
    GLSLstd460UnpackDouble2x32 = 65,

    GLSLstd460Length = 66,
    GLSLstd460Distance = 67,
    GLSLstd460Cross = 68,
    GLSLstd460Normalize = 69,
    GLSLstd460FaceForward = 70,
    GLSLstd460Reflect = 71,
    GLSLstd460Refract = 72,

    GLSLstd460FindILsb = 73,
    GLSLstd460FindSMsb = 74,
    GLSLstd460FindUMsb = 75,

    GLSLstd460InterpolateAtCentroid = 76,
    GLSLstd460InterpolateAtSample = 77,
    GLSLstd460InterpolateAtOffset = 78,

    GLSLstd460NMin = 79,
    GLSLstd460NMax = 80,
    GLSLstd460NClamp = 82,

    GLSLstd460Count
};

#endif  // #ifndef GLSLstd450_H

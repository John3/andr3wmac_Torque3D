//-----------------------------------------------------------------------------
// Copyright (c) 2012 GarageGames, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#define blurSamples 4.0

uniform sampler2D diffuseMap0;
uniform float texSize;
uniform vec2 blurDimension;

VARYING vec2 tex0;

void main()
{
   // Preshader
   float TexelSize = 1.0 / texSize;
   vec2 SampleOffset = TexelSize * blurDimension;
   vec2 Offset = 0.5 * float(blurSamples - 1.0) * SampleOffset;
   
   vec2 BaseTexCoord = tex0 - Offset;
   
   vec4 accum = vec4(0.0, 0.0, 0.0, 0.0);
   for(int i = 0; i < int(blurSamples); i++)
   {
      accum += texture(diffuseMap0, BaseTexCoord + float(i) * SampleOffset);
   }
   accum /= blurSamples;
   OUT_FragColor0 = accum;
}
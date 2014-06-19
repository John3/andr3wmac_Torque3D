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

#include "platform/platform.h"
#include "lighting/advanced/hlsl/deferredShadingFeaturesHLSL.h"

#include "lighting/advanced/advancedLightBinManager.h"
#include "shaderGen/langElement.h"
#include "shaderGen/shaderOp.h"
#include "shaderGen/conditionerFeature.h"
#include "renderInstance/renderPrePassMgr.h"
#include "materials/processedMaterial.h"
#include "materials/materialFeatureTypes.h"


//****************************************************************************
// Deferred Shading Features
//****************************************************************************

// Diffuse Map -> Color Buffer
void DeferredDiffuseMapHLSL::processPix( Vector<ShaderComponent*> &componentList, const MaterialFeatureData &fd )
{
   // grab connector texcoord register
   Var *inTex = getInTexCoord( "texCoord", "float2", true, componentList );

   // create texture var
   Var *diffuseMap = new Var;
   diffuseMap->setType( "sampler2D" );
   diffuseMap->setName( "diffuseMap" );
   diffuseMap->uniform = true;
   diffuseMap->sampler = true;
   diffuseMap->constNum = Var::getTexUnitNum();     // used as texture unit num here

   LangElement *statement = new GenOp( "tex2D(@, @)", diffuseMap, inTex );

   output = new GenOp( "   @;\r\n", assignColor( statement, Material::None, NULL, ShaderFeature::RenderTarget2 ) );
}

ShaderFeature::Resources DeferredDiffuseMapHLSL::getResources( const MaterialFeatureData &fd )
{
   Resources res; 
   res.numTex = 1;
   res.numTexReg = 1;

   return res;
}

void DeferredDiffuseMapHLSL::setTexData(   Material::StageData &stageDat,
                                       const MaterialFeatureData &fd,
                                       RenderPassData &passData,
                                       U32 &texIndex )
{
   GFXTextureObject *tex = stageDat.getTex( MFT_DiffuseMap );
   if ( tex )
      passData.mTexSlot[ texIndex++ ].texObject = tex;
}

void DeferredDiffuseMapHLSL::processVert( Vector<ShaderComponent*> &componentList, 
                                       const MaterialFeatureData &fd )
{
   MultiLine *meta = new MultiLine;
   getOutTexCoord(   "texCoord", 
                     "float2", 
                     true, 
                     fd.features[MFT_TexAnim], 
                     meta, 
                     componentList );
   output = meta;
}

// Diffuse Color -> Color Buffer
void DeferredDiffuseColorHLSL::processPix( Vector<ShaderComponent*> &componentList, const MaterialFeatureData &fd )
{
   Var *diffuseMaterialColor  = new Var;
   diffuseMaterialColor->setType( "float4" );
   diffuseMaterialColor->setName( "diffuseMaterialColor" );
   diffuseMaterialColor->uniform = true;
   diffuseMaterialColor->constSortPos = cspPotentialPrimitive;

   MultiLine * meta = new MultiLine;
   meta->addStatement( new GenOp( "   @;\r\n", assignColor( diffuseMaterialColor, Material::Mul, NULL, ShaderFeature::RenderTarget2 ) ) );
   output = meta;
}

// Specular Map -> Lighting Buffer
void DeferredSpecMapHLSL::processPix( Vector<ShaderComponent*> &componentList, const MaterialFeatureData &fd )
{
   // Get the texture coord.
   Var *texCoord = getInTexCoord( "texCoord", "float2", true, componentList );

   // create texture var
   Var *specularMap = new Var;
   specularMap->setType( "sampler2D" );
   specularMap->setName( "specularMap" );
   specularMap->uniform = true;
   specularMap->sampler = true;
   specularMap->constNum = Var::getTexUnitNum();
   LangElement *texOp = new GenOp( "tex2D(@, @)", specularMap, texCoord );

   // output spec color
   output = new GenOp( "   @;\r\n", assignColor( texOp, Material::None, NULL, ShaderFeature::RenderTarget1 ) );
}

ShaderFeature::Resources DeferredSpecMapHLSL::getResources( const MaterialFeatureData &fd )
{
   Resources res; 
   res.numTex = 1;
   res.numTexReg = 1;

   return res;
}

void DeferredSpecMapHLSL::setTexData(   Material::StageData &stageDat,
                                       const MaterialFeatureData &fd,
                                       RenderPassData &passData,
                                       U32 &texIndex )
{
   GFXTextureObject *tex = stageDat.getTex( MFT_SpecularMap );
   if ( tex )
   {
      passData.mTexType[ texIndex ] = Material::Standard;
      passData.mTexSlot[ texIndex++ ].texObject = tex;
   }
}

void DeferredSpecMapHLSL::processVert( Vector<ShaderComponent*> &componentList, 
                                       const MaterialFeatureData &fd )
{
   MultiLine *meta = new MultiLine;
   getOutTexCoord(   "texCoord", 
                     "float2", 
                     true, 
                     fd.features[MFT_TexAnim], 
                     meta, 
                     componentList );
   output = meta;
}

// Specular Color -> Lighting Buffer
void DeferredSpecColorHLSL::processPix( Vector<ShaderComponent*> &componentList, const MaterialFeatureData &fd )
{
   Var *specularColor = (Var*)LangElement::find( "specularColor" );
   if ( !specularColor )
   {
      specularColor  = new Var( "specularColor", "float4" );
      specularColor->uniform = true;
      specularColor->constSortPos = cspPotentialPrimitive;
   }
   
   output = new GenOp( "   @;\r\n", assignColor( specularColor, Material::None, NULL,  ShaderFeature::RenderTarget1 ) );
}

// Black -> Lighting Buffer (representing no spec color)
void DeferredEmptySpecHLSL::processPix( Vector<ShaderComponent*> &componentList, const MaterialFeatureData &fd )
{
   output = new GenOp( "   @;\r\n", assignColor( new GenOp( "0.00001" ), Material::None, NULL,  ShaderFeature::RenderTarget1 ) );
}

// Spec Strength -> Alpha Channel of Color Buffer
void DeferredSpecStrengthHLSL::processPix( Vector<ShaderComponent*> &componentList, const MaterialFeatureData &fd )
{
   // search for color var
   Var *color = (Var*) LangElement::find( getOutputTargetVarName(ShaderFeature::RenderTarget2) );
   if ( !color )
   {
      // create color var
      color = new Var;
      color->setType( "fragout" );
      color->setName( getOutputTargetVarName(ShaderFeature::RenderTarget2) );
      color->setStructName( "OUT" );
   }

   Var *specStrength = new Var;
   specStrength->setType( "float" );
   specStrength->setName( "specularStrength" );
   specStrength->uniform = true;
   specStrength->constSortPos = cspPotentialPrimitive;

   output = new GenOp( "   @.a = 1.0 - @;\r\n", color, specStrength );
}

// Spec Power -> Alpha Channel of Lighting Buffer
void DeferredSpecPowerHLSL::processPix( Vector<ShaderComponent*> &componentList, const MaterialFeatureData &fd )
{
   // search for color var
   Var *color = (Var*) LangElement::find( getOutputTargetVarName(ShaderFeature::RenderTarget1) );
   if ( !color )
   {
      // create color var
      color = new Var;
      color->setType( "fragout" );
      color->setName( getOutputTargetVarName(ShaderFeature::RenderTarget1) );
      color->setStructName( "OUT" );
   }

   Var *specPower = new Var;
   specPower->setType( "float" );
   specPower->setName( "specularPower" );
   specPower->uniform = true;
   specPower->constSortPos = cspPotentialPrimitive;

   output = new GenOp( "   @.a = 1.0 - @;\r\n", color, specPower );
}
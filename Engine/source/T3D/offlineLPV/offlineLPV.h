//-----------------------------------------------------------------------------
// Copyright (c) 2015 Andrew Mac
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

#ifndef _OFFLINELPV_H_
#define _OFFLINELPV_H_

#ifndef _SCENEPOLYHEDRALSPACE_H_
#include "scene/scenePolyhedralSpace.h"
#endif

#ifndef _MSILHOUETTEEXTRACTOR_H_
#include "math/mSilhouetteExtractor.h"
#endif

#ifndef _GFXDEVICE_H_
#include "gfx/gfxDevice.h"
#endif

#define LPV_GRID_RESOLUTION 10

/// A volume in space that blocks visibility.
class OfflineLPV : public ScenePolyhedralSpace
{
   public:

      typedef ScenePolyhedralSpace Parent;

   protected:

      bool mGeometryGrid[LPV_GRID_RESOLUTION][LPV_GRID_RESOLUTION][LPV_GRID_RESOLUTION];
      ColorF mLightGrid[LPV_GRID_RESOLUTION][LPV_GRID_RESOLUTION][LPV_GRID_RESOLUTION];
      ColorF mPropagatedLightGrid[LPV_GRID_RESOLUTION][LPV_GRID_RESOLUTION][LPV_GRID_RESOLUTION];

      typedef SilhouetteExtractorPerspective< PolyhedronType > SilhouetteExtractorType;



      /// Whether the volume's transform has changed and we need to recompute
      /// transform-based data.
      bool mTransformDirty;

      /// World-space points of the volume's polyhedron.
      Vector< Point3F > mWSPoints;

      /// Silhouette extractor when using perspective projections.
      SilhouetteExtractorType mSilhouetteExtractor;
      
      mutable Vector< SceneObject* > mVolumeQueryList;
      
      // SceneSpace.
      virtual void _renderObject( ObjectRenderInst* ri, SceneRenderState* state, BaseMatInstance* overrideMat );

   public:

      OfflineLPV();
      ~OfflineLPV();

      bool mRegenVolume;
      bool mInjectLights;
      bool mPropagateLights;
      bool mShowLightGrid;
      bool mShowPropagatedLightGrid;

      // SimObject.
      DECLARE_CONOBJECT( OfflineLPV );
      DECLARE_DESCRIPTION( "Offline Light Propogation Volume" );
      DECLARE_CATEGORY( "3D Scene" );

      virtual bool onAdd();
      virtual void onRemove();
      void inspectPostApply();
      void regenVolume();
      void injectLights();
      ColorF calcLightColor(Point3F position);
      F32 getAttenuation(LightInfo* lightInfo, Point3F position);
      void propagateLights();

      // Static Functions.
      static void consoleInit();
      static void initPersistFields();

      // Network
      U32 packUpdate( NetConnection *, U32 mask, BitStream *stream );
      void unpackUpdate( NetConnection *, BitStream *stream );

      // SceneObject.
      virtual void buildSilhouette( const SceneCameraState& cameraState, Vector< Point3F >& outPoints );
      virtual void setTransform( const MatrixF& mat );

      static bool _setRegenVolume( void *object, const char *index, const char *data );
      static bool _setInjectLights( void *object, const char *index, const char *data );
      static bool _setPropagateLights( void *object, const char *index, const char *data );
};

#endif // !_AccumulationVolume_H_

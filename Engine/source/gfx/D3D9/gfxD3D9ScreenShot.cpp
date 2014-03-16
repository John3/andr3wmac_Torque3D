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

//-----------------------------------------------------------------------------
// Partial refactor by: Anis A. Hireche (C) 2014 - anishireche@gmail.com
//-----------------------------------------------------------------------------

#include "platform/platform.h"
#include "gfx/D3D9/gfxD3D9screenShot.h"
#include "gfx/D3D9/gfxD3D9Device.h"

GBitmap* GFXD3D9ScreenShot::_captureBackBuffer()
{
   IDirect3DSurface9 * backBuffer;
   static_cast<GFXD3D9Device *>(GFX)->getDevice()->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer );

   // Figure the size we're snagging.
   D3DSURFACE_DESC desc;
   backBuffer->GetDesc(&desc);

   Point2I size;
   size.x = desc.Width;
   size.y = desc.Height;

   // set up the 2 copy surfaces
   GFXTexHandle tex[2];
   IDirect3DSurface9 *surface[2];

   tex[0].set( size.x, size.y, GFXFormatR8G8B8X8, &GFXDefaultRenderTargetProfile, avar("%s() - tex[0] (line %d)", __FUNCTION__, __LINE__) );
   tex[1].set( size.x, size.y, GFXFormatR8G8B8X8, &GFXSystemMemProfile, avar("%s() - tex[1] (line %d)", __FUNCTION__, __LINE__) );

   // grab the top level surface of tex 0
   GFXD3D9TextureObject *to = (GFXD3D9TextureObject *) &(*tex[0]);
   HRESULT hr = to->get2DTex()->GetSurfaceLevel( 0, &surface[0] );

	if(FAILED(hr)) 
	{
		AssertFatal(false, "GetSurfaceLevel call failure");
	}

   // use StretchRect because it allows a copy from a multisample surface
   // to a normal rendertarget surface
   static_cast<GFXD3D9Device *>(GFX)->getDevice()->StretchRect(backBuffer, NULL, surface[0], NULL, D3DTEXF_NONE);

   // grab the top level surface of tex 1
   to = (GFXD3D9TextureObject *) &(*tex[1]);
   hr = to->get2DTex()->GetSurfaceLevel( 0, &surface[1] );

	if(FAILED(hr)) 
	{
		AssertFatal(false, "GetSurfaceLevel call failure");
	}

   // copy the data from the render target to the system memory texture
   static_cast<GFXD3D9Device *>(GFX)->getDevice()->GetRenderTargetData(surface[0], surface[1]);

   // Allocate a GBitmap and copy into it.
   GBitmap *gb = new GBitmap(size.x, size.y);

   D3DLOCKED_RECT r;
   D3DSURFACE_DESC d;
   surface[1]->GetDesc(&d);
   surface[1]->LockRect( &r, NULL, D3DLOCK_READONLY);

   // We've got the X8 in there so we have to manually copy stuff.
   ColorI c;
   for(S32 i=0; i<size.y; i++)
   {
      const U8 *a = ((U8*)r.pBits) + i * size.x * 4;
      for(S32 j=0; j<size.x; j++)
      {
         c.blue  = *(a++);
         c.green = *(a++);
         c.red   = *(a++);
         a++; // Ignore X.

         gb->setColor(j, i, c);
      }
   }

   surface[1]->UnlockRect();

   surface[0]->Release();
   surface[1]->Release();
   backBuffer->Release();

   return gb;
}


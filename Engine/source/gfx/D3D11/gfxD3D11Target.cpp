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
// D3D11 layer created by: Anis A. Hireche (C) 2014 - anishireche@gmail.com
//-----------------------------------------------------------------------------

#include "platform/platform.h"
#include "gfx/D3D11/gfxD3D11Target.h"
#include "gfx/D3D11/gfxD3D11Cubemap.h"
#include "gfx/D3D11/gfxD3D11EnumTranslate.h"
#include "gfx/gfxDebugEvent.h"
#include "gfx/gfxStringEnumTranslate.h"
#include "windowManager/win32/win32Window.h"

GFXD3D11TextureTarget::GFXD3D11TextureTarget() 
   :  mTargetSize( Point2I::Zero ),
      mTargetFormat( GFXFormatR8G8B8A8 )
{
   for(S32 i=0; i<MaxRenderSlotId; i++)
   {
      mTargets[i] = NULL;
      mResolveTargets[i] = NULL;
   }
}

GFXD3D11TextureTarget::~GFXD3D11TextureTarget()
{
   // Release anything we might be holding.
   for(S32 i=0; i<MaxRenderSlotId; i++)
   {
      mResolveTargets[i] = NULL;
      SAFE_RELEASE(mTargets[i]);
   }

   zombify();
}

void GFXD3D11TextureTarget::attachTexture( RenderSlot slot, GFXTextureObject *tex, U32 mipLevel/*=0*/, U32 zOffset /*= 0*/ )
{
   GFXDEBUGEVENT_SCOPE( GFXPCD3D11TextureTarget_attachTexture, ColorI::RED );

   AssertFatal(slot < MaxRenderSlotId, "GFXD3D11TextureTarget::attachTexture - out of range slot.");

   // TODO:  The way this is implemented... you can attach a texture 
   // object multiple times and it will release and reset it.
   //
   // We should rework this to detect when no change has occured
   // and skip out early.

   // Mark state as dirty so device can know to update.
   invalidateState();

   // Release what we had, it's definitely going to change.
   SAFE_RELEASE(mTargets[slot]);
   mTargets[slot] = NULL;
   mResolveTargets[slot] = NULL;

   if(slot == Color0)
   {
      mTargetSize = Point2I::Zero;
      mTargetFormat = GFXFormatR8G8B8A8;
   }

   // Are we clearing?
   if(!tex)
   {
      // Yup - just exit, it'll stay NULL.      
      return;
   }

   // Take care of default targets
   if( tex == GFXTextureTarget::sDefaultDepthStencil )
   {
      mTargets[slot] = static_cast<GFXD3D11Device*>(GFX)->mDeviceDepthStencil;
      mTargets[slot]->AddRef();
   }
   else
   {
      // Cast the texture object to D3D...
      AssertFatal(static_cast<GFXD3D11TextureObject*>(tex), "GFXD3D11TextureTarget::attachTexture - invalid texture object.");

      GFXD3D11TextureObject *d3dto = static_cast<GFXD3D11TextureObject*>(tex);

      // Grab the surface level.
      if( slot == DepthStencil )
      {
         mTargets[slot] = d3dto->getSurface();
         if ( mTargets[slot] )
            mTargets[slot]->AddRef();
      }
      else
      {
         // getSurface will almost always return NULL. It will only return non-NULL
         // if the surface that it needs to render to is different than the mip level
         // in the actual texture. This will happen with MSAA.
         if( d3dto->getSurface() == NULL )
         {
			D3D11_TEXTURE2D_DESC SourceSurfaceDesc;
			//SourceSurface->Resource->GetDesc(&SourceSurfaceDesc);

			//mipLevel, &mTargets[slot]

            d3dto->get2DTex()->GetDesc(&SourceSurfaceDesc);

			//static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->ResolveSubresource(
         } 
         else 
         {
            mTargets[slot] = d3dto->getSurface();
            mTargets[slot]->AddRef();

            // Only assign resolve target if d3dto has a surface to give us.
            //
            // That usually means there is an MSAA target involved, which is why
            // the resolve is needed to get the data out of the target.
            mResolveTargets[slot] = d3dto;

            if ( tex && slot == Color0 )
            {
               mTargetSize.set( tex->getSize().x, tex->getSize().y );
               mTargetFormat = tex->getFormat();
            }
         }           
      }

      // Update surface size
      if(slot == Color0)
      {
         ID3D11Texture2D *surface = mTargets[Color0];
         if ( surface )
         {
			D3D11_TEXTURE2D_DESC sd;
            surface->GetDesc(&sd);
            mTargetSize = Point2I(sd.Width, sd.Height);

            S32 format = sd.Format;
            GFXREVERSE_LOOKUP( GFXD3D11TextureFormat, GFXFormat, format );
            mTargetFormat = (GFXFormat)format;
         }
      }
   }
}

void GFXD3D11TextureTarget::attachTexture( RenderSlot slot, GFXCubemap *tex, U32 face, U32 mipLevel/*=0*/ )
{
   GFXDEBUGEVENT_SCOPE( GFXPCD3D11TextureTarget_attachTexture_Cubemap, ColorI::RED );

   AssertFatal(slot < MaxRenderSlotId, "GFXD3D11TextureTarget::attachTexture - out of range slot.");

   // Mark state as dirty so device can know to update.
   invalidateState();

   // Release what we had, it's definitely going to change.
   SAFE_RELEASE(mTargets[slot]);
   mTargets[slot] = NULL;
   mResolveTargets[slot] = NULL;

   // Cast the texture object to D3D...
   AssertFatal(!tex || static_cast<GFXD3D11Cubemap*>(tex), "GFXD3DTextureTarget::attachTexture - invalid cubemap object.");

   //GFXD3D11Cubemap *cube = static_cast<GFXD3D11Cubemap*>(tex);

   if(slot == Color0)
   {
      mTargetSize = Point2I::Zero;
      mTargetFormat = GFXFormatR8G8B8A8;
   }

   // Are we clearing?
   if(!tex)
   {
      // Yup - just exit, it'll stay NULL.      
      return;
   }

    //HRESULT hr = cube->mCubeTex->GetCubeMapSurface( (D3DCUBEMAP_FACES)face, mipLevel, &mTargets[slot] );

	//if(FAILED(hr)) 
	//{
		//AssertFatal(false, "GFXD3DTextureTarget::attachTexture - could not get surface level for the passed texture!");
	//}

   // Update surface size
   if(slot == Color0)
   {
      ID3D11Texture2D *surface = mTargets[Color0];
      if ( surface )
      {
         D3D11_TEXTURE2D_DESC sd;
         surface->GetDesc(&sd);
         mTargetSize = Point2I(sd.Width, sd.Height);

         S32 format = sd.Format;
         GFXREVERSE_LOOKUP( GFXD3D11TextureFormat, GFXFormat, format );
         mTargetFormat = (GFXFormat)format;
      }
   }
}

void GFXD3D11TextureTarget::activate()
{
   GFXDEBUGEVENT_SCOPE( GFXPCD3D11TextureTarget_activate, ColorI::RED );

   AssertFatal( mTargets[GFXTextureTarget::Color0], "GFXD3D11TextureTarget::activate() - You can never have a NULL primary render target!" );

   // Clear the state indicator.
   stateApplied();

   //ID3D11Texture2D *depth = mTargets[GFXTextureTarget::DepthStencil];
   
   // First clear the non-primary targets to make the debug DX runtime happy.
   for(U32 i = 1; i < static_cast<GFXD3D11Device*>(GFX)->getNumRenderTargets(); i++)
   {
		//HRESULT hr = static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->OMSetRenderTargets(i, NULL);

		//if(FAILED(hr)) 
		//{
			//AssertFatal(false, avar("GFXD3D11TextureTarget::activate() - failed to clear texture target %d!", i));
		//}
   }

   // Now set all the new surfaces into the appropriate slots.
   for(U32 i = 0; i < static_cast<GFXD3D11Device*>(GFX)->getNumRenderTargets(); i++)
   {
      ID3D11Texture2D *target = mTargets[GFXTextureTarget::Color0 + i];
      if(target)
      {
         //HRESULT hr = static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->OMSetRenderTargets(i, target);

		 //if(FAILED(hr)) 
		 //{
		 	//AssertFatal(false, avar("GFXD3D11TextureTarget::activate() - failed to set slot %d for texture target!", i));
		 //}
      }
   }

   // TODO: This is often the same shared depth buffer used by most
   // render targets.  Are we getting performance hit from setting it
   // multiple times... aside from the function call?

   //HRESULT hr = static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->SetDepthStencilSurface(depth);

   //if(FAILED(hr)) 
   //{
	   //AssertFatal(false, "GFXD3D11TextureTarget::activate() - failed to set depthstencil target!");
   //}
}

void GFXD3D11TextureTarget::deactivate()
{
   // Nothing to do... the next activate() call will
   // set all the targets correctly.
}

void GFXD3D11TextureTarget::resolve()
{
   GFXDEBUGEVENT_SCOPE( GFXPCD3D11TextureTarget_resolve, ColorI::RED );

   for (U32 i = 0; i < MaxRenderSlotId; i++)
   {
      // We use existance @ mResolveTargets as a flag that we need to copy
      // data from the rendertarget into the texture.
      if (mResolveTargets[i])
      {
         //ID3D11Texture2D *surf;
         //HRESULT hr = mResolveTargets[i]->get2DTex()->GetSurfaceLevel( 0, &surf );

		 //if(FAILED(hr)) 
		 //{
			//AssertFatal(false, "GFXD3D11TextureTarget::resolve() - GetSurfaceLevel failed!");
		 //}

		 //static_cast<GFXD3D11Device*>( GFX )->getDeviceContext()->CopyResource( surf, mTargets[i] );

         //surf->Release();
      }
   }
}

void GFXD3D11TextureTarget::resolveTo( GFXTextureObject *tex )
{
   GFXDEBUGEVENT_SCOPE( GFXPCD3D11TextureTarget_resolveTo, ColorI::RED );

   if ( mTargets[Color0] == NULL )
      return;

    //IDirect3DSurface9 *surf;
    //HRESULT hr = ((GFXD3D11TextureObject*)(tex))->get2DTex()->GetSurfaceLevel( 0, &surf );

	//if(FAILED(hr)) 
	//{
		//AssertFatal(false, "GFXD3D11TextureTarget::resolveTo() - GetSurfaceLevel failed!");
	//}

    //hr = static_cast<GFXD3D11Device*>( GFX )->getDevice()->StretchRect( mTargets[Color0], NULL, surf, NULL, D3DTEXF_NONE );

	//if(FAILED(hr)) 
	//{
		//AssertFatal(false, "GFXD3D11TextureTarget::resolveTo() - StretchRect failed!");
	//}

   //surf->Release();     
}

void GFXD3D11TextureTarget::zombify()
{
   for(int i = 0; i < MaxRenderSlotId; i++)
      attachTexture(RenderSlot(i), NULL);
}

void GFXD3D11TextureTarget::resurrect()
{

}

GFXD3D11WindowTarget::GFXD3D11WindowTarget(IDXGISwapChain* SwapChain)
{
   mSwapChain = SwapChain;
   mSwapChain->AddRef();

   mDepthStencil = NULL;
   mWindow       = NULL;
   mBackbuffer   = NULL;
}

GFXD3D11WindowTarget::~GFXD3D11WindowTarget()
{
   SAFE_RELEASE(mSwapChain);
   SAFE_RELEASE(mDepthStencil);
   SAFE_RELEASE(mBackbuffer);
}

void GFXD3D11WindowTarget::initPresentationParams()
{
   // Get some video mode related info.
   GFXVideoMode vm = mWindow->getVideoMode();

   Win32Window* win = static_cast<Win32Window*>(mWindow);

   HWND hwnd = win->getHWND();

   // At some point, this will become GFXD3D11WindowTarget like trunk has,
   // so this cast isn't as bad as it looks. ;) BTR
   mPresentationParams = static_cast<GFXD3D11Device*>(GFX)->setupPresentParams(vm, hwnd);

   static_cast<GFXD3D11Device*>(GFX)->mMultisampleDesc.Count = mPresentationParams.SampleDesc.Count;
   static_cast<GFXD3D11Device*>(GFX)->mMultisampleDesc.Quality = mPresentationParams.SampleDesc.Quality;
}

const Point2I GFXD3D11WindowTarget::getSize()
{
   return mWindow->getVideoMode().resolution; 
}

GFXFormat GFXD3D11WindowTarget::getFormat()
{ 
   S32 format = mPresentationParams.BufferDesc.Format;
   GFXREVERSE_LOOKUP( GFXD3D11TextureFormat, GFXFormat, format );
   return (GFXFormat)format;
}

bool GFXD3D11WindowTarget::present()
{
   AssertFatal(mSwapChain, "GFXD3D11WindowTarget::present - no swap chain present to present!");
   HRESULT res = mSwapChain->Present(!static_cast<GFXD3D11Device*>(GFX)->smDisableVSync, 0);

   return SUCCEEDED(res);
}

void GFXD3D11WindowTarget::setImplicitSwapChain()
{
   if(!mSwapChain)
      //static_cast<GFXD3D11Device*>(GFX)->getDevice()->GetSwapChain(0, &mSwapChain);
   if(!mDepthStencil)
      //static_cast<GFXD3D11Device*>(GFX)->getDevice()->GetDepthStencilSurface(&mDepthStencil);
   if (!mBackbuffer)      
      mSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&mBackbuffer);
}

void GFXD3D11WindowTarget::resetMode()
{
   mWindow->setSuppressReset(true);

   if (mSwapChain)
   {
      // The current video settings.
      DXGI_SWAP_CHAIN_DESC pp;
      mSwapChain->GetDesc(&pp);      
      bool ppFullscreen = !pp.Windowed;
	  Point2I backbufferSize(pp.BufferDesc.Width, pp.BufferDesc.Height);

      // The settings we are now applying.
      const GFXVideoMode &mode = mWindow->getVideoMode();
      
      // Convert the current multisample parameters into something
      // we can compare with our GFXVideoMode.antialiasLevel value.
      U32 ppAntiAliaseLevel = 0;
	  if ( pp.SampleDesc.Count != 0 )      
         ppAntiAliaseLevel = pp.SampleDesc.Quality + 1;

      // Early out if none of the settings which require a device reset
      // have changed.      
      if ( backbufferSize == getSize() && ppFullscreen == mode.fullScreen && ppAntiAliaseLevel == mode.antialiasLevel )
         return;   
   }

   // Setup our presentation params.
   initPresentationParams();

   // Otherwise, we have to reset the device, if we're the implicit swapchain.
   static_cast<GFXD3D11Device*>(GFX)->reset(mPresentationParams);

   // Update our size, too.
   mSize = Point2I(mPresentationParams.BufferDesc.Width, mPresentationParams.BufferDesc.Height);      

   mWindow->setSuppressReset(false);
}

void GFXD3D11WindowTarget::zombify()
{
   // Release our resources
   SAFE_RELEASE(mSwapChain);
   SAFE_RELEASE(mDepthStencil);
   SAFE_RELEASE(mBackbuffer);
}

void GFXD3D11WindowTarget::resurrect()
{
   setImplicitSwapChain();
}

void GFXD3D11WindowTarget::activate()
{
   GFXDEBUGEVENT_SCOPE( GFXPCD3D11WindowTarget_activate, ColorI::RED );
   /*
   HRESULT hr;

   hr = static_cast<GFXD3D11Device*>(GFX)->getDevice()->SetRenderTarget(0, mBackbuffer);

   if(FAILED(hr)) 
   {
      AssertFatal(false, "GFXD3D11WindowTarget::activate() - Failed to set backbuffer target!");
   }

   hr = static_cast<GFXD3D11Device*>(GFX)->getDevice()->SetDepthStencilSurface(mDepthStencil);

   if(FAILED(hr)) 
   {
      AssertFatal(false, "GFXD3D11WindowTarget::activate() - Failed to set depthstencil target!");
   }
   */

   DXGI_SWAP_CHAIN_DESC pp;
   mSwapChain->GetDesc(&pp);

   // Update our video mode here, too.
   GFXVideoMode vm;
   vm = mWindow->getVideoMode();
   vm.resolution.x = pp.BufferDesc.Width;
   vm.resolution.y = pp.BufferDesc.Height;
   vm.fullScreen = !pp.Windowed;
   mSize = vm.resolution;
}

void GFXD3D11WindowTarget::resolveTo(GFXTextureObject *tex)
{
	GFXDEBUGEVENT_SCOPE( GFXPCD3D11WindowTarget_resolveTo, ColorI::RED );

	//IDirect3DSurface9 *surf;
	//HRESULT hr;

	//hr = ((GFXD3D11TextureObject*)(tex))->get2DTex()->GetSurfaceLevel(0, &surf);

	//if(FAILED(hr)) 
	//{
		//AssertFatal(false, "GFXD3D11WindowTarget::resolveTo() - GetSurfaceLevel failed!");
	//}

	//hr = static_cast<GFXD3D11Device*>(GFX)->getDevice()->StretchRect(mBackbuffer, NULL, surf, NULL, D3DTEXF_NONE);

	//if(FAILED(hr)) 
	//{
		//AssertFatal(false, "GFXD3D11WindowTarget::resolveTo() - StretchRect failed!");
	//}

	//surf->Release();    
}
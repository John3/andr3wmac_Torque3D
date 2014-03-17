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
#include "gfx/D3D11/gfxD3D11Shader.h"
#include "core/frameAllocator.h"
#include "core/stream/fileStream.h"
#include "core/util/safeDelete.h"
#include "console/console.h"

/*
	anis -> note:

	To use shaders we need support of shader model 3 (directx9 level).
	Today almost any videocard support it.
	
	from GFXD3D11Device::init:

	Platform::AlertOK("DirectX Error!", "Failed to initialize Direct3D! Make sure you have DirectX 9 installed, and are running a graphics card that supports Shader Model 3.");
	So, if garage games already say this, it's ok! :D
*/

extern bool gDisassembleAllShaders;

#pragma comment(lib, "d3dcompiler.lib")

gfxD3DIncludeRef GFXD3D11Shader::smD3DInclude = NULL;

HRESULT gfxD3D11Include::Open(THIS_ D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes)
{
   using namespace Torque;
   // First try making the path relative to the parent.
   Torque::Path path = Torque::Path::Join( mLastPath.last(), '/', pFileName );
   path = Torque::Path::CompressPath( path );

   if ( !Torque::FS::ReadFile( path, (void *&)*ppData, *pBytes, true ) )
   {
      // Ok... now try using the path as is.
      path = String( pFileName );
      path = Torque::Path::CompressPath( path );

      if ( !Torque::FS::ReadFile( path, (void *&)*ppData, *pBytes, true ) )
      {
         AssertISV(false, avar( "Failed to open include '%s'.", pFileName));
         return E_FAIL;
      }
   }

   // If the data was of zero size then we cannot recurse
   // into this file and DX won't call Close() below.
   //
   // So in this case don't push on the path.
   if ( *pBytes > 0 )
      mLastPath.push_back( path.getRootAndPath() );

   return S_OK;
}

HRESULT gfxD3D11Include::Close( THIS_ LPCVOID pData )
{
   // Free the data file and pop its path off the stack.
   delete [] (U8*)pData;
   mLastPath.pop_back();

   return S_OK;
}

GFXD3D11ShaderConstHandle::GFXD3D11ShaderConstHandle()
{
   clear();
}

const String& GFXD3D11ShaderConstHandle::getName() const
{
   if ( mVertexConstant )
      return mVertexHandle.name;
   else
      return mPixelHandle.name;
}

GFXShaderConstType GFXD3D11ShaderConstHandle::getType() const
{
   if ( mVertexConstant )
      return mVertexHandle.constType;
   else
      return mPixelHandle.constType;
}

U32 GFXD3D11ShaderConstHandle::getArraySize() const
{
   if ( mVertexConstant )
      return mVertexHandle.arraySize;
   else
      return mPixelHandle.arraySize;
}

S32 GFXD3D11ShaderConstHandle::getSamplerRegister() const
{
   if ( !mValid || !isSampler() )
      return -1;

   // We always store sampler type and register index in the pixelHandle,
   // sampler registers are shared between vertex and pixel shaders anyway.

   return mPixelHandle.offset;   
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------


bool GFXD3D11ShaderBufferLayout::setMatrix(const ParamDesc& pd, const GFXShaderConstType constType, const U32 size, const void* data, U8* basePointer)
{
   PROFILE_SCOPE(GFXD3D11ShaderBufferLayout_setMatrix);

   if (pd.constType == GFXSCT_Float4x4)
   {
      // Special case, we can just blast this guy.
      AssertFatal(pd.size >= size, "Not enough room in the buffer for this data!");
      if (dMemcmp(basePointer+pd.offset, data, size) != 0)
      {
         dMemcpy(basePointer+pd.offset, data, size);         
         return true;
      }

      return false;
   }
   else
   {
      PROFILE_SCOPE(GFXD3D11ShaderBufferLayout_setMatrix_not4x4);

      // Figure out how big of a chunk we are copying.  We're going to copy 4 columns by N rows of data
      U32 csize;
      switch (pd.constType)
      {
      case GFXSCT_Float2x2 :
         csize = 32;
         break;
      case GFXSCT_Float3x3 :
         csize = 48;
         break;
      default:
         AssertFatal(false, "Unhandled case!");
         return false;
         break;
      }

      // Loop through and copy 
      bool ret = false;
      U8* currDestPointer = basePointer+pd.offset;
      const U8* currSourcePointer = static_cast<const U8*>(data);
      const U8* endData = currSourcePointer + size;
      while (currSourcePointer < endData)
      {
         if (dMemcmp(currDestPointer, currSourcePointer, csize) != 0)
         {
            dMemcpy(currDestPointer, currSourcePointer, csize);            
            ret = true;
         }

         currDestPointer += csize;
         currSourcePointer += sizeof(MatrixF);
      }

      return ret;
   }
}

//------------------------------------------------------------------------------
GFXD3D11ShaderConstBuffer::GFXD3D11ShaderConstBuffer( GFXD3D11Shader* shader, 
                                                    GFXD3D11ShaderBufferLayout* vertexLayoutF, 
                                                    GFXD3D11ShaderBufferLayout* vertexLayoutI, 
                                                    GFXD3D11ShaderBufferLayout* pixelLayoutF, 
                                                    GFXD3D11ShaderBufferLayout* pixelLayoutI ) 
{
    AssertFatal( shader, "GFXD3D11ShaderConstBuffer() - Got a null shader!" );

    // We hold on to this so we don't have to call
    // this virtual method during activation.

    mShader = shader;

    // TODO: Remove buffers and layouts that don't exist for performance?

    mVertexConstBufferLayoutF = vertexLayoutF;
    mVertexConstBufferF = new GenericConstBuffer(vertexLayoutF);   
    mVertexConstBufferLayoutI = vertexLayoutI;
    mVertexConstBufferI = new GenericConstBuffer(vertexLayoutI);   
    mPixelConstBufferLayoutF = pixelLayoutF;
    mPixelConstBufferF = new GenericConstBuffer(pixelLayoutF);   
    mPixelConstBufferLayoutI = pixelLayoutI;
    mPixelConstBufferI = new GenericConstBuffer(pixelLayoutI);   

	HRESULT hr;

	// Create a vertex float constant buffer
	D3D11_BUFFER_DESC cbDescVF;
	cbDescVF.ByteWidth = mVertexConstBufferLayoutF->getBufferSize();
	cbDescVF.Usage = D3D11_USAGE_DYNAMIC;
	cbDescVF.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDescVF.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDescVF.MiscFlags = 0;
	cbDescVF.StructureByteStride = 0;

	hr = static_cast<GFXD3D11Device*>(GFX)->getDevice()->CreateBuffer(&cbDescVF, NULL, &mConstantBuffersVF);

	if(FAILED(hr))
	{
		AssertFatal(false, "can't create constant mConstantBuffersVF!");
	}

	// Create a vertex integer constant buffer
	D3D11_BUFFER_DESC cbDescVI;
	cbDescVI.ByteWidth = mVertexConstBufferLayoutI->getBufferSize();
	cbDescVI.Usage = D3D11_USAGE_DYNAMIC;
	cbDescVI.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDescVI.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDescVI.MiscFlags = 0;
	cbDescVI.StructureByteStride = 0;

	hr = static_cast<GFXD3D11Device*>(GFX)->getDevice()->CreateBuffer(&cbDescVI, NULL, &mConstantBuffersVI);

	if(FAILED(hr))
	{
		AssertFatal(false, "can't create constant mConstantBuffersVI!");
	}

	// Create a pixel float constant buffer
	D3D11_BUFFER_DESC cbDescPF;
	cbDescPF.ByteWidth = mPixelConstBufferLayoutF->getBufferSize();
	cbDescPF.Usage = D3D11_USAGE_DYNAMIC;
	cbDescPF.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDescPF.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDescPF.MiscFlags = 0;
	cbDescPF.StructureByteStride = 0;

	hr = static_cast<GFXD3D11Device*>(GFX)->getDevice()->CreateBuffer(&cbDescPF, NULL, &mConstantBuffersPF);

	if(FAILED(hr))
	{
		AssertFatal(false, "can't create constant mConstantBuffersPF!");
	}

	// Create a pixel integer constant buffer
	D3D11_BUFFER_DESC cbDescPI;
	cbDescPI.ByteWidth = mPixelConstBufferLayoutI->getBufferSize();
	cbDescPI.Usage = D3D11_USAGE_DYNAMIC;
	cbDescPI.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDescPI.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDescPI.MiscFlags = 0;
	cbDescPI.StructureByteStride = 0;

	hr = static_cast<GFXD3D11Device*>(GFX)->getDevice()->CreateBuffer(&cbDescPI, NULL, &mConstantBuffersPI);

	if(FAILED(hr))
	{
		AssertFatal(false, "can't create constant mConstantBuffersPI!");
	}
}

GFXD3D11ShaderConstBuffer::~GFXD3D11ShaderConstBuffer()
{   
   // release constant buffer
   SAFE_RELEASE(mConstantBuffersPI);
   SAFE_RELEASE(mConstantBuffersPF);
   SAFE_RELEASE(mConstantBuffersVI);
   SAFE_RELEASE(mConstantBuffersVF);

   SAFE_DELETE(mVertexConstBufferF);
   SAFE_DELETE(mPixelConstBufferF);
   SAFE_DELETE(mVertexConstBufferI);
   SAFE_DELETE(mPixelConstBufferI);

   if ( mShader )
      mShader->_unlinkBuffer( this );
}

GFXShader* GFXD3D11ShaderConstBuffer::getShader()
{
   return mShader;
}

// This is kind of cheesy, but I don't think templates would work well here because 
// these functions potentially need to be handled differently by other derived types
template<class T>
inline void GFXD3D11ShaderConstBuffer::SET_CONSTANT( GFXShaderConstHandle* handle, const T& fv, GenericConstBuffer *vBuffer, GenericConstBuffer *pBuffer )
{
   AssertFatal(static_cast<const GFXD3D11ShaderConstHandle*>(handle), "Incorrect const buffer type!");
   const GFXD3D11ShaderConstHandle* h = static_cast<const GFXD3D11ShaderConstHandle*>(handle);
   AssertFatal(h, "Handle is NULL!" );
   AssertFatal(h->isValid(), "Handle is not valid!" );
   AssertFatal(!h->isSampler(), "Handle is sampler constant!" );
   AssertFatal(!mShader.isNull(), "Buffer's shader is null!" );
   AssertFatal(!h->mShader.isNull(), "Handle's shader is null!" );
   AssertFatal(h->mShader.getPointer() == mShader.getPointer(), "Mismatched shaders!");
   if ( h->mInstancingConstant )
   {
      dMemcpy( mInstPtr+h->mPixelHandle.offset, &fv, sizeof( fv ) );
      return;
   }
   if (h->mVertexConstant)
      vBuffer->set(h->mVertexHandle, fv);
   if (h->mPixelConstant)
      pBuffer->set(h->mPixelHandle, fv);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const F32 fv) 
{
   SET_CONSTANT(handle, fv, mVertexConstBufferF, mPixelConstBufferF);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const Point2F& fv) 
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferF, mPixelConstBufferF);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const Point3F& fv) 
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferF, mPixelConstBufferF);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const Point4F& fv) 
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferF, mPixelConstBufferF);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const PlaneF& fv) 
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferF, mPixelConstBufferF);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const ColorF& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferF, mPixelConstBufferF);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const S32 f)
{ 
   // This is the only type that is allowed to be used
   // with a sampler shader constant type, but it is only
   // allowed to be set from GLSL.
   //
   // So we ignore it here... all other cases will assert.
   //
   if ( ((GFXD3D11ShaderConstHandle*)handle)->isSampler() )
      return;

   SET_CONSTANT(handle, f, mVertexConstBufferI, mPixelConstBufferI);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const Point2I& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferI, mPixelConstBufferI);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const Point3I& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferI, mPixelConstBufferI);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const Point4I& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferI, mPixelConstBufferI);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const AlignedArray<F32>& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferF, mPixelConstBufferF);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const AlignedArray<Point2F>& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferF, mPixelConstBufferF);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const AlignedArray<Point3F>& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferF, mPixelConstBufferF);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const AlignedArray<Point4F>& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferF, mPixelConstBufferF);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const AlignedArray<S32>& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferI, mPixelConstBufferI);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const AlignedArray<Point2I>& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferI, mPixelConstBufferI);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const AlignedArray<Point3I>& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferI, mPixelConstBufferI);
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const AlignedArray<Point4I>& fv)
{ 
   SET_CONSTANT(handle, fv, mVertexConstBufferI, mPixelConstBufferI);
}
#undef SET_CONSTANT

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const MatrixF& mat, const GFXShaderConstType matrixType) 
{    
   AssertFatal(handle, "Handle is NULL!" );
   AssertFatal(handle->isValid(), "Handle is not valid!" );

   AssertFatal(static_cast<const GFXD3D11ShaderConstHandle*>(handle), "Incorrect const buffer type!"); 
   const GFXD3D11ShaderConstHandle* h = static_cast<const GFXD3D11ShaderConstHandle*>(handle); 
   AssertFatal(!h->isSampler(), "Handle is sampler constant!" );
   AssertFatal(h->mShader == mShader, "Mismatched shaders!"); 

   MatrixF transposed;   
   mat.transposeTo(transposed);

   if (h->mInstancingConstant) 
   {
      if ( matrixType == GFXSCT_Float4x4 )
         dMemcpy( mInstPtr+h->mPixelHandle.offset, mat, sizeof( mat ) );
         
      // TODO: Support 3x3 and 2x2 matricies?      
      return;
   }

   if (h->mVertexConstant) 
      mVertexConstBufferF->set(h->mVertexHandle, transposed, matrixType); 
   if (h->mPixelConstant) 
      mPixelConstBufferF->set(h->mPixelHandle, transposed, matrixType);   
}

void GFXD3D11ShaderConstBuffer::set(GFXShaderConstHandle* handle, const MatrixF* mat, const U32 arraySize, const GFXShaderConstType matrixType)
{
   AssertFatal(handle, "Handle is NULL!" );
   AssertFatal(handle->isValid(), "Handle is not valid!" );

   AssertFatal(static_cast<const GFXD3D11ShaderConstHandle*>(handle), "Incorrect const buffer type!"); 
   const GFXD3D11ShaderConstHandle* h = static_cast<const GFXD3D11ShaderConstHandle*>(handle); 
   AssertFatal(!h->isSampler(), "Handle is sampler constant!" );
   AssertFatal(h->mShader == mShader, "Mismatched shaders!"); 

   static Vector<MatrixF> transposed;
   if (arraySize > transposed.size())
      transposed.setSize(arraySize);   
   for (U32 i = 0; i < arraySize; i++)
      mat[i].transposeTo(transposed[i]);

   // TODO: Maybe support this in the future?
   if (h->mInstancingConstant) 
      return;

   if (h->mVertexConstant) 
      mVertexConstBufferF->set(h->mVertexHandle, transposed.begin(), arraySize, matrixType);
   if (h->mPixelConstant) 
      mPixelConstBufferF->set(h->mPixelHandle, transposed.begin(), arraySize, matrixType);              
}

const String GFXD3D11ShaderConstBuffer::describeSelf() const
{
   String ret;
   ret = String("   GFXD3D11ShaderConstBuffer\n");

   for (U32 i = 0; i < mVertexConstBufferLayoutF->getParameterCount(); i++)
   {
      GenericConstBufferLayout::ParamDesc pd;
      mVertexConstBufferLayoutF->getDesc(i, pd);

      ret += String::ToString("      Constant name: %s", pd.name);
   }

   return ret;
}

void GFXD3D11ShaderConstBuffer::zombify()
{
}

void GFXD3D11ShaderConstBuffer::resurrect()
{
}

bool GFXD3D11ShaderConstBuffer::isDirty()
{
   bool ret = mVertexConstBufferF->isDirty();
   ret |= mVertexConstBufferI->isDirty();
   ret |= mPixelConstBufferF->isDirty();
   ret |= mPixelConstBufferI->isDirty();
   return ret;
}

void GFXD3D11ShaderConstBuffer::activate( GFXD3D11ShaderConstBuffer *prevShaderBuffer )
{
   PROFILE_SCOPE(GFXD3D11ShaderConstBuffer_activate);

   // NOTE: This is a really critical function as it gets
   // called between every draw call to update the constants.
   //
   // Alot of the calls here are inlined... be careful 
   // what you change.

   // If the buffer has changed we need to compare it
   // with the new buffer to see if we can skip copying
   // equal buffer content.
   //
   // If the buffer hasn't changed then we only will
   // be copying the changes that have occured since
   // the last activate call.
   //
   if ( prevShaderBuffer != this )
   {
      // If the previous buffer is dirty, than we can't compare 
      // against it, because it hasn't sent its contents to the
      // card yet and must be copied.
      if ( prevShaderBuffer && !prevShaderBuffer->isDirty() )
      {
         PROFILE_SCOPE(GFXD3D11ShaderConstBuffer_activate_dirty_check_1);

         // If the buffer content is equal then we set the dirty
         // flag to false knowing the current state of the card matches
         // the new buffer.
         //
         // If the content is not equal we set the dirty flag to
         // true which causes the full content of the buffer to be
         // copied to the card.
         //
         mVertexConstBufferF->setDirty( !prevShaderBuffer->mVertexConstBufferF->isEqual( mVertexConstBufferF ) );
         mPixelConstBufferF->setDirty( !prevShaderBuffer->mPixelConstBufferF->isEqual( mPixelConstBufferF ) );
         mVertexConstBufferI->setDirty( !prevShaderBuffer->mVertexConstBufferF->isEqual( mVertexConstBufferI ) );
         mPixelConstBufferI->setDirty( !prevShaderBuffer->mPixelConstBufferF->isEqual( mPixelConstBufferI ) );
      } 
      else
      {
         // This happens rarely... but it can happen.
         //
         // We copy the entire dirty state to the card.

         PROFILE_SCOPE(GFXD3D11ShaderConstBuffer_activate_dirty_check_2);

         mVertexConstBufferF->setDirty( true );
         mPixelConstBufferF->setDirty( true );
         mVertexConstBufferI->setDirty( true );
         mPixelConstBufferI->setDirty( true );
      }      
   }

   D3D11_MAPPED_SUBRESOURCE pConstData;
   ZeroMemory(&pConstData, sizeof(D3D11_MAPPED_SUBRESOURCE));

   const U32 bytesToFloat4 = 16;
   const U32 bytesToInt4 = 16;
   U32 start, bufferSize;      
   const U8* buf;
   HRESULT hr;

   if(mVertexConstBufferF->isDirty())
   {
      buf = mVertexConstBufferF->getDirtyBuffer(&start, &bufferSize );
	  hr = static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->Map(mConstantBuffersVF, 0, D3D11_MAP_WRITE_DISCARD, NULL, &pConstData);

	  if(FAILED(hr))
	  {
		  AssertFatal(false, "failed to lock mConstantBuffersVF");
	  }
					
	  memcpy(&(((char*)(pConstData.pData))[start / bytesToFloat4]), (void*)buf, bufferSize / bytesToFloat4);

	  static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->Unmap(mConstantBuffersVF, 0);
	  static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->VSSetConstantBuffers(0, 1, &mConstantBuffersVF);
   }

   if(mPixelConstBufferF->isDirty())    
   {
      buf = mPixelConstBufferF->getDirtyBuffer(&start, &bufferSize);
	  hr = static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->Map(mConstantBuffersPF, 0, D3D11_MAP_WRITE_DISCARD, NULL, &pConstData);

	  if(FAILED(hr))
	  {
		  AssertFatal(false, "failed to lock mConstantBuffersPF");
	  }
					
	  memcpy(&(((char*)(pConstData.pData))[start / bytesToFloat4]), (void*)buf, bufferSize / bytesToFloat4);

	  static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->Unmap(mConstantBuffersPF, 0);
	  static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->PSSetConstantBuffers(0, 1, &mConstantBuffersPF);       
   }

   if(mVertexConstBufferI->isDirty() )
   {
      buf = mVertexConstBufferI->getDirtyBuffer(&start, &bufferSize );
	  hr = static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->Map(mConstantBuffersVI, 0, D3D11_MAP_WRITE_DISCARD, NULL, &pConstData);

	  if(FAILED(hr))
	  {
		  AssertFatal(false, "failed to lock mConstantBuffersVI");
	  }
					
	  memcpy(&(((char*)(pConstData.pData))[start / bytesToInt4]), (void*)buf, bufferSize / bytesToInt4);

	  static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->Unmap(mConstantBuffersVI, 0);
	  static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->VSSetConstantBuffers(0, 1, &mConstantBuffersVI);
   }

   if(mPixelConstBufferI->isDirty())    
   {
      buf = mPixelConstBufferI->getDirtyBuffer(&start, &bufferSize);
	  hr = static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->Map(mConstantBuffersPI, 0, D3D11_MAP_WRITE_DISCARD, NULL, &pConstData);

	  if(FAILED(hr))
	  {
		  AssertFatal(false, "failed to lock mConstantBuffersPI");
	  }
					
	  memcpy(&(((char*)(pConstData.pData))[start / bytesToInt4]), (void*)buf, bufferSize / bytesToInt4);

	  static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->Unmap(mConstantBuffersPI, 0);
	  static_cast<GFXD3D11Device*>(GFX)->getDeviceContext()->PSSetConstantBuffers(0, 1, &mConstantBuffersPI);       
   }

   #ifdef TORQUE_DEBUG

      // Make sure all the constants for this buffer were assigned.
      if(mWasLost)
      {
         mVertexConstBufferF->assertUnassignedConstants( mShader->getVertexShaderFile().c_str() );
         mVertexConstBufferI->assertUnassignedConstants( mShader->getVertexShaderFile().c_str() );
         mPixelConstBufferF->assertUnassignedConstants( mShader->getPixelShaderFile().c_str() );
         mPixelConstBufferI->assertUnassignedConstants( mShader->getPixelShaderFile().c_str() );
      }

   #endif

   // Clear the lost state.
   mWasLost = false;
}

void GFXD3D11ShaderConstBuffer::onShaderReload( GFXD3D11Shader *shader )
{
    AssertFatal( shader == mShader, "GFXD3D11ShaderConstBuffer::onShaderReload is hosed!" );

    // release constant buffer
    SAFE_RELEASE(mConstantBuffersPI);
    SAFE_RELEASE(mConstantBuffersPF);
    SAFE_RELEASE(mConstantBuffersVI);
    SAFE_RELEASE(mConstantBuffersVF);

    SAFE_DELETE( mVertexConstBufferF );
    SAFE_DELETE( mPixelConstBufferF );
    SAFE_DELETE( mVertexConstBufferI );
    SAFE_DELETE( mPixelConstBufferI );
        
    AssertFatal( mVertexConstBufferLayoutF == shader->mVertexConstBufferLayoutF, "GFXD3D11ShaderConstBuffer::onShaderReload is hosed!" );   
    AssertFatal( mPixelConstBufferLayoutF == shader->mPixelConstBufferLayoutF, "GFXD3D11ShaderConstBuffer::onShaderReload is hosed!" );   
    AssertFatal( mVertexConstBufferLayoutI == shader->mVertexConstBufferLayoutI, "GFXD3D11ShaderConstBuffer::onShaderReload is hosed!" );   
    AssertFatal( mPixelConstBufferLayoutI == shader->mPixelConstBufferLayoutI, "GFXD3D11ShaderConstBuffer::onShaderReload is hosed!" );      

    mVertexConstBufferF = new GenericConstBuffer( mVertexConstBufferLayoutF );      
    mVertexConstBufferI = new GenericConstBuffer( mVertexConstBufferLayoutI );      
    mPixelConstBufferF = new GenericConstBuffer( mPixelConstBufferLayoutF );      
    mPixelConstBufferI = new GenericConstBuffer( mPixelConstBufferLayoutI ); 
   
	HRESULT hr;

	// Create a vertex float constant buffer
	D3D11_BUFFER_DESC cbDescVF;
	cbDescVF.ByteWidth = mVertexConstBufferLayoutF->getBufferSize();
	cbDescVF.Usage = D3D11_USAGE_DYNAMIC;
	cbDescVF.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDescVF.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDescVF.MiscFlags = 0;
	cbDescVF.StructureByteStride = 0;

	hr = static_cast<GFXD3D11Device*>(GFX)->getDevice()->CreateBuffer(&cbDescVF, NULL, &mConstantBuffersVF);

	if(FAILED(hr))
	{
		AssertFatal(false, "can't create constant mConstantBuffersVF!");
	}

	// Create a vertex integer constant buffer
	D3D11_BUFFER_DESC cbDescVI;
	cbDescVI.ByteWidth = mVertexConstBufferLayoutI->getBufferSize();
	cbDescVI.Usage = D3D11_USAGE_DYNAMIC;
	cbDescVI.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDescVI.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDescVI.MiscFlags = 0;
	cbDescVI.StructureByteStride = 0;

	hr = static_cast<GFXD3D11Device*>(GFX)->getDevice()->CreateBuffer(&cbDescVI, NULL, &mConstantBuffersVI);

	if(FAILED(hr))
	{
		AssertFatal(false, "can't create constant mConstantBuffersVI!");
	}

	// Create a pixel float constant buffer
	D3D11_BUFFER_DESC cbDescPF;
	cbDescPF.ByteWidth = mPixelConstBufferLayoutF->getBufferSize();
	cbDescPF.Usage = D3D11_USAGE_DYNAMIC;
	cbDescPF.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDescPF.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDescPF.MiscFlags = 0;
	cbDescPF.StructureByteStride = 0;

	hr = static_cast<GFXD3D11Device*>(GFX)->getDevice()->CreateBuffer(&cbDescPF, NULL, &mConstantBuffersPF);

	if(FAILED(hr))
	{
		AssertFatal(false, "can't create constant mConstantBuffersPF!");
	}

	// Create a pixel integer constant buffer
	D3D11_BUFFER_DESC cbDescPI;
	cbDescPI.ByteWidth = mPixelConstBufferLayoutI->getBufferSize();
	cbDescPI.Usage = D3D11_USAGE_DYNAMIC;
	cbDescPI.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDescPI.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDescPI.MiscFlags = 0;
	cbDescPI.StructureByteStride = 0;

	hr = static_cast<GFXD3D11Device*>(GFX)->getDevice()->CreateBuffer(&cbDescPI, NULL, &mConstantBuffersPI);

	if(FAILED(hr))
	{
		AssertFatal(false, "can't create constant mConstantBuffersPI!");
	}

    // Set the lost state.
    mWasLost = true;
}

//------------------------------------------------------------------------------

GFXD3D11Shader::GFXD3D11Shader()
{
    VECTOR_SET_ASSOCIATION( mShaderConsts );

    AssertFatal(static_cast<GFXD3D11Device*>( GFX )->getDevice(), "Invalid device for shader.");
    mVertShader = NULL;
    mPixShader = NULL;
    mVertexConstBufferLayoutF = NULL;
    mPixelConstBufferLayoutF = NULL;
    mVertexConstBufferLayoutI = NULL;
    mPixelConstBufferLayoutI = NULL;

    if( smD3DInclude == NULL )
       smD3DInclude = new gfxD3D11Include;
}

//------------------------------------------------------------------------------

GFXD3D11Shader::~GFXD3D11Shader()
{
   for (HandleMap::Iterator i = mHandles.begin(); i != mHandles.end(); i++)
      delete i->value;
   SAFE_DELETE(mVertexConstBufferLayoutF);
   SAFE_DELETE(mPixelConstBufferLayoutF);
   SAFE_DELETE(mVertexConstBufferLayoutI);
   SAFE_DELETE(mPixelConstBufferLayoutI);
   SAFE_RELEASE(mVertShader);
   SAFE_RELEASE(mPixShader);
}

bool GFXD3D11Shader::_init()
{
   PROFILE_SCOPE( GFXD3D11Shader_Init );

   SAFE_RELEASE(mVertShader);
   SAFE_RELEASE(mPixShader);

   // Create the macro array including the system wide macros.
   const U32 macroCount = smGlobalMacros.size() + mMacros.size() + 2;
   FrameTemp<D3D_SHADER_MACRO> d3dMacros( macroCount );

   for ( U32 i=0; i < smGlobalMacros.size(); i++ )
   {
      d3dMacros[i].Name = smGlobalMacros[i].name.c_str();
      d3dMacros[i].Definition = smGlobalMacros[i].value.c_str();
   }

   for ( U32 i=0; i < mMacros.size(); i++ )
   {
      d3dMacros[i+smGlobalMacros.size()].Name = mMacros[i].name.c_str();
      d3dMacros[i+smGlobalMacros.size()].Definition = mMacros[i].value.c_str();
   }

   d3dMacros[macroCount - 2].Name = "TORQUE_SM";
   d3dMacros[macroCount - 2].Definition = "30";

   memset(&d3dMacros[macroCount - 1], 0, sizeof(D3D_SHADER_MACRO));

   if ( !mVertexConstBufferLayoutF )
      mVertexConstBufferLayoutF = new GFXD3D11ShaderBufferLayout();
   else
      mVertexConstBufferLayoutF->clear();

   if ( !mVertexConstBufferLayoutI )
      mVertexConstBufferLayoutI = new GFXD3D11ShaderBufferLayout();
   else
      mVertexConstBufferLayoutI->clear();
      
   if ( !mPixelConstBufferLayoutF )
      mPixelConstBufferLayoutF = new GFXD3D11ShaderBufferLayout();
   else
      mPixelConstBufferLayoutF->clear();

   if ( !mPixelConstBufferLayoutI )
      mPixelConstBufferLayoutI = new GFXD3D11ShaderBufferLayout();
   else
      mPixelConstBufferLayoutI->clear();
   
   mSamplerDescriptions.clear();
   mShaderConsts.clear();

   if ( !Con::getBoolVariable( "$shaders::forceLoadCSF", false ) )
   {
      if (  !mVertexFile.isEmpty() && !_compileShader( mVertexFile, "vs_3_0", d3dMacros, mVertexConstBufferLayoutF, mVertexConstBufferLayoutI, mSamplerDescriptions ) )
         return false;

      if (  !mPixelFile.isEmpty() && !_compileShader( mPixelFile, "ps_3_0", d3dMacros, mPixelConstBufferLayoutF, mPixelConstBufferLayoutI, mSamplerDescriptions ) )
         return false;
   } 
   else 
   {
      if ( !_loadCompiledOutput( mVertexFile, "vs_3_0", mVertexConstBufferLayoutF, mVertexConstBufferLayoutI, mSamplerDescriptions ) )
      {
         if ( smLogErrors )
            Con::errorf( "GFXD3D11Shader::init - Unable to load precompiled vertex shader for '%s'.",  mVertexFile.getFullPath().c_str() );

         return false;
      }

      if ( !_loadCompiledOutput( mPixelFile, "ps_3_0", mPixelConstBufferLayoutF, mPixelConstBufferLayoutI, mSamplerDescriptions ) )
      {
         if ( smLogErrors )
            Con::errorf( "GFXD3D11Shader::init - Unable to load precompiled pixel shader for '%s'.",  mPixelFile.getFullPath().c_str() );

         return false;
      }
   }

   // Existing handles are resored to an uninitialized state.
   // Those that are found when parsing the layout parameters
   // will then be re-initialized.
   HandleMap::Iterator iter = mHandles.begin();
   for ( ; iter != mHandles.end(); iter++ )        
      (iter->value)->clear();      

   _buildShaderConstantHandles(mVertexConstBufferLayoutF, true);
   _buildShaderConstantHandles(mVertexConstBufferLayoutI, true);
   _buildShaderConstantHandles(mPixelConstBufferLayoutF, false);
   _buildShaderConstantHandles(mPixelConstBufferLayoutI, false);
   _buildSamplerShaderConstantHandles( mSamplerDescriptions );
   _buildInstancingShaderConstantHandles();

   // Notify any existing buffers that the buffer 
   // layouts have changed and they need to update.
   Vector<GFXShaderConstBuffer*>::iterator biter = mActiveBuffers.begin();
   for ( ; biter != mActiveBuffers.end(); biter++ )
      ((GFXD3D11ShaderConstBuffer*)(*biter))->onShaderReload( this );

   return true;
}

bool GFXD3D11Shader::_compileShader( const Torque::Path &filePath, 
                                    const String& target,                                  
                                    const D3D_SHADER_MACRO *defines, 
                                    GenericConstBufferLayout* bufferLayoutF, 
                                    GenericConstBufferLayout* bufferLayoutI,
                                    Vector<GFXShaderConstDesc> &samplerDescriptions )
{
   PROFILE_SCOPE( GFXD3D11Shader_CompileShader );

   using namespace Torque;

   HRESULT res = E_FAIL;
   ID3DBlob* code = NULL;
   ID3DBlob* errorBuff = NULL;
   bool HasValidConstants = false;

#ifdef TORQUE_DEBUG

   // anis note:

   /*
		warning: Using D3DCOMPILE_SKIP_OPTIMIZATION (tested on directx9)
		cause a possible D3DCompile error return for excessive number of asm instruction generated (>64).
		At the moment D3DCOMPILE_DEBUG it's enough for debugging purpose....
   */

   U32 flags = D3DCOMPILE_DEBUG /*| D3DCOMPILE_SKIP_OPTIMIZATION*/;

#else

   // anis -> useful flags for shader compiler:

   /*
		D3DCOMPILE_OPTIMIZATION_LEVEL0
		D3DCOMPILE_OPTIMIZATION_LEVEL1
		D3DCOMPILE_OPTIMIZATION_LEVEL2
		D3DCOMPILE_OPTIMIZATION_LEVEL3
		D3DCOMPILE_WARNINGS_ARE_ERRORS
   */

   U32 flags = 0; //place here flags for optimization level customization =)

#endif

   /* 
   
	   anis -> useful flag for D3D11 engine to make compile under 5_0 target without touching effects...

   */

   flags |= D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;

   // Is it an HLSL shader?
   if(filePath.getExtension().equal("hlsl", String::NoCase))   
   {
      // Set this so that the D3DInclude::Open will have this 
      // information for relative paths.
      smD3DInclude->setPath(filePath.getRootAndPath());

      FileStream s;
      if (!s.open(filePath, Torque::FS::File::Read))
      {
         AssertISV(false, avar("GFXD3D11Shader::initShader - failed to open shader '%s'.", filePath.getFullPath().c_str()));

         if ( smLogErrors )
            Con::errorf( "GFXD3D11Shader::_compileShader - Failed to open shader file '%s'.", filePath.getFullPath().c_str() );

         return false;
      }

      // Convert the path which might have virtualized
      // mount paths to a real file system path.
      Torque::Path realPath;
      if (!FS::GetFSPath( filePath, realPath))
         realPath = filePath;

      U32 bufSize = s.getStreamSize();

      FrameAllocatorMarker fam;
      char *buffer = NULL;

      buffer = (char*)fam.alloc(bufSize + 1);
      s.read(bufSize, buffer);
      buffer[bufSize] = 0;

	  res = D3DCompile(buffer, bufSize, realPath.getFullPath().c_str(), defines, smD3DInclude, "main", target, flags, 0, &code, &errorBuff);
   }

   // Is it a precompiled obj shader?
   else if(filePath.getExtension().equal("obj", String::NoCase))
   {     
      FileStream  s;
      if(!s.open(filePath, Torque::FS::File::Read))
      {
         AssertISV(false, avar("GFXD3D11Shader::initShader - failed to open shader '%s'.", filePath.getFullPath().c_str()));

         if ( smLogErrors )
            Con::errorf( "GFXD3D11Shader::_compileShader - Failed to open shader file '%s'.", filePath.getFullPath().c_str() );

         return false;
      }

	  res = D3DCreateBlob(s.getStreamSize(), &code);
      AssertISV(SUCCEEDED(res), "Unable to create buffer!");
      s.read(s.getStreamSize(), code->GetBufferPointer());
   }
   else
   {
      if (smLogErrors)
         Con::errorf("GFXD3D11Shader::_compileShader - Unsupported shader file type '%s'.", filePath.getFullPath().c_str());

      return false;
   }

   if(SUCCEEDED(res) && code)
   {
	   // anis -> HasValidConstants == false ? -> if this condition is true, there should be a problem inside the bytecode parser...
       void* buffer = code->GetBufferPointer();
       HasValidConstants = buffer && table.Create(buffer);
   }

   if(errorBuff)
   {
      // remove \n at end of buffer
      U8 *buffPtr = (U8*) errorBuff->GetBufferPointer();
      U32 len = dStrlen( (const char*) buffPtr );
      buffPtr[len-1] = '\0';

      if(FAILED(res))
      {
         if(smLogErrors)
            Con::errorf("failed to compile shader: %s", buffPtr);
      }
      else
      {
         if(smLogWarnings)
            Con::errorf("shader compiled with warning(s): %s", buffPtr);
      }
   }
   else if (code == NULL && smLogErrors)
      Con::errorf( "GFXD3D11Shader::_compileShader - no compiled code produced; possibly missing file '%s'.", filePath.getFullPath().c_str() );

   if(code != NULL)
   {
      #ifndef TORQUE_SHIPPING

		 ID3DBlob* disassem = NULL;
         D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, NULL, &disassem);
         mDissasembly = (const char*)disassem->GetBufferPointer();         
         SAFE_RELEASE(disassem);

         if(gDisassembleAllShaders)
         {
			String filename = filePath.getFullPath();
            filename.replace( ".hlsl", "_dis.txt" );

            FileStream *fstream = FileStream::createAndOpen( filename, Torque::FS::File::Write );
            if ( fstream )
            {            
               fstream->write( mDissasembly );
               fstream->close();
               delete fstream;   
            }
         }

      #endif

      if (target.compare("ps_", 3) == 0)      
         res = static_cast<GFXD3D11Device*>(GFX)->getDevice()->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), NULL,  &mPixShader);
      else
         res = static_cast<GFXD3D11Device*>(GFX)->getDevice()->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), NULL, &mVertShader);

      if (SUCCEEDED(res) && HasValidConstants)
         _getShaderConstants(bufferLayoutF, bufferLayoutI, samplerDescriptions);

#ifdef TORQUE_ENABLE_CSF_GENERATION

      // Ok, we've got a valid shader and constants, let's write them all out.
      if (!_saveCompiledOutput(filePath, code, bufferLayoutF, bufferLayoutI) && smLogErrors)
         Con::errorf( "GFXD3D11Shader::_compileShader - Unable to save shader compile output for: %s", 
            filePath.getFullPath().c_str());

#endif

      if(FAILED(res) && smLogErrors)
         Con::errorf("GFXD3D11Shader::_compileShader - Unable to create shader for '%s'.", filePath.getFullPath().c_str());
   }

   bool result = code && SUCCEEDED(res) && HasValidConstants;

   SAFE_RELEASE(code);
   SAFE_RELEASE(errorBuff);

   return result;
}

void GFXD3D11Shader::_getShaderConstants( GenericConstBufferLayout *bufferLayoutF, 
                                         GenericConstBufferLayout* bufferLayoutI,
                                         Vector<GFXShaderConstDesc> &samplerDescriptions )
{
   PROFILE_SCOPE( GFXD3D11Shader_GetShaderConstants );

   for (U32 i = 0; i < table.GetConstantCount(); i++)
   {
	    const ConstantDesc& constantDesc = table.GetConstantByIndex(i)[0];

        GFXShaderConstDesc desc;
                  
		desc.name = String(constantDesc.Name.c_str());
        // Prepend a "$" if it doesn't exist.  Just to make things consistent.
        if (desc.name.find("$") != 0) desc.name = String::ToString("$%s", desc.name.c_str());

#ifdef TORQUE_DEBUG
        Con::printf("name %s:, offset: %d, size: %d, constantDesc.Elements: %d", desc.name.c_str(), constantDesc.RegisterIndex, constantDesc.Bytes, constantDesc.Elements);
#endif
        desc.arraySize = constantDesc.Elements;         
                  
        GenericConstBufferLayout* bufferLayout = NULL;
        switch (constantDesc.RegisterSet)
        {
			case D3DRS_INT4:   
				{
					bufferLayout = bufferLayoutI;
					switch (constantDesc.Class)
					{
						case D3DPC_SCALAR:
							desc.constType = GFXSCT_Int;
							break;
						case D3DPC_VECTOR:
						{
							switch (constantDesc.Columns)
							{
							case 1 :
								desc.constType = GFXSCT_Int;
								break;
							case 2 :
								desc.constType = GFXSCT_Int2;
								break;
							case 3 :
								desc.constType = GFXSCT_Int3;
								break;
							case 4 :
								desc.constType = GFXSCT_Int4;
								break;                           
							default:
								AssertFatal(false, "Unknown int vector type!");
								break;
							}
						}
						break;
					}
					desc.constType = GFXSCT_Int4;
					break;
				}
			case D3DRS_FLOAT4:
				{  
					bufferLayout = bufferLayoutF;
					switch (constantDesc.Class)
					{
					case D3DPC_SCALAR:                     
						desc.constType = GFXSCT_Float;
						break;
					case D3DPC_VECTOR:               
						{                     
						switch (constantDesc.Columns)
						{
							case 1 :
								desc.constType = GFXSCT_Float;
								break;
							case 2 :
								desc.constType = GFXSCT_Float2;
								break;
							case 3 :
								desc.constType = GFXSCT_Float3;
								break;
							case 4 :
								desc.constType = GFXSCT_Float4;
								break;                           
							default:
								AssertFatal(false, "Unknown float vector type!");
								break;
						}
						}
						break;
					case D3DPC_MATRIX_ROWS:
					case D3DPC_MATRIX_COLUMNS:                     
						{
						switch (constantDesc.RegisterCount)                        
						{
							case 3 :
								desc.constType = GFXSCT_Float3x3;
								break;
							case 4 :
								desc.constType = GFXSCT_Float4x4;
								break;
						}
						}
						break;
					case D3DPC_OBJECT:
					case D3DPC_STRUCT:
						bufferLayout = NULL;
						break;
					}
				}
				break;
			case D3DRS_SAMPLER:
				{
					AssertFatal( constantDesc.Elements == 1, "Sampler Arrays not yet supported!" );

					switch (constantDesc.Type)
					{
						case D3DPT_SAMPLER:
						case D3DPT_SAMPLER1D:
						case D3DPT_SAMPLER2D:
						case D3DPT_SAMPLER3D:
						// Hi-jack the desc's arraySize to store the registerIndex.
						desc.constType = GFXSCT_Sampler;
						desc.arraySize = constantDesc.RegisterIndex;
						samplerDescriptions.push_back( desc );
						break;
						case D3DPT_SAMPLERCUBE :
						desc.constType = GFXSCT_SamplerCube;
						desc.arraySize = constantDesc.RegisterIndex;
						samplerDescriptions.push_back( desc );
						break;
					}
				}
				break;
			default:               
				AssertFatal(false, "Unknown shader constant class enum");               
				break;
        }         
         
        if (bufferLayout)
        {
			mShaderConsts.push_back(desc);

			U32 alignBytes = getAlignmentValue(desc.constType);
			U32 paramSize = alignBytes * desc.arraySize;
			bufferLayout->addParameter( desc.name, 
										desc.constType, 
										constantDesc.RegisterIndex * sizeof(Point4F), 
										paramSize, 
										desc.arraySize, 
										alignBytes );
        }
   }

   table.ClearConstants();
}

const U32 GFXD3D11Shader::smCompiledShaderTag = MakeFourCC('t','c','s','f');

bool GFXD3D11Shader::_saveCompiledOutput( const Torque::Path &filePath, 
                                         ID3DBlob *buffer, 
                                         GenericConstBufferLayout *bufferLayoutF, 
                                         GenericConstBufferLayout *bufferLayoutI,
                                         Vector<GFXShaderConstDesc> &samplerDescriptions )
{
   Torque::Path outputPath(filePath);
   outputPath.setExtension("csf");     // "C"ompiled "S"hader "F"ile (fancy!)

   FileStream f;
   if (!f.open(outputPath, Torque::FS::File::Write))
      return false;
   if (!f.write(smCompiledShaderTag))
      return false;
   // We could reverse engineer the structure in the compiled output, but this
   // is a bit easier because we can just read it into the struct that we want.
   if (!bufferLayoutF->write(&f))
      return false;
   if (!bufferLayoutI->write(&f))
      return false;
   U32 bufferSize = buffer->GetBufferSize();
   if (!f.write(bufferSize))
      return false;
   if (!f.write(bufferSize, buffer->GetBufferPointer()))
      return false;

   // Write out sampler descriptions.

   f.write( samplerDescriptions.size() );   

   for ( U32 i = 0; i < samplerDescriptions.size(); i++ )
   {
      f.write( samplerDescriptions[i].name );
      f.write( (U32)(samplerDescriptions[i].constType) );
      f.write( samplerDescriptions[i].arraySize );
   }

   f.close();

   return true;
}

bool GFXD3D11Shader::_loadCompiledOutput( const Torque::Path &filePath, 
                                         const String &target, 
                                         GenericConstBufferLayout *bufferLayoutF, 
                                         GenericConstBufferLayout *bufferLayoutI,
                                         Vector<GFXShaderConstDesc> &samplerDescriptions )
{
   Torque::Path outputPath(filePath);
   outputPath.setExtension("csf");     // "C"ompiled "S"hader "F"ile (fancy!)

   FileStream f;
   if (!f.open(outputPath, Torque::FS::File::Read))
      return false;
   U32 fileTag;
   if (!f.read(&fileTag))
      return false;
   if (fileTag != smCompiledShaderTag)
      return false;
   if (!bufferLayoutF->read(&f))
      return false;
   if (!bufferLayoutI->read(&f))
      return false;
   U32 bufferSize;
   if (!f.read(&bufferSize))
      return false;
   U32 waterMark = FrameAllocator::getWaterMark();
   DWORD* buffer = static_cast<DWORD*>(FrameAllocator::alloc(bufferSize));
   if (!f.read(bufferSize, buffer))
      return false;

   // Read sampler descriptions.

   U32 samplerCount;
   f.read( &samplerCount );   

   for ( U32 i = 0; i < samplerCount; i++ )
   {
      GFXShaderConstDesc samplerDesc;
      f.read( &(samplerDesc.name) );
      f.read( (U32*)&(samplerDesc.constType) );
      f.read( &(samplerDesc.arraySize) );

      samplerDescriptions.push_back( samplerDesc );
   }

   f.close();

   HRESULT res;
   if (target.compare("ps_", 3) == 0)      
      res = static_cast<GFXD3D11Device*>( GFX )->getDevice()->CreatePixelShader(buffer, bufferSize, NULL, &mPixShader);
   else
      res = static_cast<GFXD3D11Device*>( GFX )->getDevice()->CreateVertexShader(buffer, bufferSize, NULL, &mVertShader);
   AssertFatal(SUCCEEDED(res), "Unable to load shader!");

   FrameAllocator::setWaterMark(waterMark);
   return SUCCEEDED(res);
}

void GFXD3D11Shader::_buildShaderConstantHandles(GenericConstBufferLayout* layout, bool vertexConst)
{                     
   for (U32 i = 0; i < layout->getParameterCount(); i++)
   {
      GenericConstBufferLayout::ParamDesc pd;
      layout->getDesc(i, pd);

      GFXD3D11ShaderConstHandle* handle;
      HandleMap::Iterator j = mHandles.find(pd.name);

      if (j != mHandles.end())
      {
         handle = j->value;         
         handle->mShader = this;
         handle->setValid( true );
      } 
      else
      {
         handle = new GFXD3D11ShaderConstHandle();
         handle->mShader = this;
         mHandles[pd.name] = handle;
         handle->setValid( true );
      }

      if ( vertexConst )
      {
         handle->mVertexConstant = true;
         handle->mVertexHandle = pd;
      } 
      else 
      {
         handle->mPixelConstant = true;
         handle->mPixelHandle = pd;
      }
   }
}

void GFXD3D11Shader::_buildSamplerShaderConstantHandles( Vector<GFXShaderConstDesc> &samplerDescriptions )
{                     
   Vector<GFXShaderConstDesc>::iterator iter = samplerDescriptions.begin();
   for ( ; iter != samplerDescriptions.end(); iter++ )
   {
      const GFXShaderConstDesc &desc = *iter;

      AssertFatal(   desc.constType == GFXSCT_Sampler || 
                     desc.constType == GFXSCT_SamplerCube, 
                     "GFXD3D11Shader::_buildSamplerShaderConstantHandles - Invalid samplerDescription type!" );

      GFXD3D11ShaderConstHandle *handle;
      HandleMap::Iterator j = mHandles.find(desc.name);

      if ( j != mHandles.end() )
         handle = j->value;
      else
      {
         handle = new GFXD3D11ShaderConstHandle();
         mHandles[desc.name] = handle;         
      }

      handle->mShader = this;         
      handle->setValid( true );         
      handle->mPixelConstant = true;
      handle->mPixelHandle.name = desc.name;
      handle->mPixelHandle.constType = desc.constType;
      handle->mPixelHandle.offset = desc.arraySize;         
   }
}

void GFXD3D11Shader::_buildInstancingShaderConstantHandles()
{
   U32 offset = 0;
   for ( U32 i=0; i < mInstancingFormat.getElementCount(); i++ )
   {
      const GFXVertexElement &element = mInstancingFormat.getElement( i );
      
      String constName = String::ToString( "$%s", element.getSemantic().c_str() );

      GFXD3D11ShaderConstHandle *handle;
      HandleMap::Iterator j = mHandles.find( constName );

      if ( j != mHandles.end() )
         handle = j->value; 
      else
      {
         handle = new GFXD3D11ShaderConstHandle();
         mHandles[ constName ] = handle;         
      }

      handle->mShader = this;
      handle->setValid( true );         
      handle->mInstancingConstant = true;

      // We shouldn't have an instancing constant that is also 
      // a vertex or pixel constant!  This means the shader features
      // are confused as to what is instanced.
      //
      AssertFatal(   !handle->mVertexConstant &&
                     !handle->mPixelConstant,
                     "GFXD3D11Shader::_buildInstancingShaderConstantHandles - Bad instanced constant!" );

      // HACK:  The GFXD3D11ShaderConstHandle will check mVertexConstant then
      // fall back to reading the mPixelHandle values.  We depend on this here
      // and store the data we need in the mPixelHandle constant although its
      // not a pixel shader constant.
      //
      handle->mPixelHandle.name = constName;
      handle->mPixelHandle.offset = offset;

      // If this is a matrix we will have 2 or 3 more of these
      // semantics with the same name after it.
      for ( ; i < mInstancingFormat.getElementCount(); i++ )
      {
         const GFXVertexElement &nextElement = mInstancingFormat.getElement( i );
         if ( nextElement.getSemantic() != element.getSemantic() )
         {
            i--;
            break;
         }
         offset += nextElement.getSizeInBytes();
      }
   }
}

GFXShaderConstBufferRef GFXD3D11Shader::allocConstBuffer()
{
   if (mVertexConstBufferLayoutF && mPixelConstBufferLayoutF)
   {
      GFXD3D11ShaderConstBuffer* buffer = new GFXD3D11ShaderConstBuffer(this, mVertexConstBufferLayoutF, mVertexConstBufferLayoutI, mPixelConstBufferLayoutF, mPixelConstBufferLayoutI);
      mActiveBuffers.push_back( buffer );
      buffer->registerResourceWithDevice(getOwningDevice());
      return buffer;
   } else {
      return NULL;
   }
}

/// Returns a shader constant handle for name, if the variable doesn't exist NULL is returned.
GFXShaderConstHandle* GFXD3D11Shader::getShaderConstHandle(const String& name)
{
   HandleMap::Iterator i = mHandles.find(name);   
   if ( i != mHandles.end() )
   {
      return i->value;
   } 
   else 
   {     
      GFXD3D11ShaderConstHandle *handle = new GFXD3D11ShaderConstHandle();
      handle->setValid( false );
      handle->mShader = this;
      mHandles[name] = handle;

      return handle;      
   }      
}

// andrewmac: added to match Luis' changes
GFXShaderConstHandle* GFXD3D11Shader::findShaderConstHandle(const String& name)
{
   HandleMap::Iterator i = mHandles.find(name);
   if(i != mHandles.end())
      return i->value;
   else
   {
      return NULL;
   }
}

const Vector<GFXShaderConstDesc>& GFXD3D11Shader::getShaderConstDesc() const
{
   return mShaderConsts;
}

U32 GFXD3D11Shader::getAlignmentValue(const GFXShaderConstType constType) const
{   
   const U32 mRowSizeF = 16;
   const U32 mRowSizeI = 16;

   switch (constType)
   {
      case GFXSCT_Float :
      case GFXSCT_Float2 :
      case GFXSCT_Float3 : 
      case GFXSCT_Float4 :
         return mRowSizeF;
         break;
         // Matrices
      case GFXSCT_Float2x2 :
         return mRowSizeF * 2;
         break;
      case GFXSCT_Float3x3 : 
         return mRowSizeF * 3;
         break;
      case GFXSCT_Float4x4 :
         return mRowSizeF * 4;
         break;   
      //// Scalar
      case GFXSCT_Int :
      case GFXSCT_Int2 :
      case GFXSCT_Int3 : 
      case GFXSCT_Int4 :
         return mRowSizeI;
         break;
      default:
         AssertFatal(false, "Unsupported type!");
         return 0;
         break;
   }
}

void GFXD3D11Shader::zombify()
{
   // Shaders don't need zombification
}

void GFXD3D11Shader::resurrect()
{
   // Shaders are never zombies, and therefore don't have to be brought back.
}
Torque 3D v3.5
==============

This branch combines D3D9-Refactor & DirectX 11 progress by Anis with VS2013 fixes by signmotion. This version requires Windows 8 SDK to be installed instead of DirectX SDK since it is now deprecated.

D3D-Refactor by Anis
----------------

* Better *.cpp organization / code cleaning
* Removed D3DX calls dependencies. (deprecated by Microsoft)
* Removed D3D9 XBOX360 old reference.
* Shader/Constant buffer refactor with a custom parser.
* gfxD3D9Shader total refactor.
* Mipmap autogen fixes
* Minor fixes under cubemap
* Speed improved on texture loading
* Removed 24bit format (deprecated since DX10) (i convert it to 32bit) 
* Some performance improvement (especially under a lot of dynamic_cast used in wrong context)
* Removed fixed pipeline (deprecated by Microsoft since DX10)
* Many other minor bug fixed.

DirectX 11 Support
----------------

* gfxD3D11CardProfiler - OK
* gfxD3D11Cubemap - OK
* gfxD3D11Device - UNFINISHED
* gfxD3D11EnumTranslate - OK
* gfxD3D11IndexBuffer - OK
* gfxD3D11OcclusionQuery - OK
* gfxD3D11QueryFence - OK
* gfxD3D11Shader - OK
* gfxD3D11StateBlock - OK
* gfxD3D11Target - UNFINISHED
* gfxD3D11TextureManager - TODO
* gfxD3D11TextureObject - TODO
* gfxD3D11VertexBuffer - OK

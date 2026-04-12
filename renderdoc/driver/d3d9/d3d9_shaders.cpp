/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "d3d9_shaders.h"
#include "d3d9_resources.h"

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DVertexShader9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DVertexShader9::WrappedIDirect3DVertexShader9(IDirect3DVertexShader9 *real,
                                                             WrappedIDirect3DDevice9 *device,
                                                             ResourceId id)
    : m_pReal(real), m_pDevice(device), m_ExtRef(1), m_IntRef(0)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;
  m_WrappedInfo = {D3D9WrappedType::VertexShader, m_ID, m_pReal};

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DVertexShader9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);

  // Capture the shader bytecode from the real object
  UINT size = 0;
  m_pReal->GetFunction(NULL, &size);
  if(size > 0)
  {
    m_Bytecode.resize(size / sizeof(DWORD));
    m_pReal->GetFunction(m_Bytecode.data(), &size);
  }
}

WrappedIDirect3DVertexShader9::~WrappedIDirect3DVertexShader9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DVertexShader9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DVertexShader9::Release()
{
  Atomic::Dec32(&m_ExtRef);
  RDCASSERT(m_ExtRef >= 0);

  int32_t extRef = m_ExtRef;
  int32_t intRef = m_IntRef;

  WrappedIDirect3DDevice9 *dev = m_pDevice;

  if(extRef + intRef == 0)
  {
    delete this;
    dev->Release();
    return 0;
  }

  if(extRef == 0)
    dev->Release();

  return (ULONG)extRef;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexShader9::QueryInterface(REFIID riid, void **ppvObj)
{
  if(ppvObj == NULL)
    return E_POINTER;

  if(riid == IID_ID3D9WrappedResource)
  {
    *ppvObj = &m_WrappedInfo;
    return S_OK;
  }
  if(riid == __uuidof(IUnknown))
  {
    *ppvObj = (IUnknown *)(IDirect3DVertexShader9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DVertexShader9))
  {
    *ppvObj = (IDirect3DVertexShader9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexShader9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexShader9::GetFunction(void *pData, UINT *pSizeOfData)
{
  if(pSizeOfData == NULL)
    return D3DERR_INVALIDCALL;

  UINT size = (UINT)(m_Bytecode.count() * sizeof(DWORD));

  if(pData == NULL)
  {
    *pSizeOfData = size;
    return D3D_OK;
  }

  if(*pSizeOfData < size)
  {
    *pSizeOfData = size;
    return D3DERR_MOREDATA;
  }

  memcpy(pData, m_Bytecode.data(), size);
  *pSizeOfData = size;
  return D3D_OK;
}

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DPixelShader9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DPixelShader9::WrappedIDirect3DPixelShader9(IDirect3DPixelShader9 *real,
                                                           WrappedIDirect3DDevice9 *device,
                                                           ResourceId id)
    : m_pReal(real), m_pDevice(device), m_ExtRef(1), m_IntRef(0)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;
  m_WrappedInfo = {D3D9WrappedType::PixelShader, m_ID, m_pReal};

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DPixelShader9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);

  // Capture the shader bytecode from the real object
  UINT size = 0;
  m_pReal->GetFunction(NULL, &size);
  if(size > 0)
  {
    m_Bytecode.resize(size / sizeof(DWORD));
    m_pReal->GetFunction(m_Bytecode.data(), &size);
  }
}

WrappedIDirect3DPixelShader9::~WrappedIDirect3DPixelShader9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DPixelShader9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DPixelShader9::Release()
{
  Atomic::Dec32(&m_ExtRef);
  RDCASSERT(m_ExtRef >= 0);

  int32_t extRef = m_ExtRef;
  int32_t intRef = m_IntRef;

  WrappedIDirect3DDevice9 *dev = m_pDevice;

  if(extRef + intRef == 0)
  {
    delete this;
    dev->Release();
    return 0;
  }

  if(extRef == 0)
    dev->Release();

  return (ULONG)extRef;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DPixelShader9::QueryInterface(REFIID riid, void **ppvObj)
{
  if(ppvObj == NULL)
    return E_POINTER;

  if(riid == IID_ID3D9WrappedResource)
  {
    *ppvObj = &m_WrappedInfo;
    return S_OK;
  }
  if(riid == __uuidof(IUnknown))
  {
    *ppvObj = (IUnknown *)(IDirect3DPixelShader9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DPixelShader9))
  {
    *ppvObj = (IDirect3DPixelShader9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DPixelShader9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DPixelShader9::GetFunction(void *pData, UINT *pSizeOfData)
{
  if(pSizeOfData == NULL)
    return D3DERR_INVALIDCALL;

  UINT size = (UINT)(m_Bytecode.count() * sizeof(DWORD));

  if(pData == NULL)
  {
    *pSizeOfData = size;
    return D3D_OK;
  }

  if(*pSizeOfData < size)
  {
    *pSizeOfData = size;
    return D3DERR_MOREDATA;
  }

  memcpy(pData, m_Bytecode.data(), size);
  *pSizeOfData = size;
  return D3D_OK;
}

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DVertexDeclaration9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DVertexDeclaration9::WrappedIDirect3DVertexDeclaration9(
    IDirect3DVertexDeclaration9 *real, WrappedIDirect3DDevice9 *device, ResourceId id)
    : m_pReal(real), m_pDevice(device), m_ExtRef(1), m_IntRef(0)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;
  m_WrappedInfo = {D3D9WrappedType::VertexDeclaration, m_ID, m_pReal};

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DVertexDeclaration9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);

  // Capture the vertex elements from the real object
  UINT numElements = 0;
  m_pReal->GetDeclaration(NULL, &numElements);
  if(numElements > 0)
  {
    m_Elements.resize(numElements);
    m_pReal->GetDeclaration(m_Elements.data(), &numElements);
  }
}

WrappedIDirect3DVertexDeclaration9::~WrappedIDirect3DVertexDeclaration9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DVertexDeclaration9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DVertexDeclaration9::Release()
{
  Atomic::Dec32(&m_ExtRef);
  RDCASSERT(m_ExtRef >= 0);

  int32_t extRef = m_ExtRef;
  int32_t intRef = m_IntRef;

  WrappedIDirect3DDevice9 *dev = m_pDevice;

  if(extRef + intRef == 0)
  {
    delete this;
    dev->Release();
    return 0;
  }

  if(extRef == 0)
    dev->Release();

  return (ULONG)extRef;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexDeclaration9::QueryInterface(REFIID riid,
                                                                              void **ppvObj)
{
  if(ppvObj == NULL)
    return E_POINTER;

  if(riid == IID_ID3D9WrappedResource)
  {
    *ppvObj = &m_WrappedInfo;
    return S_OK;
  }
  if(riid == __uuidof(IUnknown))
  {
    *ppvObj = (IUnknown *)(IDirect3DVertexDeclaration9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DVertexDeclaration9))
  {
    *ppvObj = (IDirect3DVertexDeclaration9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexDeclaration9::GetDevice(
    IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexDeclaration9::GetDeclaration(
    D3DVERTEXELEMENT9 *pElement, UINT *pNumElements)
{
  if(pNumElements == NULL)
    return D3DERR_INVALIDCALL;

  UINT count = (UINT)m_Elements.count();

  if(pElement == NULL)
  {
    *pNumElements = count;
    return D3D_OK;
  }

  memcpy(pElement, m_Elements.data(), count * sizeof(D3DVERTEXELEMENT9));
  *pNumElements = count;
  return D3D_OK;
}

///////////////////////////////////////////////////////////////////////////
// Device-side creation serialization
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateVertexShader(SerialiserType &ser,
                                                            const DWORD *pFunction,
                                                            IDirect3DVertexShader9 **ppShader)
{
  // Calculate function size by scanning for END token (0x0000FFFF)
  UINT functionSize = 0;
  if(ser.IsWriting())
  {
    const DWORD *ptr = pFunction;
    while(*ptr != 0x0000FFFF)
    {
      ptr++;
      functionSize += 4;
    }
    functionSize += 4;    // include END token
  }

  SERIALISE_ELEMENT(functionSize).Important();
  SERIALISE_ELEMENT_ARRAY(pFunction, functionSize / sizeof(DWORD));

  SERIALISE_ELEMENT_LOCAL(pShader, GetIDForD3D9Resource(*ppShader))
      .TypedAs("IDirect3DVertexShader9 *"_lit);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DVertexShader9 *real = NULL;
    HRESULT hr = m_pDevice->CreateVertexShader(pFunction, &real);

    if(FAILED(hr))
    {
      RDCERR("Failed to create vertex shader on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DVertexShader9(real, this, pShader);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreatePixelShader(SerialiserType &ser,
                                                           const DWORD *pFunction,
                                                           IDirect3DPixelShader9 **ppShader)
{
  // Calculate function size by scanning for END token (0x0000FFFF)
  UINT functionSize = 0;
  if(ser.IsWriting())
  {
    const DWORD *ptr = pFunction;
    while(*ptr != 0x0000FFFF)
    {
      ptr++;
      functionSize += 4;
    }
    functionSize += 4;    // include END token
  }

  SERIALISE_ELEMENT(functionSize).Important();
  SERIALISE_ELEMENT_ARRAY(pFunction, functionSize / sizeof(DWORD));

  SERIALISE_ELEMENT_LOCAL(pShader, GetIDForD3D9Resource(*ppShader))
      .TypedAs("IDirect3DPixelShader9 *"_lit);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DPixelShader9 *real = NULL;
    HRESULT hr = m_pDevice->CreatePixelShader(pFunction, &real);

    if(FAILED(hr))
    {
      RDCERR("Failed to create pixel shader on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DPixelShader9(real, this, pShader);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateVertexDeclaration(
    SerialiserType &ser, const D3DVERTEXELEMENT9 *pVertexElements,
    IDirect3DVertexDeclaration9 **ppDecl)
{
  // Count elements by scanning for D3DDECL_END sentinel (Stream == 0xFF)
  UINT numElements = 0;
  if(ser.IsWriting())
  {
    const D3DVERTEXELEMENT9 *elem = pVertexElements;
    while(elem->Stream != 0xFF)
    {
      elem++;
      numElements++;
    }
    numElements++;    // include end sentinel
  }

  SERIALISE_ELEMENT(numElements).Important();
  SERIALISE_ELEMENT_ARRAY(pVertexElements, numElements);

  SERIALISE_ELEMENT_LOCAL(pDecl, GetIDForD3D9Resource(*ppDecl))
      .TypedAs("IDirect3DVertexDeclaration9 *"_lit);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DVertexDeclaration9 *real = NULL;
    HRESULT hr = m_pDevice->CreateVertexDeclaration(pVertexElements, &real);

    if(FAILED(hr))
    {
      RDCERR("Failed to create vertex declaration on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DVertexDeclaration9(real, this, pDecl);
    }
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Template instantiations for serialization
///////////////////////////////////////////////////////////////////////////

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateVertexShader,
                                CONST DWORD *pFunction, IDirect3DVertexShader9 **ppShader);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreatePixelShader,
                                CONST DWORD *pFunction, IDirect3DPixelShader9 **ppShader);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateVertexDeclaration,
                                CONST D3DVERTEXELEMENT9 *pVertexElements,
                                IDirect3DVertexDeclaration9 **ppDecl);

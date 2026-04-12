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

#include "d3d9_stateblock.h"
#include "d3d9_resources.h"

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DStateBlock9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DStateBlock9::WrappedIDirect3DStateBlock9(IDirect3DStateBlock9 *real,
                                                         WrappedIDirect3DDevice9 *device,
                                                         D3DSTATEBLOCKTYPE type, ResourceId id)
    : m_pReal(real), m_pDevice(device), m_ExtRef(1), m_IntRef(0), m_Type(type)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DStateBlock9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);
}

WrappedIDirect3DStateBlock9::~WrappedIDirect3DStateBlock9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DStateBlock9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DStateBlock9::Release()
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

HRESULT STDMETHODCALLTYPE WrappedIDirect3DStateBlock9::QueryInterface(REFIID riid, void **ppvObj)
{
  if(ppvObj == NULL)
    return E_POINTER;

  if(riid == __uuidof(IUnknown))
  {
    *ppvObj = (IUnknown *)(IDirect3DStateBlock9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DStateBlock9))
  {
    *ppvObj = (IDirect3DStateBlock9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DStateBlock9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DStateBlock9::Capture()
{
  HRESULT ret = m_pReal->Capture();

  if(IsActiveCapturing(m_pDevice->GetState()))
  {
    SCOPED_LOCK(m_pDevice->D3DLock());
    WriteSerialiser &ser = m_pDevice->GetScratchSerialiser();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::StateBlockCapture);
    m_pDevice->Serialise_StateBlockCapture(ser, this);
    m_pDevice->GetDeviceRecord()->AddChunk(scope.Get());
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DStateBlock9::Apply()
{
  HRESULT ret = m_pReal->Apply();

  if(IsActiveCapturing(m_pDevice->GetState()))
  {
    SCOPED_LOCK(m_pDevice->D3DLock());
    WriteSerialiser &ser = m_pDevice->GetScratchSerialiser();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::StateBlockApply);
    m_pDevice->Serialise_StateBlockApply(ser, this);
    m_pDevice->GetDeviceRecord()->AddChunk(scope.Get());
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// Device-side creation serialization
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateStateBlock(SerialiserType &ser,
                                                          D3DSTATEBLOCKTYPE Type,
                                                          IDirect3DStateBlock9 **ppSB)
{
  SERIALISE_ELEMENT(Type).Important();
  SERIALISE_ELEMENT_LOCAL(pSB, GetIDForD3D9Resource(*ppSB))
      .TypedAs("IDirect3DStateBlock9 *"_lit);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DStateBlock9 *real = NULL;
    HRESULT hr = m_pDevice->CreateStateBlock(Type, &real);

    if(FAILED(hr))
    {
      RDCERR("Failed to create state block on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DStateBlock9(real, this, Type, pSB);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_BeginStateBlock(SerialiserType &ser)
{
  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->BeginStateBlock();
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_EndStateBlock(SerialiserType &ser,
                                                       IDirect3DStateBlock9 **ppSB)
{
  SERIALISE_ELEMENT_LOCAL(pSB, GetIDForD3D9Resource(*ppSB))
      .TypedAs("IDirect3DStateBlock9 *"_lit);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DStateBlock9 *real = NULL;
    HRESULT hr = m_pDevice->EndStateBlock(&real);

    if(FAILED(hr))
    {
      RDCERR("Failed to end state block on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DStateBlock9(real, this, (D3DSTATEBLOCKTYPE)0, pSB);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_StateBlockCapture(SerialiserType &ser,
                                                           IDirect3DStateBlock9 *pSB)
{
  ResourceId id;
  if(ser.IsWriting())
    id = GetIDForD3D9Resource(pSB);
  SERIALISE_ELEMENT(id).Named("StateBlock"_lit).TypedAs("IDirect3DStateBlock9 *"_lit);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    if(id != ResourceId())
    {
      IUnknown *res = GetResourceManager()->GetResource(id);
      if(res)
      {
        WrappedIDirect3DStateBlock9 *wrappedSB =
            dynamic_cast<WrappedIDirect3DStateBlock9 *>(res);
        if(wrappedSB)
          wrappedSB->GetReal()->Capture();
      }
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_StateBlockApply(SerialiserType &ser,
                                                         IDirect3DStateBlock9 *pSB)
{
  ResourceId id;
  if(ser.IsWriting())
    id = GetIDForD3D9Resource(pSB);
  SERIALISE_ELEMENT(id).Named("StateBlock"_lit).TypedAs("IDirect3DStateBlock9 *"_lit);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    if(id != ResourceId())
    {
      IUnknown *res = GetResourceManager()->GetResource(id);
      if(res)
      {
        WrappedIDirect3DStateBlock9 *wrappedSB =
            dynamic_cast<WrappedIDirect3DStateBlock9 *>(res);
        if(wrappedSB)
          wrappedSB->GetReal()->Apply();
      }
    }
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Template instantiations for serialization
///////////////////////////////////////////////////////////////////////////

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateStateBlock,
                                D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9 **ppSB);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, BeginStateBlock);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, EndStateBlock,
                                IDirect3DStateBlock9 **ppSB);

template bool WrappedIDirect3DDevice9::Serialise_StateBlockCapture(ReadSerialiser &ser,
                                                                    IDirect3DStateBlock9 *pSB);
template bool WrappedIDirect3DDevice9::Serialise_StateBlockCapture(WriteSerialiser &ser,
                                                                    IDirect3DStateBlock9 *pSB);

template bool WrappedIDirect3DDevice9::Serialise_StateBlockApply(ReadSerialiser &ser,
                                                                  IDirect3DStateBlock9 *pSB);
template bool WrappedIDirect3DDevice9::Serialise_StateBlockApply(WriteSerialiser &ser,
                                                                  IDirect3DStateBlock9 *pSB);

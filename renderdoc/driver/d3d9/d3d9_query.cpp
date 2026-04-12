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

#include "d3d9_query.h"
#include "d3d9_resources.h"

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DQuery9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DQuery9::WrappedIDirect3DQuery9(IDirect3DQuery9 *real,
                                               WrappedIDirect3DDevice9 *device, D3DQUERYTYPE type,
                                               ResourceId id)
    : m_pReal(real), m_pDevice(device), m_ExtRef(1), m_IntRef(0), m_Type(type)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;
  m_WrappedInfo = {D3D9WrappedType::Query, m_ID, m_pReal};

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DQuery9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);
}

WrappedIDirect3DQuery9::~WrappedIDirect3DQuery9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DQuery9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DQuery9::Release()
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

HRESULT STDMETHODCALLTYPE WrappedIDirect3DQuery9::QueryInterface(REFIID riid, void **ppvObj)
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
    *ppvObj = (IUnknown *)(IDirect3DQuery9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DQuery9))
  {
    *ppvObj = (IDirect3DQuery9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DQuery9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

D3DQUERYTYPE STDMETHODCALLTYPE WrappedIDirect3DQuery9::GetType()
{
  return m_Type;
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DQuery9::GetDataSize()
{
  return m_pReal->GetDataSize();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DQuery9::Issue(DWORD dwIssueFlags)
{
  HRESULT ret = m_pReal->Issue(dwIssueFlags);

  if(SUCCEEDED(ret) && IsActiveCapturing(m_pDevice->GetState()))
  {
    SCOPED_LOCK(m_pDevice->D3DLock());
    WriteSerialiser &ser = m_pDevice->GetScratchSerialiser();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::QueryIssue);
    m_pDevice->Serialise_QueryIssue(ser, this, dwIssueFlags);
    m_pDevice->GetDeviceRecord()->AddChunk(scope.Get());
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DQuery9::GetData(void *pData, DWORD dwSize,
                                                           DWORD dwGetDataFlags)
{
  return m_pReal->GetData(pData, dwSize, dwGetDataFlags);
}

///////////////////////////////////////////////////////////////////////////
// Device-side creation serialization
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateQuery(SerialiserType &ser, D3DQUERYTYPE Type,
                                                     IDirect3DQuery9 **ppQuery)
{
  SERIALISE_ELEMENT(Type).Important();
  SERIALISE_ELEMENT_LOCAL(pQuery, GetIDForD3D9Resource(*ppQuery))
      .TypedAs("IDirect3DQuery9 *"_lit);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DQuery9 *real = NULL;
    HRESULT hr = m_pDevice->CreateQuery(Type, &real);

    if(FAILED(hr))
    {
      RDCERR("Failed to create query on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DQuery9(real, this, Type, pQuery);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_QueryIssue(SerialiserType &ser, IDirect3DQuery9 *pQuery,
                                                    DWORD dwIssueFlags)
{
  ResourceId id;
  if(ser.IsWriting())
    id = GetIDForD3D9Resource(pQuery);
  SERIALISE_ELEMENT(id).Named("Query"_lit).TypedAs("IDirect3DQuery9 *"_lit);
  SERIALISE_ELEMENT(dwIssueFlags);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    if(id != ResourceId())
    {
      IUnknown *res = GetResourceManager()->GetResource(id);
      if(res)
      {
        D3D9WrappedInfo *info = GetD3D9WrappedInfo(res);
        if(info)
          ((IDirect3DQuery9 *)info->realObject)->Issue(dwIssueFlags);
      }
    }
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Template instantiations for serialization
///////////////////////////////////////////////////////////////////////////

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateQuery, D3DQUERYTYPE Type,
                                IDirect3DQuery9 **ppQuery);

template bool WrappedIDirect3DDevice9::Serialise_QueryIssue(ReadSerialiser &ser,
                                                             IDirect3DQuery9 *pQuery,
                                                             DWORD dwIssueFlags);
template bool WrappedIDirect3DDevice9::Serialise_QueryIssue(WriteSerialiser &ser,
                                                             IDirect3DQuery9 *pQuery,
                                                             DWORD dwIssueFlags);

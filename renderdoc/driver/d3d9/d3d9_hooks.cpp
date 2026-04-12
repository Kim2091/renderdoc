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

#include "hooks/hooks.h"
#include "d3d9_d3d9.h"
#include "d3d9_device.h"

typedef int(WINAPI *PFN_BEGIN_EVENT)(DWORD, WCHAR *);
typedef int(WINAPI *PFN_END_EVENT)();
typedef int(WINAPI *PFN_SET_MARKER_EVENT)(DWORD, WCHAR *);
typedef void(WINAPI *PFN_SET_OPTIONS)(DWORD);
typedef DWORD(WINAPI *PFN_GET_OPTIONS)();

class D3D9Hook : LibraryHook
{
public:
  void RegisterHooks()
  {
    RDCLOG("Registering D3D9 hooks");

    LibraryHooks::RegisterLibraryHook("d3d9.dll", NULL);

    Direct3DCreate9_hook.Register("d3d9.dll", "Direct3DCreate9", Direct3DCreate9_hook_fn);

    // D3DPERF hooks
    PERF_BeginEvent.Register("d3d9.dll", "D3DPERF_BeginEvent", PERF_BeginEvent_hook);
    PERF_EndEvent.Register("d3d9.dll", "D3DPERF_EndEvent", PERF_EndEvent_hook);
    PERF_SetMarker.Register("d3d9.dll", "D3DPERF_SetMarker", PERF_SetMarker_hook);
    PERF_SetOptions.Register("d3d9.dll", "D3DPERF_SetOptions", PERF_SetOptions_hook);
    PERF_GetStatus.Register("d3d9.dll", "D3DPERF_GetStatus", PERF_GetStatus_hook);

    m_RecurseSlot = Threading::AllocateTLSSlot();
    Threading::SetTLSValue(m_RecurseSlot, NULL);
  }

private:
  static D3D9Hook d3d9hooks;

  // re-entrancy detection
  uint64_t m_RecurseSlot = 0;

  bool CheckRecurse() { return Threading::GetTLSValue(m_RecurseSlot) != 0; }
  void BeginRecurse() { Threading::SetTLSValue(m_RecurseSlot, (void *)1); }
  void EndRecurse() { Threading::SetTLSValue(m_RecurseSlot, NULL); }

  ////////////////////////////////////////////////////////////////
  // Direct3DCreate9 hook

  HookedFunction<decltype(&Direct3DCreate9)> Direct3DCreate9_hook;

  static IDirect3D9 *WINAPI Direct3DCreate9_hook_fn(UINT SDKVersion)
  {
    RDCLOG("Direct3DCreate9 called (SDKVersion=%u)", SDKVersion);

    if(d3d9hooks.CheckRecurse())
      return d3d9hooks.Direct3DCreate9_hook()(SDKVersion);

    d3d9hooks.BeginRecurse();
    IDirect3D9 *real = d3d9hooks.Direct3DCreate9_hook()(SDKVersion);
    d3d9hooks.EndRecurse();

    if(real == NULL)
    {
      RDCERR("Direct3DCreate9 returned NULL");
      return NULL;
    }

    RDCLOG("Wrapping IDirect3D9 %p", real);
    return new WrappedIDirect3D9(real);
  }

  ////////////////////////////////////////////////////////////////
  // D3DPERF hooks

  HookedFunction<PFN_BEGIN_EVENT> PERF_BeginEvent;
  HookedFunction<PFN_END_EVENT> PERF_EndEvent;
  HookedFunction<PFN_SET_MARKER_EVENT> PERF_SetMarker;
  HookedFunction<PFN_SET_OPTIONS> PERF_SetOptions;
  HookedFunction<PFN_GET_OPTIONS> PERF_GetStatus;

  static int WINAPI PERF_BeginEvent_hook(DWORD col, WCHAR *wszName)
  {
    int ret = WrappedIDirect3DDevice9::BeginEvent((uint32_t)col, wszName);

    d3d9hooks.PERF_BeginEvent()(col, wszName);

    return ret;
  }

  static int WINAPI PERF_EndEvent_hook()
  {
    int ret = WrappedIDirect3DDevice9::EndEvent();

    d3d9hooks.PERF_EndEvent()();

    return ret;
  }

  static void WINAPI PERF_SetMarker_hook(DWORD col, WCHAR *wszName)
  {
    WrappedIDirect3DDevice9::SetMarker((uint32_t)col, wszName);

    d3d9hooks.PERF_SetMarker()(col, wszName);
  }

  static void WINAPI PERF_SetOptions_hook(DWORD dwOptions)
  {
    if(dwOptions & 1)
      RDCLOG("Application requested not to be hooked via D3DPERF_SetOptions: no longer supported.");
  }

  static DWORD WINAPI PERF_GetStatus_hook() { return 1; }
};

D3D9Hook D3D9Hook::d3d9hooks;

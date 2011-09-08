/*
 *  Copyright (C) 2005-2011 Team MediaPortal
 *  http://www.team-mediaportal.com
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#pragma warning(disable:4996)
#pragma warning(disable:4995)
#include "StdAfx.h"

#include <commdlg.h>
#include <time.h>
#include <streams.h>
#include <initguid.h>
#include <shlobj.h>
#include <tchar.h>
#include "bdreader.h"
#include "audiopin.h"
#include "videopin.h"
#include "subtitlepin.h"
#include "..\..\shared\DebugSettings.h"
#include <cassert>

#include <keys.h>

// For more details for memory leak detection see the alloctracing.h header
#include "..\..\alloctracing.h"

#define CONVERT_90KHz_DS(x) (REFERENCE_TIME)(x * (1000.0 / 9.0))
#define CONVERT_DS_90KHz(x) (REFERENCE_TIME)(x / (1000.0 / 9.0))

static char logFile[MAX_PATH];
static WORD logFileParsed = -1;

void GetLogFile(char *pLog)
{
  SYSTEMTIME systemTime;
  GetLocalTime(&systemTime);
  if(logFileParsed != systemTime.wDay)
  {
    TCHAR folder[MAX_PATH];
    ::SHGetSpecialFolderPath(NULL,folder,CSIDL_COMMON_APPDATA,FALSE);
    sprintf(logFile,"%s\\Team MediaPortal\\MediaPortal\\Log\\BDReader-%04.4d-%02.2d-%02.2d.Log",folder, systemTime.wYear, systemTime.wMonth, systemTime.wDay);
    logFileParsed=systemTime.wDay; // rec
  }
  strcpy(pLog, &logFile[0]);
}

void LogDebug(const char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);

  char buffer[1000];
  int tmp;
  va_start(ap, fmt);
  tmp = vsprintf(buffer, fmt, ap);
  va_end(ap);
  SYSTEMTIME systemTime;
  GetLocalTime(&systemTime);

//#ifdef DONTLOG
  TCHAR filename[1024];
  GetLogFile(filename);
  FILE* fp = fopen(filename,"a+");

  if (fp != NULL)
  {
    fprintf(fp,"%02.2d-%02.2d-%04.4d %02.2d:%02.2d:%02.2d.%03.3d [%5x]%s\n",
      systemTime.wDay, systemTime.wMonth, systemTime.wYear,
      systemTime.wHour,systemTime.wMinute,systemTime.wSecond,
      systemTime.wMilliseconds,
      GetCurrentThreadId(),
      buffer);
    fclose(fp);
  }
//#endif
  char buf[1000];
  sprintf(buf,"%02.2d-%02.2d-%04.4d %02.2d:%02.2d:%02.2d %s\n",
    systemTime.wDay, systemTime.wMonth, systemTime.wYear,
    systemTime.wHour,systemTime.wMinute,systemTime.wSecond,
    buffer);
  //::OutputDebugString(buf);
};


const AMOVIESETUP_MEDIATYPE acceptAudioPinTypes =
{
  &MEDIATYPE_Audio,             // major type
  &MEDIASUBTYPE_MPEG1Audio      // minor type
};
const AMOVIESETUP_MEDIATYPE acceptVideoPinTypes =
{
  &MEDIATYPE_Video,             // major type
  &MEDIASUBTYPE_MPEG2_VIDEO     // minor type
};

const AMOVIESETUP_MEDIATYPE acceptSubtitlePinTypes =
{
  &MEDIATYPE_Stream,            // major type
  &MEDIASUBTYPE_MPEG2_TRANSPORT // minor type
};

const AMOVIESETUP_PIN pins[] =
{
  {L"Audio", FALSE, TRUE, FALSE, FALSE, &CLSID_NULL, NULL, 1, &acceptAudioPinTypes},
  {L"Video", FALSE, TRUE, FALSE, FALSE, &CLSID_NULL, NULL, 1, &acceptVideoPinTypes},
  {L"Subtitle", FALSE, TRUE, FALSE, FALSE, &CLSID_NULL, NULL, 1, &acceptSubtitlePinTypes}
};

const AMOVIESETUP_FILTER BDReader =
{
  &CLSID_BDReader, L"MediaPortal BD Reader", MERIT_NORMAL + 1000, 3, pins
};

CFactoryTemplate g_Templates[] =
{
  {L"MediaPortal BD Reader", &CLSID_BDReader, CBDReaderFilter::CreateInstance, NULL, &BDReader},
};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

CUnknown * WINAPI CBDReaderFilter::CreateInstance(LPUNKNOWN punk, HRESULT *phr)
{
  ASSERT(phr);
  CBDReaderFilter *pNewObject = new CBDReaderFilter(punk, phr);

  if (pNewObject == NULL)
  {
    if (phr)
      *phr = E_OUTOFMEMORY;
  }
  return pNewObject;
}

// Constructor
CBDReaderFilter::CBDReaderFilter(IUnknown *pUnk, HRESULT *phr):
  CSource(NAME("CBDReaderFilter"), pUnk, CLSID_BDReader),
  m_pAudioPin(NULL),
  m_demultiplexer(*this),
  m_pDVBSubtitle(NULL),
  m_pCallback(NULL),
  m_pRequestAudioCallback(NULL),
  m_pEvr(NULL),
  m_hCommandThread(NULL),
  m_hCommandEvent(NULL),
  m_hStopCommandThreadEvent(NULL),
  m_bIgnoreLibSeeking(false),
  m_dwThreadId(0),
  m_pMediaSeeking(NULL)
{
  // use the following line if you are having trouble setting breakpoints
  // #pragma comment( lib, "strmbasd" )
  TCHAR filename[1024];
  GetLogFile(filename);
  ::DeleteFile(filename);
  LogDebug("--------- bluray ---------------------");
  LogDebug("-------------- v0.5.1 ----------------");

  LogDebug("CBDReaderFilter::ctor");
  m_pAudioPin = new CAudioPin(GetOwner(), this, phr, &m_section);
  m_pVideoPin = new CVideoPin(GetOwner(), this, phr, &m_section);
  m_pSubtitlePin = new CSubtitlePin(GetOwner(), this, phr, &m_section);

  if (!m_pAudioPin || !m_pVideoPin || !m_pSubtitlePin)
  {
    *phr = E_OUTOFMEMORY;
    return;
  }
  wcscpy(m_fileName, L"");

  LogDebug("Wait for seeking to eof - false - constructor");
  m_WaitForSeekToEof = 0;
  m_bStopping = false;
  m_bStoppedForUnexpectedSeek = false;
  m_bForceSeekOnStop = false;
  m_bForceSeekAfterRateChange = false;
  m_bSeekAfterRcDone = false;
  m_MPmainThreadID = GetCurrentThreadId();

  lib.Initialize();
  lib.SetEventObserver(this);

  // Manual reset to allow easier command queue handling
  m_hCommandEvent = CreateEvent(NULL, true, false, NULL); 
  m_hStopCommandThreadEvent = CreateEvent(NULL, false, false, NULL);
}

CBDReaderFilter::~CBDReaderFilter()
{
  LogDebug("CBDReaderFilter::dtor");

  if (m_hStopCommandThreadEvent)
  {
    SetEvent(m_hStopCommandThreadEvent);
    WaitForSingleObject(m_hCommandThread, INFINITE);
    CloseHandle(m_hStopCommandThreadEvent);
  }

  if (m_hCommandEvent)
  {
    CloseHandle(m_hCommandEvent);
  }

  lib.RemoveEventObserver(this);
  if (m_pAudioPin)
  {
    m_pAudioPin->Disconnect();
    delete m_pAudioPin;
  }

  if (m_pVideoPin)
  {
    m_pVideoPin->Disconnect();
    delete m_pVideoPin;
  }

  if (m_pSubtitlePin)
  {
    m_pSubtitlePin->Disconnect();
    delete m_pSubtitlePin;
  }

  if (m_pDVBSubtitle)
  {
    m_pDVBSubtitle->Release();
    m_pDVBSubtitle = NULL;
  }
}

STDMETHODIMP CBDReaderFilter::NonDelegatingQueryInterface(REFIID riid, void ** ppv)
{
  if (riid == IID_IMediaSeeking)
  {
    if (m_pAudioPin->IsConnected())
      return m_pAudioPin->NonDelegatingQueryInterface(riid, ppv);

    if (m_pVideoPin->IsConnected())
      return m_pVideoPin->NonDelegatingQueryInterface(riid, ppv);
  }
  if (riid == IID_IAMFilterMiscFlags)
  {
    return GetInterface((IAMFilterMiscFlags*)this, ppv);
  }
  if (riid == IID_IFileSourceFilter)
  {
    return GetInterface((IFileSourceFilter*)this, ppv);
  }
  if (riid == IID_IAMStreamSelect)
  {
    return GetInterface((IAMStreamSelect*)this, ppv);
  }
  if (riid == IID_ISubtitleStream)
  {
    return GetInterface((ISubtitleStream*)this, ppv);
  }
  if ( riid == IID_IBDReader )
  {
    return GetInterface((IBDReader*)this, ppv);
  }
  if ( riid == IID_IAudioStream )
  {
    return GetInterface((IAudioStream*)this, ppv);
  }
  return CSource::NonDelegatingQueryInterface(riid, ppv);
}

CBasePin * CBDReaderFilter::GetPin(int n)
{
  if (n == 0)
  {
    return m_pAudioPin;
  }
  else  if (n == 1)
  {
    return m_pVideoPin;
  }
  else if (n == 2)
  {
    return m_pSubtitlePin;
  }
  return NULL;
}

int CBDReaderFilter::GetPinCount()
{
  return 3;
}

void CBDReaderFilter::IssueCommand(DS_CMD_ID pCommand, REFERENCE_TIME pTime)
{
  {
    CAutoLock lock(&m_csCommandQueue);
    DS_CMD cmd;
    cmd.id = pCommand;
    cmd.refTime = pTime;
    m_commandQueue.push_back(cmd);
  }
  
  SetEvent(m_hCommandEvent);
}

void CBDReaderFilter::TriggerOnMediaChanged()
{
  if (m_pCallback)
  {		
    m_demultiplexer.SetMediaChanging(true);

    CMediaType pmt;
    int audioStream = 0;
    int videoRate = 0;
    
    BLURAY_CLIP_INFO* clip = lib.CurrentClipInfo();
    if (clip)
    {
      videoRate = clip->video_streams->rate;
    }
    
    m_demultiplexer.GetAudioStream(audioStream);
    m_demultiplexer.GetAudioStreamType(audioStream, pmt);
    
    HRESULT hr = m_pCallback->OnMediaTypeChanged(videoRate, m_demultiplexer.GetVideoServiceType(), *pmt.Subtype());

    if (hr == S_FALSE)
    {
      // There was no need for the graph rebuilding
      m_demultiplexer.SetMediaChanging(false);
    }
  }
  else
  {
    LogDebug("CBDReaderFilter: TriggerOnMediaChanged - no callback set!");
  }
}

STDMETHODIMP CBDReaderFilter::SetGraphCallback(IBDReaderCallback* pCallback)
{
  LogDebug("callback set");
  m_pCallback = pCallback;
  return S_OK;
}

STDMETHODIMP CBDReaderFilter::Action(int key)
{
  lib.LogAction(key);

  INT64 pos = -1;

  if (m_pMediaSeeking)
  {
    HRESULT hr = m_pMediaSeeking->GetCurrentPosition(&pos);
  }
  
  switch (key)
  {  
    case BD_VK_0:
    case BD_VK_1:
    case BD_VK_2:
    case BD_VK_3:
    case BD_VK_4:
    case BD_VK_5:
    case BD_VK_6:
    case BD_VK_7:
    case BD_VK_8:
    case BD_VK_9:
    case BD_VK_UP:
    case BD_VK_DOWN:
    case BD_VK_LEFT:
    case BD_VK_RIGHT:
    case BD_VK_ENTER:
    case BD_VK_POPUP:
    case BD_VK_ROOT_MENU:
    case BD_VK_MOUSE_ACTIVATE:
      return lib.ProvideUserInput(CONVERT_DS_90KHz(pos), (UINT32)key) == true ? S_OK : S_FALSE;
    break;
    default:  
      return S_FALSE;
  }
  return S_FALSE;
}

STDMETHODIMP CBDReaderFilter::SetAngle(UINT8 angle)
{
  return lib.SetAngle(angle) ? S_OK : S_FALSE;
}

STDMETHODIMP CBDReaderFilter::SetChapter(UINT32 chapter)
{
  HRESULT hr = lib.SetChapter(chapter) ? S_OK : S_FALSE;
  if (hr == S_OK) m_demultiplexer.Flush();
  return hr;
}

STDMETHODIMP CBDReaderFilter::GetAngle(UINT8* angle)
{
  return lib.GetAngle(angle) ? S_OK : S_FALSE;
}

STDMETHODIMP CBDReaderFilter::GetChapter(UINT32* chapter)
{
  return lib.GetChapter(chapter) ? S_OK : S_FALSE;
}

STDMETHODIMP CBDReaderFilter::GetTitleCount(UINT32* count)
{
  (*count) = lib.GetTitles(TITLES_ALL);
  return S_OK;
}

STDMETHODIMP CBDReaderFilter::MouseMove(UINT16 x, UINT16 y)
{
  INT64 pos = -1;

  if (m_pMediaSeeking)
  {
    HRESULT hr = m_pMediaSeeking->GetCurrentPosition(&pos);
    if (SUCCEEDED(hr))
    {
      pos = CONVERT_DS_90KHz(pos);
    }
  }

  lib.MouseMove(CONVERT_DS_90KHz(pos), x, y);
  return S_OK;
}

BLURAY_TITLE_INFO* STDMETHODCALLTYPE CBDReaderFilter::GetTitleInfo(UINT32 pIndex)
{
  return lib.GetTitleInfo(pIndex);
}

STDMETHODIMP CBDReaderFilter::FreeTitleInfo(BLURAY_TITLE_INFO* info)
{
  lib.FreeTitleInfo(info);
  return S_OK;
}

void STDMETHODCALLTYPE CBDReaderFilter::OnGraphRebuild(int info)
{
  LogDebug("CBDReaderFilter::OnGraphRebuild %d", info);
  m_demultiplexer.SetMediaChanging(false);

  m_bForceSeekOnStop = true;
}

DWORD WINAPI CBDReaderFilter::CommandThreadEntryPoint(LPVOID lpParameter)
{
  return ((CBDReaderFilter*)lpParameter)->CommandThread();
}

DWORD WINAPI CBDReaderFilter::CommandThread()
{
  IFilterGraph* pGraph = NULL;    

  pGraph = GetFilterGraph();

  if (pGraph)
  {
    pGraph->QueryInterface(&m_pMediaSeeking);
    pGraph->Release();
  }

  HANDLE handles[2];
  handles[0] = m_hStopCommandThreadEvent;
  handles[1] = m_hCommandEvent;

  ResetEvent(m_hCommandEvent);

  if (m_pMediaSeeking)
  {
    while(1)
    {
      DWORD result = WaitForMultipleObjects(2, handles, false, INFINITE);
      if (result == WAIT_OBJECT_0) // exit event
      {
        LogDebug("CBDReaderFilter::Command thread: closing down");
        return 0;
      }
      else if (result == WAIT_OBJECT_0 + 1) // command in queue
      {
        LONGLONG posEnd(~0);
        LONGLONG zeroPos(0);

        ivecCommandQueue it;
        DS_CMD cmd;

        { // just fetch the command and release the lock
          CAutoLock lock(&m_csCommandQueue);
          it = m_commandQueue.begin();
          cmd = (*it);
          m_commandQueue.erase(it);
          if (m_commandQueue.empty())
          {
            ResetEvent(m_hCommandEvent);
          }
        }

        switch (cmd.id)
        {
        case REBUILD:
          LogDebug("CBDReaderFilter::Command thread: issue rebuild!");
          TriggerOnMediaChanged();

          // TODO add media changed event so we don't have to poll
          while (m_demultiplexer.IsMediaChanging())
          {
            Sleep(1);
          }

          m_bIgnoreLibSeeking = true;

          LogDebug("CBDReaderFilter::Command thread: seek requested - pos: %06.3f", zeroPos / 1000000.0);
          m_pMediaSeeking->SetPositions(&zeroPos, AM_SEEKING_AbsolutePositioning, &posEnd, AM_SEEKING_NoPositioning);

          break;

        case FLUSH:
          m_bIgnoreLibSeeking = true;
          m_bForceSeekAfterRateChange = true;

          LogDebug("CBDReaderFilter::Command thread: flush requested - pos: %06.3f", cmd.refTime.Millisecs() / 1000.0);
          m_demultiplexer.Flush();

          m_pMediaSeeking->SetPositions(&zeroPos, AM_SEEKING_AbsolutePositioning, &posEnd, AM_SEEKING_NoPositioning);

          break;

        case SEEK:
          m_bIgnoreLibSeeking = true;
          
          LogDebug("CBDReaderFilter::Command thread: seek requested - pos: %06.3f", cmd.refTime.Millisecs() / 1000.0);
          HRESULT hr = m_pMediaSeeking->SetPositions((LONGLONG*)&cmd.refTime.m_time, AM_SEEKING_AbsolutePositioning | AM_SEEKING_NoFlush, &posEnd, AM_SEEKING_NoPositioning);
          break;
        }
      }
      else
      {
        DWORD error = GetLastError();
        LogDebug("CBDReaderFilter::Command thread: WaitForMultipleObjects failed: %d", error);
      }
    }
  }

  return 0;
}

STDMETHODIMP CBDReaderFilter::Run(REFERENCE_TIME tStart)
{
  CRefTime runTime = tStart;
  double msec = (double)runTime.Millisecs();
  msec /= 1000.0;
  LogDebug("CBDReaderFilter::Run(%05.2f) state %d seeking %d", msec / 1000.0, m_State, IsSeeking());
  
  CAutoLock cObjectLock(m_pLock);
  lib.SetState(State_Running);  
  
  m_bSeekAfterRcDone = false;

  if (m_pSubtitlePin) 
    m_pSubtitlePin->SetRunningStatus(true);
	
  // Set our StreamTime Reference offset to zero
  HRESULT hr = CSource::Run(tStart);

  FindSubtitleFilter();
  LogDebug("CBDReaderFilter::Run(%05.2f) state %d -->done", msec / 1000.0, m_State);

  if (!m_hCommandThread)
  {
    m_hCommandThread = CreateThread(NULL, 0, CBDReaderFilter::CommandThreadEntryPoint, (LPVOID)this, 0, &m_dwThreadId);
  }

  return hr;
}

STDMETHODIMP CBDReaderFilter::Stop()
{
  LogDebug("CBDReaderFilter::Stop()");

  CAutoLock cObjectLock(m_pLock);
  lib.SetState(State_Stopped);

  m_bStopping = true;

  if (m_pSubtitlePin)
  {
    m_pSubtitlePin->SetRunningStatus(false);
  }

  LogDebug("CBDReaderFilter::Stop()  -stop source");
  HRESULT hr = CSource::Stop();
  LogDebug("CBDReaderFilter::Stop()  -stop source done");

  m_bSeekAfterRcDone = false;
  m_bStopping = false;

  m_bStoppedForUnexpectedSeek = true;
  LogDebug("CBDReaderFilter::Stop() done");

  return hr;
}

STDMETHODIMP CBDReaderFilter::Pause()
{
  LogDebug("CBDReaderFilter::Pause() - state = %d", m_State);

  CAutoLock cObjectLock(m_pLock);
  lib.SetState(State_Paused);

  if (m_State == State_Running)
  {
    m_lastPause = GetTickCount();
  }

  HRESULT hr = CSource::Pause();

  LogDebug("CBDReaderFilter::Pause() - END - state = %d", m_State);
  return hr;
}

STDMETHODIMP CBDReaderFilter::GetDuration(REFERENCE_TIME* pDuration)
{
  if (!pDuration)
    return E_INVALIDARG;

  ULONGLONG pos = 0, dur = 0;

  if (lib.CurrentPosition(pos, dur))
  {
    *pDuration = CONVERT_90KHz_DS(dur);
  }
  else
  {
    pDuration = 0;
  }

  return NOERROR;
}

STDMETHODIMP CBDReaderFilter::Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE *pmt)
{
  LogDebug("CBDReaderFilter::Load()");

  wcscpy(m_fileName, pszFileName);
  char path[MAX_PATH];
  WideCharToMultiByte(CP_ACP, 0, m_fileName, -1, path, MAX_PATH, 0, 0);

  int pathLen = strlen(path);
  int extLen = strlen("\\BDMV\\index.bdmv");
  
  // We need at least "\BDMV\index.bdmv"
  if (pathLen < extLen)
    return S_FALSE; // cannot be a path to Blu-ray

  if (strcmp(path + pathLen - 16, "\\BDMV\\index.bdmv") != 0)
    return S_FALSE; // not a BD

  strncpy(m_pathToBD, path, pathLen - extLen);
  m_pathToBD[pathLen - extLen] = '\0';

  lib.OpenBluray(m_pathToBD);
  UINT32 titleCount = lib.GetTitles(TITLES_ALL);

  for (unsigned int i = 0; i < titleCount; i++)
  {
    lib.LogTitleInfo(i, true);
  }

  // Debugging aid - allow GraphEdit to be used
  if (0)
  {
    // Aviator
    lib.ForceTitleBasedPlayback(true);
    lib.SetTitle(5);
    Start();
  }

  return S_OK;
}

STDMETHODIMP CBDReaderFilter::Start()
{
  lib.Play();

  m_seekTime = CRefTime(0L);
  m_absSeekTime = CRefTime(0L);
  m_WaitForSeekToEof = 0;

  HRESULT hr = m_demultiplexer.Start();
  TriggerOnMediaChanged();
  // Close BD so the HDVM etc. are reset completely after querying the initial data.
  // This is required so we wont lose the initial events.
  lib.CloseBluray();
  lib.OpenBluray(m_pathToBD);
  lib.Play();

  return hr;
}


STDMETHODIMP CBDReaderFilter::GetCurFile(LPOLESTR * ppszFileName, AM_MEDIA_TYPE *pmt)
{
  CheckPointer(ppszFileName, E_POINTER);
  *ppszFileName = NULL;

  if (lstrlenW(m_fileName) > 0)
  {
    *ppszFileName = (LPOLESTR)QzTaskMemAlloc(sizeof(WCHAR) * (1 + lstrlenW(m_fileName)));
    wcscpy(*ppszFileName, m_fileName);
  }
  if (pmt)
  {
    ZeroMemory(pmt, sizeof(*pmt));
    pmt->majortype = MEDIATYPE_Stream;
    pmt->subtype = MEDIASUBTYPE_MPEG2_PROGRAM;
  }
  return S_OK;
}

// IAMFilterMiscFlags
ULONG CBDReaderFilter::GetMiscFlags()
{
  return AM_FILTER_MISC_FLAGS_IS_SOURCE;
}

CDeMultiplexer& CBDReaderFilter::GetDemultiplexer()
{
  return m_demultiplexer;
}

void CBDReaderFilter::Seek(CRefTime& seekTime, bool seekInfile)
{
  if (!m_bIgnoreLibSeeking)
  {
    lib.Seek(CONVERT_DS_90KHz(seekTime.m_time));
  }
  m_bIgnoreLibSeeking = false;
}

void CBDReaderFilter::SeekPreStart(CRefTime& rtAbsSeek)
{
  CAutoLock cObjectLock(m_pLock);

  // Should we really seek ?
  // Because all skips generated after "Stop()" cause a lot of problem
  // This remove all these stupid skips. 
  if (m_State == State_Stopped)
  {   
    if ((m_bStoppedForUnexpectedSeek || (m_absSeekTime == rtAbsSeek)) && !m_bForceSeekOnStop && !m_bForceSeekAfterRateChange)
    {
      //LogDebug("CBDReaderFilter::--SeekStart()--   BAIL OUT!");
      m_bStoppedForUnexpectedSeek = false;
      m_absSeekTime = rtAbsSeek;
      m_bIgnoreLibSeeking = false;
      m_WaitForSeekToEof = 0;
      return;
    }
  }

  if (((m_absSeekTime == rtAbsSeek) && !m_bForceSeekAfterRateChange) || (m_demultiplexer.IsMediaChanging() && !m_bForceSeekOnStop && !m_bForceSeekAfterRateChange))
  {
    //LogDebug("CBDReaderFilter::--SeekStart()-- No new seek %f ( Abs %f ) - Force %d, Media changing: %d", 
		//(float)rtAbsSeek.Millisecs() / 1000.0f, (float)rtAbsSeek.Millisecs() / 1000.0f, m_bForceSeekOnStop, m_demultiplexer.IsMediaChanging());
    m_bForceSeekOnStop = false;
    m_bIgnoreLibSeeking = false;
  }
  else
  {
    LogDebug("CBDReaderFilter::--SeekStart()-- %3.3f ( Abs %f ), Force %d, ForceRC %d, Media changing %d",
		  (float)rtAbsSeek.Millisecs() / 1000.0, (float)rtAbsSeek.Millisecs() / 1000.0f, m_bForceSeekOnStop, m_bForceSeekAfterRateChange, m_demultiplexer.IsMediaChanging());

    m_bForceSeekOnStop = false;
    
    if (m_bForceSeekAfterRateChange)
    {
      m_bSeekAfterRcDone = true;
      m_bForceSeekAfterRateChange = false; 
    }

    m_absSeekTime = rtAbsSeek;

    m_WaitForSeekToEof = 1;

    if (m_pAudioPin->IsConnected())
    {
      m_pAudioPin->DeliverBeginFlush();
      m_pAudioPin->Stop();
    }

    if (m_pVideoPin->IsConnected())
    {
      m_pVideoPin->DeliverBeginFlush();
      m_pVideoPin->Stop();
    }

    if (!m_bIgnoreLibSeeking)
    {
      LogDebug("CBDReaderFilter::SeekPreStart - Flush");
      m_demultiplexer.Flush();
    }

    if (!m_demultiplexer.IsMediaChanging())
    {
      Seek(rtAbsSeek, true);
    }
    else
    {
      m_bIgnoreLibSeeking = false;
    }

    REFERENCE_TIME duration;
    GetDuration(&duration);
    if (rtAbsSeek >= duration)
    {
      rtAbsSeek = duration;
    }

    if (m_pAudioPin->IsConnected())
    {
      m_pAudioPin->DeliverEndFlush();
      m_pAudioPin->SetStart(rtAbsSeek);
      m_pAudioPin->Run();
    }

    if (m_pVideoPin->IsConnected())
    {
      m_pVideoPin->DeliverEndFlush();
      m_pVideoPin->SetStart(rtAbsSeek);
      m_pVideoPin->Run();
    }

    if (m_pDVBSubtitle)
    {
      m_pDVBSubtitle->SetFirstPcr(0); // TODO: check if we need ot set the 1st PTS
      m_pDVBSubtitle->SeekDone(rtAbsSeek);
    }
  }

  m_WaitForSeekToEof = 0;
}

// When a IMediaSeeking.SetPositions() is done on one of the output pins the output pin will do:
//  SeekStart() ->indicates to any other output pins we're busy seeking
//  Seek()      ->Does the seeking
//  SeekDone()  ->indicates that seeking has finished
// This prevents the situation where multiple outputpins are seeking in the file at the same time

///Returns the audio output pin
CAudioPin* CBDReaderFilter::GetAudioPin()
{
  return m_pAudioPin;
}

CVideoPin* CBDReaderFilter::GetVideoPin()
{
  return m_pVideoPin;
}

CSubtitlePin* CBDReaderFilter::GetSubtitlePin()
{
  return m_pSubtitlePin;
}

IDVBSubtitle* CBDReaderFilter::GetSubtitleFilter()
{
  return m_pDVBSubtitle;
}

void CBDReaderFilter::HandleBDEvent(BD_EVENT& pEv, UINT64 pPos)
{
  switch (pEv.event)
  {
    case BD_EVENT_SEEK:
      break;

    case BD_EVENT_STILL_TIME:
      break;

    case BD_EVENT_STILL:
      break;

    case BD_EVENT_TITLE:
      break;

    case BD_EVENT_PLAYLIST:
      break;
  }

  // Send event to the callback - filter out the none events
  if (m_pCallback && pEv.event != BD_EVENT_NONE)
  {
    m_pCallback->OnBDEvent(pEv);
  }
}

void CBDReaderFilter::HandleOSDUpdate(OSDTexture& pTexture)
{
  if (m_pCallback)
  {
    m_pCallback->OnOSDUpdate(pTexture);
  }
}

void CBDReaderFilter::HandleMenuStateChange(bool pVisible)
{
  BD_EVENT ev;
  ev.event = BD_CUSTOM_EVENT_MENU_VISIBILITY;
  ev.param = pVisible ? 1 : 0;

  if (m_pCallback)
  {
    m_pCallback->OnBDEvent(ev);
  }
}

/// method which implements IAMStreamSelect.Count
/// returns the number of audio streams available
STDMETHODIMP CBDReaderFilter::Count(DWORD* streamCount)
{
  *streamCount = m_demultiplexer.GetAudioStreamCount();
  return S_OK;
}

/// method which implements IAMStreamSelect.Enable
/// Sets the current audio stream to use
STDMETHODIMP CBDReaderFilter::Enable(long index, DWORD flags)
{
  return m_demultiplexer.SetAudioStream((int)index) ? S_OK : S_FALSE;
}

/// method which implements IAMStreamSelect.Info
/// returns an array of all audio streams available
STDMETHODIMP CBDReaderFilter::Info(long lIndex, AM_MEDIA_TYPE**ppmt, DWORD* pdwFlags, LCID* plcid, DWORD* pdwGroup, WCHAR** ppszName, IUnknown** ppObject, IUnknown** ppUnk)
{
  if (pdwFlags)
  {
    int audioIndex = 0;
    m_demultiplexer.GetAudioStream(audioIndex);

    //if (m_demultiplexer.GetAudioStream()==(int)lIndex)
    if (audioIndex == (int)lIndex)
      *pdwFlags = AMSTREAMSELECTINFO_EXCLUSIVE;
    else
      *pdwFlags = 0;
  }
  if (plcid) *plcid = 0;
  if (pdwGroup) *pdwGroup = 1;
  if (ppObject) *ppObject = NULL;
  if (ppUnk) *ppUnk = NULL;
  if (ppszName)
  {
    char szName[40];
    m_demultiplexer.GetAudioStreamInfo((int)lIndex, szName);
    *ppszName = (WCHAR *)CoTaskMemAlloc(20);
    MultiByteToWideChar(CP_ACP, 0, szName, -1, *ppszName, 20);
  }
  if (ppmt)
  {
    CMediaType mediaType;
    m_demultiplexer.GetAudioStreamType((int)lIndex, mediaType);
    AM_MEDIA_TYPE* mType = (AM_MEDIA_TYPE*)(&mediaType);
    *ppmt = (AM_MEDIA_TYPE*)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
    if (*ppmt)
    {
      memcpy(*ppmt, mType, sizeof(AM_MEDIA_TYPE));
      (*ppmt)->pbFormat = (BYTE*)CoTaskMemAlloc(mediaType.FormatLength());
      memcpy((*ppmt)->pbFormat, mType->pbFormat, mediaType.FormatLength());
    }
    else
    {
      return S_FALSE;
    }
  }
  return S_OK;
}

// IAudioStream methods
STDMETHODIMP CBDReaderFilter::GetAudioStream(__int32 &stream)
{
  return m_demultiplexer.GetAudioStream(stream) ? S_OK : S_FALSE;
}

// ISubtitleStream methods
STDMETHODIMP CBDReaderFilter::SetSubtitleStream(__int32 stream)
{
  return m_demultiplexer.SetSubtitleStream(stream) ? S_OK : S_FALSE;
}

STDMETHODIMP CBDReaderFilter::GetSubtitleStreamLanguage(__int32 stream, char* szLanguage)
{
  return m_demultiplexer.GetSubtitleStreamLanguage(stream, szLanguage) ? S_OK : S_FALSE;
}

STDMETHODIMP CBDReaderFilter::GetSubtitleStreamType(__int32 stream, int &type)
{
  return m_demultiplexer.GetSubtitleStreamType(stream, type) ? S_OK : S_FALSE;
}

STDMETHODIMP CBDReaderFilter::GetSubtitleStreamCount(__int32 &count)
{
  return m_demultiplexer.GetSubtitleStreamCount(count) ? S_OK : S_FALSE;
}

STDMETHODIMP CBDReaderFilter::GetCurrentSubtitleStream(__int32 &stream)
{
  return m_demultiplexer.GetCurrentSubtitleStream(stream) ? S_OK : S_FALSE;
}

STDMETHODIMP CBDReaderFilter::SetSubtitleResetCallback( int (CALLBACK *pSubUpdateCallback)(int c, void* opts, int* select))
{
  return m_demultiplexer.SetSubtitleResetCallback(pSubUpdateCallback) ? S_OK : S_FALSE;
}

HRESULT CBDReaderFilter::FindSubtitleFilter()
{
  if (m_pDVBSubtitle)
  {
    return S_OK;
  }

  HRESULT hr = S_FALSE;
  ULONG fetched = 0;

  IEnumFilters * piEnumFilters = NULL;
  if (GetFilterGraph() && SUCCEEDED(GetFilterGraph()->EnumFilters(&piEnumFilters)))
  {
    IBaseFilter * pFilter;
    while (piEnumFilters->Next(1, &pFilter, &fetched) == NOERROR)
    {
      FILTER_INFO filterInfo;
      if (pFilter->QueryFilterInfo(&filterInfo) == S_OK)
      {
        if (!wcsicmp(L"MediaPortal DVBSub3", filterInfo.achName))
        {
          hr = pFilter->QueryInterface(IID_IDVBSubtitle3, (void**)&m_pDVBSubtitle);
        }
        filterInfo.pGraph->Release();
      }
      pFilter->Release();
      pFilter = NULL;
    }
    piEnumFilters->Release();
  }

  return hr;
}

bool CBDReaderFilter::IsSeeking()
{
  return (m_WaitForSeekToEof > 0);
}

bool CBDReaderFilter::IsStopping()
{
  return m_bStopping;
}

void CBDReaderFilter::ForceTitleBasedPlayback(bool force, UINT32 pTitle)
{
  lib.ForceTitleBasedPlayback(force);
  lib.SetTitle(pTitle);
}

void CBDReaderFilter::SetD3DDevice(IDirect3DDevice9* device)
{
  lib.SetD3DDevice(device);
}

void CBDReaderFilter::SetBDPlayerSettings(bd_player_settings settings)
{	
  lib.SetBDPlayerSettings(settings);
}

STDAPI DllRegisterServer()
{
  return AMovieDllRegisterServer2(TRUE);
} 

STDAPI DllUnregisterServer()
{
  return AMovieDllRegisterServer2(FALSE);
} 

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL APIENTRY DllMain(HANDLE hModule, DWORD  dwReason, LPVOID lpReserved)
{
  return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}


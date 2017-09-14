/*
 *      Copyright (C) 2005-2011 Team XBMC
 *      http://xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "client.h"
#include "xbmc_pvr_dll.h"
#include "libKODI_guilib.h"
#include "p8-platform/util/util.h"
#include "Tvheadend.h"
#include "tvheadend/Settings.h"
#include "tvheadend/utilities/Logger.h"

using namespace std;
using namespace ADDON;
using namespace P8PLATFORM;
using namespace tvheadend;
using namespace tvheadend::utilities;

/* **************************************************************************
 * Global variables
 * *************************************************************************/

/*
 * Client state
 */
ADDON_STATUS m_CurStatus = ADDON_STATUS_UNKNOWN;

/*
 * Globals
 */
CMutex g_mutex;
CHelper_libXBMC_addon *XBMC      = NULL;
CHelper_libXBMC_pvr   *PVR       = NULL;
PVR_MENUHOOK          *menuHook  = NULL;
CTvheadend            *tvh2       = NULL;

/* **************************************************************************
 * ADDON setup
 * *************************************************************************/

extern "C" {

void ADDON_ReadSettings(void)
{
  Settings::GetInstance2().ReadSettings();
}

ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!hdl || !props)
    return m_CurStatus;

  /* Instantiate helpers */
  XBMC  = new CHelper_libXBMC_addon;
  PVR   = new CHelper_libXBMC_pvr;

  if (!XBMC->RegisterMe(hdl) ||
      !PVR->RegisterMe(hdl))
  {
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
    m_CurStatus = ADDON_STATUS_PERMANENT_FAILURE;
    return m_CurStatus;
  }

  /* Configure the logger */
  Logger::GetInstance().SetImplementation([](LogLevel level, const char *message)
  {
    /* Convert the log level */
    addon_log_t addonLevel;

    switch (level)
    {
      case LogLevel::LEVEL_ERROR:
        addonLevel = addon_log_t::LOG_ERROR;
        break;
      case LogLevel::LEVEL_INFO:
        addonLevel = addon_log_t::LOG_INFO;
        break;
      default:
        addonLevel = addon_log_t::LOG_DEBUG;
    }

    /* Don't log trace messages unless told so */
    if (level == LogLevel::LEVEL_TRACE && !Settings::GetInstance2().GetTraceDebug())
      return;

    XBMC->Log(addonLevel, "%s", message);
  });

  Logger::GetInstance().SetPrefix("pvr.hts");

  Logger::Log(LogLevel::LEVEL_INFO, "starting PVR client");

  ADDON_ReadSettings();

  tvh2 = new CTvheadend(reinterpret_cast<PVR_PROPERTIES *>(props));
  tvh2->Start();

  m_CurStatus = ADDON_STATUS_OK;
  return m_CurStatus;
}

ADDON_STATUS ADDON_GetStatus()
{
  CLockObject lock(g_mutex);
  return m_CurStatus;
}

void ADDON_Destroy()
{
  CLockObject lock(g_mutex);
  SAFE_DELETE(tvh2);
  SAFE_DELETE(PVR);
  SAFE_DELETE(XBMC);
  SAFE_DELETE(menuHook);
  m_CurStatus = ADDON_STATUS_UNKNOWN;
}

ADDON_STATUS ADDON_SetSetting
  (const char *settingName, const void *settingValue)
{
  CLockObject lock(g_mutex);
  m_CurStatus = Settings::GetInstance2().SetSetting(settingName, settingValue);
  return m_CurStatus;
}

void OnSystemSleep()
{
  if (tvh2)
    tvh2->OnSleep();
}

void OnSystemWake()
{
  if (tvh2)
    tvh2->OnWake();
}

void OnPowerSavingActivated()
{
}

void OnPowerSavingDeactivated()
{
}

/* **************************************************************************
 * Capabilities / Info
 * *************************************************************************/

PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  pCapabilities->bSupportsEPG                = true;
  pCapabilities->bSupportsTV                 = true;
  pCapabilities->bSupportsRadio              = true;
  pCapabilities->bSupportsRecordings         = true;
  pCapabilities->bSupportsRecordingsUndelete = false;
  pCapabilities->bSupportsTimers             = true;
  pCapabilities->bSupportsChannelGroups      = true;
  pCapabilities->bHandlesInputStream         = true;
  pCapabilities->bHandlesDemuxing            = true;
  pCapabilities->bSupportsRecordingEdl       = true;
  pCapabilities->bSupportsRecordingPlayCount = (tvh2->GetProtocol() >= 27 && Settings::GetInstance2().GetDvrPlayStatus());
  pCapabilities->bSupportsLastPlayedPosition = (tvh2->GetProtocol() >= 27 && Settings::GetInstance2().GetDvrPlayStatus());
  pCapabilities->bSupportsDescrambleInfo     = true;

  if (tvh2->GetProtocol() >= 28)
  {
    pCapabilities->bSupportsRecordingsRename = true;

    pCapabilities->bSupportsRecordingsLifetimeChange = true;

    /* PVR_RECORDING.iLifetime values and presentation.*/
    std::vector<std::pair<int, std::string>> lifetimeValues;
    tvh2->GetLivetimeValues(lifetimeValues);

    pCapabilities->iRecordingsLifetimesSize = lifetimeValues.size();

    int i = 0;
    for (auto it = lifetimeValues.cbegin(); it != lifetimeValues.cend(); ++it, ++i)
    {
      pCapabilities->recordingsLifetimeValues[i].iValue = it->first;
      strncpy(pCapabilities->recordingsLifetimeValues[i].strDescription,
              it->second.c_str(),
              sizeof(pCapabilities->recordingsLifetimeValues[i].strDescription) - 1);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

const char *GetBackendName(void)
{
  static std::string serverName;

  serverName = tvh2->GetServerName();
  return serverName.c_str();
}

const char *GetBackendVersion(void)
{
  static std::string serverVersion;

  serverVersion = tvh2->GetServerVersion();
  return serverVersion.c_str();
}

const char *GetConnectionString(void)
{
  static std::string serverString;

  serverString = tvh2->GetServerString();
  return serverString.c_str();
}

const char *GetBackendHostname(void)
{
  return Settings::GetInstance2().GetConstCharHostname();
}

PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
  return tvh2->GetDriveSpace(iTotal, iUsed);
}

/* **************************************************************************
 * GUI hooks
 * *************************************************************************/

PVR_ERROR CallMenuHook
  (const PVR_MENUHOOK &_unused(menuhook),
   const PVR_MENUHOOK_DATA &_unused(data))
{
  return PVR_ERROR_NO_ERROR;
}

/* **************************************************************************
 * Demuxer
 * *************************************************************************/

bool CanPauseStream(void)
{
  return tvh2->HasCapability("timeshift");
}

bool CanSeekStream(void)
{
  return tvh2->HasCapability("timeshift");
}

bool IsTimeshifting(void)
{
  return tvh2->DemuxIsTimeShifting();
}

static time_t ConvertMusecsToTime(int64_t musecs)
{
  // tvheadend reports microseconds for timeshifting values,
  // Kodi expects it to be an absolute UNIX timestamp
  return time(NULL) - static_cast<time_t>(static_cast<double>(musecs / 1000000));
}

time_t GetPlayingTime()
{
  return ConvertMusecsToTime(tvh2->DemuxGetTimeshiftTime());
}

time_t GetBufferTimeStart()
{
  return ConvertMusecsToTime(tvh2->DemuxGetTimeshiftBufferStart());
}

time_t GetBufferTimeEnd()
{
  return ConvertMusecsToTime(tvh2->DemuxGetTimeshiftBufferEnd());
}

bool IsRealTimeStream()
{
  return tvh2->DemuxIsRealTimeStream();
}

bool OpenLiveStream(const PVR_CHANNEL &channel)
{
  return tvh2->DemuxOpen(channel);
}

void CloseLiveStream(void)
{
  tvh2->DemuxClose();
}

bool SeekTime(double time,bool backward,double *startpts)
{
  return tvh2->DemuxSeek(time, backward, startpts);
}

void SetSpeed(int speed)
{
  tvh2->DemuxSpeed(speed);
}

PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES* pProperties)
{
  return tvh2->DemuxCurrentStreams(pProperties);
}

PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  return tvh2->DemuxCurrentSignal(signalStatus);
}

PVR_ERROR GetDescrambleInfo(PVR_DESCRAMBLE_INFO* descrambleInfo)
{
  if (!descrambleInfo)
    return PVR_ERROR_INVALID_PARAMETERS;

  return tvh2->DemuxCurrentDescramble(descrambleInfo);
}

DemuxPacket* DemuxRead(void)
{
  return tvh2->DemuxRead();
}

void DemuxAbort(void)
{
  tvh2->DemuxAbort();
}

void DemuxReset(void)
{
}

void DemuxFlush(void)
{
  tvh2->DemuxFlush();
}

/* **************************************************************************
 * Channel Management
 * *************************************************************************/

int GetChannelGroupsAmount(void)
{
  return tvh2->GetTagCount();
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  return tvh2->GetTags(handle, bRadio);
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  return tvh2->GetTagMembers(handle, group);
}

int GetChannelsAmount(void)
{
  return tvh2->GetChannelCount();
}

PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  return tvh2->GetChannels(handle, bRadio);
}

/* **************************************************************************
 * EPG
 * *************************************************************************/

PVR_ERROR GetEPGForChannel
  (ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  return tvh2->GetEPGForChannel(handle, channel, iStart, iEnd);
}

PVR_ERROR SetEPGTimeFrame(int iDays)
{
  return tvh2->SetEPGTimeFrame(iDays);
}

/* **************************************************************************
 * Recording Management
 * *************************************************************************/

int GetRecordingsAmount(bool deleted)
{
  return tvh2->GetRecordingCount();
}

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted)
{
  return tvh2->GetRecordings(handle);
}

PVR_ERROR GetRecordingEdl
  (const PVR_RECORDING &rec, PVR_EDL_ENTRY edl[], int *num)
{
  return tvh2->GetRecordingEdl(rec, edl, num);
}

PVR_ERROR DeleteRecording(const PVR_RECORDING &rec)
{
  return tvh2->DeleteRecording(rec);
}

PVR_ERROR RenameRecording(const PVR_RECORDING &rec)
{
  return tvh2->RenameRecording(rec);
}

PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR DeleteAllRecordingsFromTrash()
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR SetRecordingLifetime(const PVR_RECORDING *recording)
{
  if (!recording)
  {
    Logger::Log(LogLevel::LEVEL_ERROR, "recording must not be nullptr");
    return PVR_ERROR_INVALID_PARAMETERS;
  }
  return tvh2->SetLifetime(*recording);
}

PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count)
{
  return tvh2->SetPlayCount(recording, count);
}

PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition)
{
  return tvh2->SetPlayPosition(recording, lastplayedposition);
}

int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording)
{
  return tvh2->GetPlayPosition(recording);
}

PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  return tvh2->GetTimerTypes(types, size);
}

int GetTimersAmount(void)
{
  return tvh2->GetTimerCount();
}

PVR_ERROR GetTimers(ADDON_HANDLE handle)
{
  return tvh2->GetTimers(handle);
}

PVR_ERROR AddTimer(const PVR_TIMER &timer)
{
  return tvh2->AddTimer(timer);
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete)
{
  return tvh2->DeleteTimer(timer, bForceDelete);
}

PVR_ERROR UpdateTimer(const PVR_TIMER &timer)
{
  return tvh2->UpdateTimer(timer);
}

/* **************************************************************************
 * Recording VFS
 * *************************************************************************/

bool OpenRecordedStream(const PVR_RECORDING &recording)
{
  return tvh2->VfsOpen(recording);
}

void CloseRecordedStream(void)
{
  tvh2->VfsClose();
}

int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  return static_cast<int>(tvh2->VfsRead(pBuffer, iBufferSize));
}

long long SeekRecordedStream(long long iPosition, int iWhence /* = SEEK_SET */)
{
  return tvh2->VfsSeek(iPosition, iWhence);
}

long long PositionRecordedStream(void)
{
  return tvh2->VfsTell();
}

long long LengthRecordedStream(void)
{
  return tvh2->VfsSize();
}

/* **************************************************************************
 * Unused Functions
 * *************************************************************************/

/* Channel Management */
PVR_ERROR OpenDialogChannelScan(void)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR DeleteChannel(const PVR_CHANNEL &_unused(channel))
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR RenameChannel(const PVR_CHANNEL &_unused(channel))
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR MoveChannel(const PVR_CHANNEL &_unused(channel))
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &_unused(channel))
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &_unused(channel))
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

void PauseStream(bool _unused(bPaused))
{
}

/* Live stream (VFS interface - not relevant) */
int ReadLiveStream
  (unsigned char *_unused(pBuffer), unsigned int _unused(iBufferSize))
{
  return 0;
}

long long SeekLiveStream
  (long long _unused(iPosition), int _unused(iWhence))
{
  return -1;
}

long long PositionLiveStream(void)
{
  return -1;
}

long long LengthLiveStream(void)
{
  return -1;
}

PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES*)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL*, PVR_NAMED_VALUE*, unsigned int*)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR IsEPGTagRecordable(const EPG_TAG*, bool*)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR IsEPGTagPlayable(const EPG_TAG*, bool*)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR GetEPGTagStreamProperties(const EPG_TAG*, PVR_NAMED_VALUE*, unsigned int*)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

} /* extern "C" */

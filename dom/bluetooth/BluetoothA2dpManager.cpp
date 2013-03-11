/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include <utils/String8.h>
#include "BluetoothA2dpManager.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothUtils.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "nsContentUtils.h"
#include "nsIAudioManager.h"
#include "nsIObserverService.h"
#include "gonk/AudioSystem.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "A2DP", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

#define BLUETOOTH_A2DP_STATUS_CHANGED "bluetooth-a2dp-status-changed"

using namespace mozilla;
using namespace mozilla::ipc;
USING_BLUETOOTH_NAMESPACE

static const int STATUS_STOPPED = 0x00;
static const int STATUS_PLAYING = 0x01;
static const int STATUS_PAUSED = 0x02;
static const int STATUS_FWD_SEEK = 0x03;
static const int STATUS_REV_SEEK = 0x04;
static const int STATUS_ERROR = 0xFF;
static const int EVENT_PLAYSTATUS_CHANGED = 0x1;
static const int EVENT_TRACK_CHANGED = 0x2;
namespace {
StaticRefPtr<BluetoothA2dpManager> gBluetoothA2dpManager;
} // anonymous namespace

BluetoothA2dpManager::BluetoothA2dpManager()
{
}

bool
BluetoothA2dpManager::Init()
{
  mSocketStatus = GetConnectionStatus();

  return true;
}

BluetoothA2dpManager::~BluetoothA2dpManager()
{
}

//static
BluetoothA2dpManager*
BluetoothA2dpManager::Get()
{
  MOZ_ASSERT(NS_IsMainThread());

  // If we already exist, exit early
  if (gBluetoothA2dpManager) {
    return gBluetoothA2dpManager;
  }

  // Create new instance, register, return
  nsRefPtr<BluetoothA2dpManager> manager = new BluetoothA2dpManager();
  NS_ENSURE_TRUE(manager, nullptr);

  if (!manager->Init()) {
    return nullptr;
  }

  gBluetoothA2dpManager = manager;
  return gBluetoothA2dpManager;
}

static void
SetParameter(const nsAString& aParameter)
{
  android::String8 cmd;
  cmd.appendFormat(NS_ConvertUTF16toUTF8(aParameter).get());
  android::AudioSystem::setParameters(0, cmd);
}

static void
MakeA2dpDeviceAvailableNow(const nsAString& aBdAddress)
{
  android::AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP,
                        AUDIO_POLICY_DEVICE_STATE_AVAILABLE, NS_ConvertUTF16toUTF8(aBdAddress).get());
}

static void
MakeA2dpDeviceUnavailableNow(const nsAString& aBdAddress)
{
  android::AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP,
                        AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE, NS_ConvertUTF16toUTF8(aBdAddress).get());
}

static BluetoothA2dpState
ConvertSinkStringToState(const nsAString& aNewState)
{
  if (aNewState.EqualsLiteral("disonnected"))
    return BluetoothA2dpState::SINK_DISCONNECTED;
  if (aNewState.EqualsLiteral("connecting"))
    return BluetoothA2dpState::SINK_CONNECTING;
  if (aNewState.EqualsLiteral("connected"))
    return BluetoothA2dpState::SINK_CONNECTED;
  if (aNewState.EqualsLiteral("playing"))
    return BluetoothA2dpState::SINK_PLAYING;
    return BluetoothA2dpState::SINK_DISCONNECTED;
}

// Virtual function of class SocketConsumer
void
BluetoothA2dpManager::ReceiveSocketData(mozilla::ipc::UnixSocketRawData* aMessage)
{
  // A2dp socket do nothing here
  MOZ_NOT_REACHED("This should never be called!");
}

static void
RouteA2dpAudioPath()
{
  SetParameter(NS_LITERAL_STRING("bluetooth_enabled=true"));
  SetParameter(NS_LITERAL_STRING("A2dpSuspended=false"));
  android::AudioSystem::setForceUse((audio_policy_force_use_t)1, (audio_policy_forced_cfg_t)0);
}

void
BluetoothA2dpManager::HandleSinkPropertyChange(const nsAString& aDeviceObjectPath,
                         const nsAString& aNewState)
{
  //Possible values: "disconnected", "connecting",
  //"connected", "playing"
  // 1. "disconnected" -> "connecting"
  //  Either an incoming or outgoing connection
  //  attempt ongoing.
  // 2. "connecting" -> "disconnected"
  // Connection attempt failed
  // 3. "connecting" -> "connected"
  //     Successfully connected
  // 4. "connected" -> "playing"
  //     SCO audio connection successfully opened
  // 5. "playing" -> "connected"
  //     SCO audio connection closed
  // 6. "connected" -> "disconnected"
  // 7. "playing" -> "disconnected"
  //     Disconnected from the remote device

  if (aNewState.EqualsLiteral("connected")) {
    LOG("A2DP connected!! Route path to a2dp");
    LOG("Currnet device: %s",NS_ConvertUTF16toUTF8(mCurrentAddress).get());
    RouteA2dpAudioPath();
    //MakeA2dpDeviceAvailableNow(GetAddressFromObjectPath(mCurrentAddress));
  }
  mCurrentSinkState = ConvertSinkStringToState(aNewState);
  //TODO: Need to check Sink state and do more stuffs
}

bool
BluetoothA2dpManager::Connect(const nsAString& aDeviceAddress,
                              BluetoothReplyRunnable* aRunnable)
{
  //TODO: We shall decide BluetoothReplyRunnable is necessary, cleanup!
  LOG("a2dp manager connect");
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothService* bs = BluetoothService::Get();
  if(!bs) {
    LOG("Couldn't get BluetoothService");
    return false;
  }
  mCurrentAddress = aDeviceAddress;
  // TODO(Eric)
  // Please decide what should be passed into function ConnectSink()
  bs->ConnectSink(aDeviceAddress, aRunnable);
  LOG("Address: %s", NS_ConvertUTF16toUTF8(GetAddressFromObjectPath(mCurrentAddress)).get());
  LOG("Address: %s", NS_ConvertUTF16toUTF8(mCurrentAddress).get());
  mCurrentAddress = NS_LITERAL_STRING("/org/bluez/362/hci0/dev_00_80_98_09_0C_9F");
  MakeA2dpDeviceAvailableNow(GetAddressFromObjectPath(mCurrentAddress));
  return true;
}

bool
BluetoothA2dpManager::Listen()
{
  MOZ_ASSERT(NS_IsMainThread());
  // TODO
  // Remove it is necessary

  return true;
}


void
BluetoothA2dpManager::ResetAudio()
{
  //TODO:
  //It is necessary we need to handle it in Audio module!
  SetParameter(NS_LITERAL_STRING("bluetooth_enabled=false"));
  SetParameter(NS_LITERAL_STRING("A2dpSuspended=true"));
  MakeA2dpDeviceUnavailableNow(GetAddressFromObjectPath(mCurrentAddress));
  android::AudioSystem::setForceUse((audio_policy_force_use_t)1, (audio_policy_forced_cfg_t)10);
}

void
BluetoothA2dpManager::UpdatePlayStatus()
{
  LOG("UpdatePlayStatus!!!!!!");
  BluetoothService* bs = BluetoothService::Get();
  if (mPlayStatus == STATUS_PLAYING) {
    //TODO: we need to handle position
    LOG("Update position: %d", mPosition);
  }
  //TODO: Needs to check Duration/Position correctness
  mDuration = 1;
  mPosition = 1;
  mPlayStatus = 1;
  bs->UpdatePlayStatus(mCurrentAddress, mDuration, mPosition, mPlayStatus);
}

void
BluetoothA2dpManager::UpdateMetaData(const nsAString& aTitle, const nsAString& aArtist,
                                     const nsAString& aAlbum, const nsAString& aMediaNumber,
                                     const nsAString& aTotalMediaCount, const nsAString& aPlaytime,
                                     BluetoothReplyRunnable* aRunnable)
{
  BluetoothService* bs = BluetoothService::Get();
  if (mPlayStatus == STATUS_PLAYING) {
    //TODO: we need to handle position
    //this currently just skelton
    LOG("Update position: %d", mPosition);
  }
#ifdef AVRCP_TESTDATA
  mTrackName = NS_LITERAL_STRING("S_Tack1");
  mArtist = NS_LITERAL_STRING("S_Artist1");
  mAlbum = NS_LITERAL_STRING("S_Album1");
  mTrackName = NS_LITERAL_STRING("S_TrackName1");
  mTrackNumber = NS_LITERAL_STRING("1");
  mTotalMediaCount = NS_LITERAL_STRING("10");
  mPlaytime = NS_LITERAL_STRING("150000"); //2min30sec
#endif
  mTrackName = aTitle;
  mArtist = aArtist;
  mAlbum = aAlbum;
  mTrackNumber = aMediaNumber;
  mTotalMediaCount = aTotalMediaCount;
  mPlaytime = aPlaytime;

#ifdef BTDEBUG
  LOG("BluetoothA2dpManager::UpdateMetaData");
  LOG("aTitle: %s",NS_ConvertUTF16toUTF8(mTrackName).get());
  LOG("aArtist: %s",NS_ConvertUTF16toUTF8(mArtist).get());
  LOG("aAlbum: %s",NS_ConvertUTF16toUTF8(mAlbum).get());
  LOG("aMediaNumber: %s",NS_ConvertUTF16toUTF8(mTrackNumber).get());
  LOG("aTotalMediaCount: %s",NS_ConvertUTF16toUTF8(aTotalMediaCount).get());
  //hard code to test
  mCurrentAddress = NS_LITERAL_STRING("/org/bluez/362/hci0/dev_00_80_98_09_0C_9F");
  LOG("CurrentAddress: %s",NS_ConvertUTF16toUTF8(mCurrentAddress).get());
#endif
  if (!mCurrentAddress.IsEmpty()) {
    bs->UpdateMetaData(mCurrentAddress, mTrackName, mArtist, mAlbum, mTrackNumber, mTotalMediaCount, mPlaytime, aRunnable);
  }
}

void
BluetoothA2dpManager::Disconnect(const nsAString& aDeviceAddress,
                                BluetoothReplyRunnable* aRunnable)
{
  BluetoothService* bs = BluetoothService::Get();
  if(!bs) {
    LOG("Couldn't get BluetoothService");
    return;
  }
  // DisconnectSink actually send Close stream first
  bs->DisconnectSink(aDeviceAddress, aRunnable);
  LOG("Address: %s", NS_ConvertUTF16toUTF8(GetAddressFromObjectPath(mCurrentAddress)).get());
  SetParameter(NS_LITERAL_STRING("bluetooth_enabled=true"));
  SetParameter(NS_LITERAL_STRING("A2dpSuspended=true"));
  MakeA2dpDeviceUnavailableNow(GetAddressFromObjectPath(mCurrentAddress));
  android::AudioSystem::setForceUse((audio_policy_force_use_t)1, (audio_policy_forced_cfg_t)10);
}

void
BluetoothA2dpManager::GetConnectedSinkAddress(nsAString& aDeviceAddress)
{
  //TODO: need to check more condition
  aDeviceAddress = mCurrentAddress;
}

void
BluetoothA2dpManager::UpdateNotification(const nsAString& aDeviceObjectPath,
                                const uint16_t aEventid, const uint32_t aData)
{
  //send notification if:
  //1.Meta data change (Event Track change)
  //2.Play status changed
  BluetoothService* bs = BluetoothService::Get();
  bs->UpdateNotification(aDeviceObjectPath, aEventid, aData);
}

void
BluetoothA2dpManager::HandleCallStateChanged(uint16_t aCallState)
{
  switch (aCallState) {
    //PHONE_STATE RINGING or OFFHOOK
    case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
    case nsIRadioInterfaceLayer::CALL_STATE_CONNECTED:
      //TODO: We shall disable AVRCP, and set SetParameters suspend
      LOG("BluetoothA2dpManager: CALL_STATE_INCOMING/CALL_STATE_CONNECTED, disable AVRCP");
      SetParameter(NS_LITERAL_STRING("A2dpSuspended=true"));
      break;
    //PHONE_STATE IDLE
    case nsIRadioInterfaceLayer::CALL_STATE_DISCONNECTED:
      //IDLE state, we shall do delay resuming sink, there are many chipsets on
      //Bluetooth headsets cannot handle A2DP sink resuming stream too fast,
      //while SCO state is in disconnecting
      LOG("BluetoothA2dpManager: CALL_STATE_DISCONNECTED, delay resume sink");
      SetParameter(NS_LITERAL_STRING("A2dpSuspended=false"));
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_DIALING:
    case nsIRadioInterfaceLayer::CALL_STATE_ALERTING:
      break;
    default:
      break;
  }
}

void
BluetoothA2dpManager::OnConnectSuccess()
{
  //TODO: dummy function. Cleanup in the future
}

void
BluetoothA2dpManager::OnConnectError()
{
  //TODO: this is dummy, shall be removed
}

void
BluetoothA2dpManager::OnDisconnect()
{
  //TODO: this is dummy, shall be removed
}

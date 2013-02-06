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

// Virtual function of class SocketConsumer
void
BluetoothA2dpManager::ReceiveSocketData(mozilla::ipc::UnixSocketRawData* aMessage)
{
  // A2dp socket do nothing here
  MOZ_NOT_REACHED("This should never be called!");
}

bool
BluetoothA2dpManager::Connect(const nsAString& aDeviceAddress,
                              BluetoothReplyRunnable* aRunnable)
{
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
  SetParameter(NS_LITERAL_STRING("bluetooth_enabled=true"));
  SetParameter(NS_LITERAL_STRING("A2dpSuspended=false"));
  android::AudioSystem::setForceUse((audio_policy_force_use_t)1, (audio_policy_forced_cfg_t)0);
  MakeA2dpDeviceAvailableNow(GetAddressFromObjectPath(mCurrentAddress));
  return true;
}

bool
BluetoothA2dpManager::Listen()
{
  MOZ_ASSERT(NS_IsMainThread());
  // TODO(Eric)
  // Needs implementation to really "Listen to remote A2DP connection request"

  return true;
}

void
BluetoothA2dpManager::ResetAudio()
{
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
BluetoothA2dpManager::UpdateMetaData()
{
  LOG("UpdateMetaData!!!!!!");
  BluetoothService* bs = BluetoothService::Get();
  if (mPlayStatus == STATUS_PLAYING) {
    //TODO: we need to handle position
    LOG("Update position: %d", mPosition);
  }
  //TODO: we shall use the real data from gaia app
  mTrackName = NS_LITERAL_STRING("S_Tack1");
  mArtist = NS_LITERAL_STRING("S_Artist1");
  mAlbum = NS_LITERAL_STRING("S_Album1");
  mTrackName = NS_LITERAL_STRING("S_TrackName1");
  mTrackNumber = NS_LITERAL_STRING("1");
  mTotalMediaCount = NS_LITERAL_STRING("10");
  mPlaytime = NS_LITERAL_STRING("150000"); //2min30sec
  bs->UpdateMetaData(mCurrentAddress, mTrackName, mArtist, mAlbum, mTrackNumber, mTotalMediaCount, mPlaytime);
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

void BluetoothA2dpManager::GetConnectedSinkAddress(nsAString& aDeviceAddress)
{
  aDeviceAddress = mCurrentAddress;
}

void
BluetoothA2dpManager::OnConnectSuccess()
{
  nsString address;
  GetSocketAddr(address);

  mSocketStatus = GetConnectionStatus();
}

void
BluetoothA2dpManager::OnConnectError()
{
  //TODO: this is dummy, shall be removed
  CloseSocket();
  mSocketStatus = GetConnectionStatus();
  Listen();
}

void
BluetoothA2dpManager::OnDisconnect()
{
  //TODO: this is dummy, shall be removed
  if (mSocketStatus == SocketConnectionStatus::SOCKET_CONNECTED) {
    Listen();
  }
}

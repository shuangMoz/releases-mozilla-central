/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetootha2dpmanager_h__
#define mozilla_dom_bluetooth_bluetootha2dpmanager_h__

#include "BluetoothCommon.h"
#include "mozilla/ipc/UnixSocket.h"
#include "nsIObserver.h"
#include "BluetoothRilListener.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothReplyRunnable;

enum BluetoothA2dpState {
  SINK_DISCONNECTED = 0,
  SINK_CONNECTING = 1,
  SINK_CONNECTED = 2,
  SINK_PLAYING = 3
};

class BluetoothA2dpManager : public mozilla::ipc::UnixSocketConsumer
{
//TODO: BluetoothA2dpManager shall not inherits UnixSocketConsumer
public:
  ~BluetoothA2dpManager();

  static BluetoothA2dpManager* Get();
  void ReceiveSocketData(mozilla::ipc::UnixSocketRawData* aMessage)
    MOZ_OVERRIDE;

  bool Connect(const nsAString& aDeviceObjectPath,
               BluetoothReplyRunnable* aRunnable);
  void Disconnect(const nsAString& aDeviceObjectPath,
               BluetoothReplyRunnable* aRunnable);
  bool Listen();
  void GetConnectedSinkAddress(nsAString& aDeviceAddress);
  void ResetAudio();
  void UpdatePlayStatus();
  void UpdateMetaData();
  void HandleSinkPropertyChange(const nsAString& aDeviceObjectPath,
                                const nsAString& newState);
  void UpdateNotification(const nsAString& aDeviceObjectPath,
                          const uint16_t aEventid, const uint32_t aData);
private:
  BluetoothA2dpManager();
  bool Init();
  virtual void OnConnectSuccess() MOZ_OVERRIDE; //TODO: remove this field
  virtual void OnConnectError() MOZ_OVERRIDE; //TODO: remove this field
  virtual void OnDisconnect() MOZ_OVERRIDE; //TODO: remove this field
  int mSocketStatus; //TODO: remove this field
  BluetoothA2dpState mCurrentSinkState;
  nsString mCurrentAddress;
  //AVRCP 1.3 fields
  nsString mTrackName;
  nsString mTrackNumber;
  nsString mArtist;
  nsString mAlbum;
  nsString mTotalMediaCount;
  nsString mPlaytime;
  uint32_t mDuration;
  uint32_t mPosition;
  uint32_t mPlayStatus;
  long mReportTime;
  //TODO:Add RIL listener for suspend/resume A2DP
  //For the reason HFP/A2DP usage switch, we need to force suspend A2DP
  nsAutoPtr<BluetoothRilListener> mListener;
};
END_BLUETOOTH_NAMESPACE
#endif

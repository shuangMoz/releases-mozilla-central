/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "UnixSocket.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/socket.h>

#include "base/eintr_wrapper.h"
#include "base/message_loop.h"

#include "nsString.h"
#include "nsThreadUtils.h"
#include "nsTArray.h"
#include "mozilla/Monitor.h"
#include "mozilla/Util.h"
#include "mozilla/FileUtils.h"
#include "nsXULAppAPI.h"

namespace mozilla {
namespace ipc {

class UnixSocketImpl : public MessageLoopForIO::Watcher
{
public:
  UnixSocketImpl(UnixSocketConsumer* aConsumer,
                 int aFd) : mConsumer(aConsumer)
                          , mFd(aFd)
  {
  }

  ~UnixSocketImpl()
  {
    mReadWatcher.StopWatchingFileDescriptor();
    mWriteWatcher.StopWatchingFileDescriptor();
  }

  void QueueWriteData(UnixSocketRawData* aData)
  {
    mOutgoingQ.AppendElement(aData);
    OnFileCanWriteWithoutBlocking(mFd);
  }

  bool isFdValid()
  {
    return mFd > 0;
  }

  void SetUpIO()
  {
    mIOLoop = MessageLoopForIO::current();
    mIOLoop->WatchFileDescriptor(
      mFd,
      true,
      MessageLoopForIO::WATCH_READ,
      &mReadWatcher,
      this);
  }

  void PrepareRemoval()
  {
    mConsumer.forget();
  }
  
private:
  MessageLoopForIO* mIOLoop;
  typedef nsTArray<UnixSocketRawData* > UnixSocketRawDataQueue;
  UnixSocketRawDataQueue mOutgoingQ;
  nsRefPtr<UnixSocketConsumer> mConsumer;
  ScopedClose mFd;
  nsAutoPtr<UnixSocketRawData> mIncoming;
  MessageLoopForIO::FileDescriptorWatcher mReadWatcher;
  MessageLoopForIO::FileDescriptorWatcher mWriteWatcher;
  virtual void OnFileCanReadWithoutBlocking(int aFd);
  virtual void OnFileCanWriteWithoutBlocking(int aFd);
};

class KillImplTask : public Task
{
public:
  KillImplTask(UnixSocketImpl* aImpl) : mImpl(aImpl)
  {
  }

  void
  Run()
  {    
    delete mImpl;
  }

private:
  UnixSocketImpl* mImpl;
};

class SocketReceiveTask : public nsRunnable
{
public:
  SocketReceiveTask(UnixSocketConsumer* aConsumer, UnixSocketRawData* aData) :
    mConsumer(aConsumer),
    mRawData(aData)
  {
    MOZ_ASSERT(aConsumer);
    MOZ_ASSERT(aData);
  }

  NS_IMETHOD
  Run()
  {
    mConsumer->ReceiveSocketData(mRawData);
    return NS_OK;
  }
private:
  nsRefPtr<UnixSocketConsumer> mConsumer;
  nsAutoPtr<UnixSocketRawData> mRawData;
};

class SocketSendTask : public Task
{
public:
  SocketSendTask(UnixSocketConsumer* aConsumer, UnixSocketImpl* aImpl,
                 UnixSocketRawData* aData)
    : mConsumer(aConsumer),
      mImpl(aImpl),
      mData(aData)
  {
    MOZ_ASSERT(aConsumer);
    MOZ_ASSERT(aImpl);
    MOZ_ASSERT(aData);
  }

  void
  Run()
  {
    mImpl->QueueWriteData(mData);
  }

private:
  nsRefPtr<UnixSocketConsumer> mConsumer;
  UnixSocketImpl* mImpl;
  UnixSocketRawData* mData;
};

class StartImplReadingTask : public Task
{
public:
  StartImplReadingTask(UnixSocketImpl* aImpl) : mImpl(aImpl)
  {
  }

  void
  Run()
  {
    mImpl->SetUpIO();
  }
private:
  UnixSocketImpl* mImpl;
};

bool
UnixSocketConsumer::SendSocketData(UnixSocketRawData* aData)
{
  if (!mImpl) {
    return false;
  }
  XRE_GetIOMessageLoop()->PostTask(FROM_HERE,
                                   new SocketSendTask(this, mImpl, aData));
  return true;
}

bool
UnixSocketConsumer::SendSocketData(const nsAString& aStr)
{
  return SendSocketData(NS_ConvertUTF16toUTF8(aStr));
}

bool
UnixSocketConsumer::SendSocketData(const nsACString& aStr)
{
  if (!mImpl) {
    return false;
  }
  if (aStr.Length() > UnixSocketRawData::MAX_DATA_SIZE) {
    return false;
  }
  nsCString str(aStr);
  UnixSocketRawData* d = new UnixSocketRawData();
  memcpy(d->mData, str.get(), aStr.Length());
  d->mSize = aStr.Length();
  XRE_GetIOMessageLoop()->PostTask(FROM_HERE,
                                   new SocketSendTask(this, mImpl, d));
  return true;
}

void
UnixSocketConsumer::CloseSocket()
{
  if (!mImpl) {
    return;
  }
  // To make sure the owner doesn't die on the IOThread, remove pointer here
  mImpl->PrepareRemoval();
  // Line it up to be destructed on the IO Thread
  XRE_GetIOMessageLoop()->PostTask(FROM_HERE, new KillImplTask(mImpl));
  // Kill our pointer to it
  mImpl = nullptr;
}

bool
UnixSocketConnector::Prepare(int aFd)
{
  // Set close-on-exec bit.
  int flags = fcntl(aFd, F_GETFD);
  if (-1 == flags) {
    return false;
  }

  flags |= FD_CLOEXEC;
  if (-1 == fcntl(aFd, F_SETFD, flags)) {
    return false;
  }

  // Select non-blocking IO.
  if (-1 == fcntl(aFd, F_SETFL, O_NONBLOCK)) {
    return false;
  }

  return true;
}

void
UnixSocketImpl::OnFileCanReadWithoutBlocking(int aFd)
{
  // Keep reading data until either
  //
  //   - mIncoming is completely read
  //     If so, sConsumer->MessageReceived(mIncoming.forget())
  //
  //   - mIncoming isn't completely read, but there's no more
  //     data available on the socket
  //     If so, break;
  while (true) {
    if (!mIncoming) {
      mIncoming = new UnixSocketRawData();
      ssize_t ret = read(aFd, mIncoming->mData, UnixSocketRawData::MAX_DATA_SIZE);
      if (ret <= 0) {
        if (ret == -1) {
          if (errno == EINTR) {
            continue; // retry system call when interrupted
          }
          else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            mIncoming.forget();
            return; // no data available: return and re-poll
          }
          // else fall through to error handling on other errno's
        }
#ifdef DEBUG
        nsAutoString str;
        str.AssignLiteral("Cannot read from network, error ");
        str += (int)ret;
        NS_WARNING(NS_ConvertUTF16toUTF8(str).get());
#endif
        // At this point, assume that we can't actually access
        // the socket anymore, and start a reconnect loop.
        mIncoming.forget();
        mReadWatcher.StopWatchingFileDescriptor();
        mWriteWatcher.StopWatchingFileDescriptor();
        close(aFd);
        return;
      }
      mIncoming->mData[ret] = 0;
      mIncoming->mSize = ret;
      nsRefPtr<SocketReceiveTask> t =
        new SocketReceiveTask(mConsumer, mIncoming.forget());
      NS_DispatchToMainThread(t);
      if (ret < ssize_t(UnixSocketRawData::MAX_DATA_SIZE)) {
        return;
      }
    }
  }
}

void
UnixSocketImpl::OnFileCanWriteWithoutBlocking(int aFd)
{
  // Try to write the bytes of mCurrentRilRawData.  If all were written, continue.
  //
  // Otherwise, save the byte position of the next byte to write
  // within mCurrentRilRawData, and request another write when the
  // system won't block.
  //
  while (true) {
    UnixSocketRawData* data;
    if (mOutgoingQ.IsEmpty()) {
      return;
    }
    data = mOutgoingQ.ElementAt(0);
    const uint8_t *toWrite;
    toWrite = data->mData;

    while (data->mCurrentWriteOffset < data->mSize) {
      ssize_t write_amount = data->mSize - data->mCurrentWriteOffset;
      ssize_t written;
      written = write (aFd, toWrite + data->mCurrentWriteOffset,
                       write_amount);
      if (written > 0) {
        data->mCurrentWriteOffset += written;
      }
      if (written != write_amount) {
        break;
      }
    }

    if (data->mCurrentWriteOffset != data->mSize) {
      MessageLoopForIO::current()->WatchFileDescriptor(
        aFd,
        false,
        MessageLoopForIO::WATCH_WRITE,
        &mWriteWatcher,
        this);
      return;
    }
    mOutgoingQ.RemoveElementAt(0);
    delete data;
  }
}

bool
UnixSocketConsumer::ConnectSocket(UnixSocketConnector& aConnector,
                                  const char* aAddress)
{
  MOZ_ASSERT(!NS_IsMainThread());
  if (!mImpl) {
    NS_WARNING("Socket already connected!");
    return false;
  }
  int fd = aConnector.Create();
  if (fd < 0) {
    return false;
  }
  mImpl = new UnixSocketImpl(this, fd);
  if (!aConnector.Connect(fd, aAddress))
  {
    delete mImpl;
    return false;
  }

  XRE_GetIOMessageLoop()->PostTask(FROM_HERE,
                                   new StartImplReadingTask(mImpl));
  return true;
}

} // namespace ipc
} // namespace mozilla

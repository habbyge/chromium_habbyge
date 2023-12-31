// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_SEARCH_RANKING_PERSISTENT_PROTO_H_
#define CHROME_BROWSER_UI_APP_LIST_SEARCH_RANKING_PERSISTENT_PROTO_H_

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/sequence_checker.h"
#include "base/task/post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_runner_util.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/time/time.h"

namespace app_list {

// The result of reading a backing file from disk. These values persist to logs.
// Entries should not be renumbered and numeric values should never be reused.
enum class ReadStatus {
  kOk = 0,
  kMissing = 1,
  kReadError = 2,
  kParseError = 3,
  kMaxValue = kParseError,
};

// The result of writing a backing file to disk. These values persist to logs.
// Entries should not be renumbered and numeric values should never be reused.
enum class WriteStatus {
  kOk = 0,
  kWriteError = 1,
  kSerializationError = 2,
  kMaxValue = kSerializationError,
};

namespace {

template <class T>
std::pair<ReadStatus, std::unique_ptr<T>> Read(const base::FilePath& filepath) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  if (!base::PathExists(filepath))
    return {ReadStatus::kMissing, nullptr};

  std::string proto_str;
  if (!base::ReadFileToString(filepath, &proto_str))
    return {ReadStatus::kReadError, nullptr};

  auto proto = std::make_unique<T>();
  if (!proto->ParseFromString(proto_str))
    return {ReadStatus::kParseError, nullptr};

  return {ReadStatus::kOk, std::move(proto)};
}

WriteStatus Write(const base::FilePath& filepath,
                  const std::string& proto_str) {
  const auto directory = filepath.DirName();
  if (!base::DirectoryExists(directory))
    base::CreateDirectory(directory);

  bool write_result;
  {
    base::ScopedBlockingCall scoped_blocking_call(
        FROM_HERE, base::BlockingType::MAY_BLOCK);
    write_result = base::ImportantFileWriter::WriteFileAtomically(
        filepath, proto_str, "AppListPersistentProto");
  }

  if (!write_result)
    return WriteStatus::kWriteError;
  return WriteStatus::kOk;
}

}  // namespace

// PersistentProto wraps a proto class and persists it to disk. Usage summary:
//  - Init is asynchronous, usage before |on_read| is called will crash.
//  - pproto->Method() will call Method on the underlying proto.
//  - Call QueueWrite() to write to disk.
//
// Reading. The backing file is read asynchronously from disk once at
// initialization, and the |on_read| callback is run once this is done. Until
// |on_read| is called, has_value is false and get() will always return nullptr.
// If no proto file exists on disk, or it is invalid, a blank proto is
// constructed and immediately written to disk.
//
// Writing. Writes must be triggered manually. Two methods are available:
//  - QueueWrite() delays writing to disk for |write_delay| time, in order to
//    batch successive writes.
//  - StartWrite() writes to disk as soon as the task scheduler allows.
// The |on_write| callback is run each time a write has completed.
template <class T>
class PersistentProto {
 public:
  using ReadCallback = base::OnceCallback<void(ReadStatus)>;
  using WriteCallback = base::RepeatingCallback<void(WriteStatus)>;

  PersistentProto()
      : task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
            {base::TaskPriority::BEST_EFFORT, base::MayBlock(),
             base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})) {}

  void Init(const base::FilePath& path,
            const base::TimeDelta write_delay,
            PersistentProto<T>::ReadCallback on_read,
            PersistentProto<T>::WriteCallback on_write) {
    path_ = path;
    write_delay_ = write_delay;
    on_read_ = std::move(on_read);
    on_write_ = std::move(on_write);

    task_runner_->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&Read<T>, path_),
        base::BindOnce(&PersistentProto<T>::OnReadComplete,
                       weak_factory_.GetWeakPtr()));
  }

  ~PersistentProto() {}

  PersistentProto(const PersistentProto&) = delete;
  PersistentProto& operator=(const PersistentProto&) = delete;

  T* get() { return proto_.get(); }

  T* operator->() {
    CHECK(proto_);
    return proto_.get();
  }

  const T* operator->() const {
    CHECK(proto_);
    return proto_.get();
  }

  T operator*() {
    CHECK(proto_);
    return *proto_;
  }

  bool initialized() const { return initialized_; }

  constexpr bool has_value() const { return proto_.get() != nullptr; }

  constexpr explicit operator bool() const { return has_value(); }

  // Write the backing proto to disk after |save_delay_ms_| has elapsed.
  void QueueWrite() {
    DCHECK(proto_);
    if (!proto_)
      return;

    // If a save is already queued, do nothing.
    if (write_is_queued_)
      return;
    write_is_queued_ = true;

    base::SequencedTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&PersistentProto<T>::OnQueueWrite,
                       weak_factory_.GetWeakPtr()),
        write_delay_);
  }

  // Write the backing proto to disk 'now'.
  void StartWrite() {
    DCHECK(proto_);
    if (!proto_)
      return;

    // Serialize the proto outside of the posted task, because otherwise we need
    // to pass a proto pointer into the task. This causes a rare race condition
    // during destruction where the proto can be destroyed before serialization,
    // causing a crash.
    std::string proto_str;
    if (!proto_->SerializeToString(&proto_str))
      OnWriteComplete(WriteStatus::kSerializationError);

    // The SequentialTaskRunner ensures the writes won't trip over each other,
    // so we can schedule without checking whether another write is currently
    // active.
    task_runner_->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&Write, path_, proto_str),
        base::BindOnce(&PersistentProto<T>::OnWriteComplete,
                       weak_factory_.GetWeakPtr()));
  }

  // Safely clear this proto from memory and disk. This is preferred to clearing
  // the proto, because it ensures the proto is purged even if called before the
  // backing file is read from disk. In this case, the file is overwritten after
  // it has been read. In either case, the file is written as soon as possible,
  // skipping the |save_delay_ms_| wait time.
  void Purge() {
    if (proto_) {
      proto_.reset();
      proto_ = std::make_unique<T>();
      StartWrite();
    } else {
      purge_after_reading_ = true;
    }
  }

 private:
  void OnReadComplete(std::pair<ReadStatus, std::unique_ptr<T>> result) {
    base::UmaHistogramEnumeration("Apps.AppList.PersistentProto.ReadStatus",
                                  result.first);
    if (result.first == ReadStatus::kOk) {
      proto_ = std::move(result.second);
    } else {
      proto_ = std::make_unique<T>();
      QueueWrite();
    }

    if (purge_after_reading_) {
      proto_.reset();
      proto_ = std::make_unique<T>();
      StartWrite();
      purge_after_reading_ = false;
    }

    initialized_ = true;
    std::move(on_read_).Run(result.first);
  }

  void OnWriteComplete(const WriteStatus status) {
    base::UmaHistogramEnumeration("Apps.AppList.PersistentProto.WriteStatus",
                                  status);
    on_write_.Run(status);
  }

  void OnQueueWrite() {
    // Reset the queued flag before posting the task. Last-moment updates to
    // |proto_| will post another task to write the proto, avoiding race
    // conditions.
    write_is_queued_ = false;
    StartWrite();
  }

  // Path on disk to read from and write to.
  base::FilePath path_;

  // How long to delay writing to disk for on a call to QueueWrite.
  base::TimeDelta write_delay_;

  // Whether the proto has finished reading from disk. |proto_| will be empty
  // before |initialized_| is true.
  bool initialized_ = false;

  // Whether or not a write is currently scheduled.
  bool write_is_queued_ = false;

  // Whether we should immediately clear the proto after reading it.
  bool purge_after_reading_ = false;

  // Run when the cache finishes reading from disk, if provided.
  ReadCallback on_read_;

  // Run when the cache finishes writing to disk, if provided.
  WriteCallback on_write_;

  // The proto itself.
  std::unique_ptr<T> proto_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  base::WeakPtrFactory<PersistentProto> weak_factory_{this};
};

}  // namespace app_list

#endif  // CHROME_BROWSER_UI_APP_LIST_SEARCH_RANKING_PERSISTENT_PROTO_H_

// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/value_store/value_store_frontend.h"

#include <utility>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/trace_event/trace_event.h"
#include "components/value_store/leveldb_value_store.h"
#include "components/value_store/value_store.h"
#include "components/value_store/value_store_factory.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace value_store {

class ValueStoreFrontend::Backend : public base::RefCountedThreadSafe<Backend> {
 public:
  Backend(const scoped_refptr<ValueStoreFactory>& store_factory,
          const base::FilePath& directory,
          const std::string& uma_client_name,
          const scoped_refptr<base::SequencedTaskRunner>& task_runner)
      : store_factory_(store_factory),
        directory_(directory),
        uma_client_name_(uma_client_name),
        task_runner_(task_runner) {}
  Backend(const Backend&) = delete;
  Backend& operator=(const Backend&) = delete;

  void Get(const std::string& key, ValueStoreFrontend::ReadCallback callback) {
    DCHECK(task_runner_->RunsTasksInCurrentSequence());
    LazyInit();
    ValueStore::ReadResult result = storage_->Get(key);

    // Extract the value from the ReadResult and pass ownership of it to the
    // callback.
    absl::optional<base::Value> value;
    if (result.status().ok()) {
      value = result.settings().ExtractKey(key);
    } else {
      LOG(WARNING) << "Reading " << key << " from " << db_path_.value()
                   << " failed: " << result.status().message;
    }

    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&ValueStoreFrontend::Backend::RunCallback, this,
                       std::move(callback),
                       value.has_value()
                           ? base::Value::ToUniquePtrValue(std::move(*value))
                           : nullptr));
  }

  void Set(const std::string& key, std::unique_ptr<base::Value> value) {
    DCHECK(task_runner_->RunsTasksInCurrentSequence());
    LazyInit();
    // We don't need the old value, so skip generating changes.
    ValueStore::WriteResult result = storage_->Set(
        ValueStore::IGNORE_QUOTA | ValueStore::NO_GENERATE_CHANGES, key,
        *value);
    LOG_IF(ERROR, !result.status().ok())
        << "Error while writing " << key << " to " << db_path_.value();
  }

  void Remove(const std::string& key) {
    DCHECK(task_runner_->RunsTasksInCurrentSequence());
    LazyInit();
    storage_->Remove(key);
  }

 private:
  friend class base::RefCountedThreadSafe<Backend>;

  virtual ~Backend() {
    if (storage_ && !task_runner_->RunsTasksInCurrentSequence())
      task_runner_->DeleteSoon(FROM_HERE, storage_.release());
  }

  void LazyInit() {
    DCHECK(task_runner_->RunsTasksInCurrentSequence());
    if (storage_)
      return;
    TRACE_EVENT0("ValueStoreFrontend::Backend", "LazyInit");
    storage_ = store_factory_->CreateValueStore(directory_, uma_client_name_);
  }

  void RunCallback(ValueStoreFrontend::ReadCallback callback,
                   std::unique_ptr<base::Value> value) {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    std::move(callback).Run(std::move(value));
  }

  // The factory which will be used to lazily create the ValueStore when needed.
  // Used exclusively on the backend sequence.
  scoped_refptr<ValueStoreFactory> store_factory_;
  const base::FilePath directory_;
  const std::string uma_client_name_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  // The actual ValueStore that handles persisting the data to disk. Used
  // exclusively on the backend sequence.
  std::unique_ptr<ValueStore> storage_;

  base::FilePath db_path_;
};

ValueStoreFrontend::ValueStoreFrontend(
    const scoped_refptr<ValueStoreFactory>& store_factory,
    const base::FilePath& directory,
    const std::string& uma_client_name,
    const scoped_refptr<base::SequencedTaskRunner>& task_runner)
    : backend_(base::MakeRefCounted<Backend>(store_factory,
                                             directory,
                                             uma_client_name,
                                             task_runner)),
      task_runner_(task_runner) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

ValueStoreFrontend::~ValueStoreFrontend() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

void ValueStoreFrontend::Get(const std::string& key, ReadCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ValueStoreFrontend::Backend::Get, backend_,
                                key, std::move(callback)));
}

void ValueStoreFrontend::Set(const std::string& key,
                             std::unique_ptr<base::Value> value) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ValueStoreFrontend::Backend::Set, backend_,
                                key, std::move(value)));
}

void ValueStoreFrontend::Remove(const std::string& key) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&ValueStoreFrontend::Backend::Remove, backend_, key));
}

}  // namespace value_store

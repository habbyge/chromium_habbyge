// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/version/version_handler.h"

#include <stddef.h>

#include <string>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/metrics/field_trial.h"
#include "base/task/post_task.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "components/variations/active_field_trials.h"
#include "components/version_ui/version_handler_helper.h"
#include "components/version_ui/version_ui_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

namespace {

// Retrieves the executable and profile paths on the FILE thread.
void GetFilePaths(const base::FilePath& profile_path,
                  std::u16string* exec_path_out,
                  std::u16string* profile_path_out) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  base::FilePath executable_path = base::MakeAbsoluteFilePath(
      base::CommandLine::ForCurrentProcess()->GetProgram());
  if (!executable_path.empty())
    *exec_path_out = executable_path.LossyDisplayName();
  else
    *exec_path_out = l10n_util::GetStringUTF16(IDS_VERSION_UI_PATH_NOTFOUND);

  base::FilePath profile_path_copy(base::MakeAbsoluteFilePath(profile_path));
  if (!profile_path.empty() && !profile_path_copy.empty())
    *profile_path_out = profile_path.LossyDisplayName();
  else
    *profile_path_out = l10n_util::GetStringUTF16(IDS_VERSION_UI_PATH_NOTFOUND);
}

}  // namespace

VersionHandler::VersionHandler() {}

VersionHandler::~VersionHandler() {}

void VersionHandler::RegisterMessages() {
  web_ui()->RegisterDeprecatedMessageCallback(
      version_ui::kRequestVersionInfo,
      base::BindRepeating(&VersionHandler::HandleRequestVersionInfo,
                          base::Unretained(this)));
  web_ui()->RegisterDeprecatedMessageCallback(
      version_ui::kRequestVariationInfo,
      base::BindRepeating(&VersionHandler::HandleRequestVariationInfo,
                          base::Unretained(this)));
  web_ui()->RegisterDeprecatedMessageCallback(
      version_ui::kRequestPathInfo,
      base::BindRepeating(&VersionHandler::HandleRequestPathInfo,
                          base::Unretained(this)));
}

void VersionHandler::HandleRequestVersionInfo(const base::ListValue* args) {
  // This method is overridden by platform-specific handlers which may still
  // use |CallJavascriptFunction|. Main version info is returned by promise
  // using handlers below.
  // TODO(orinj): To fully eliminate chrome.send usage in JS, derived classes
  // could be made to work more like this base class, using
  // |ResolveJavascriptCallback| instead of |CallJavascriptFunction|.
  AllowJavascript();
}

void VersionHandler::HandleRequestVariationInfo(const base::ListValue* args) {
  AllowJavascript();

  const auto& list = args->GetList();
  CHECK_EQ(2U, list.size());
  const std::string& callback_id = list[0].GetString();
  const bool include_variations_cmd = list[1].GetBool();

  base::Value response(base::Value::Type::DICTIONARY);
  response.SetKey(version_ui::kKeyVariationsList,
                  version_ui::GetVariationsList());
  if (include_variations_cmd) {
    response.SetKey(version_ui::kKeyVariationsCmd,
                    version_ui::GetVariationsCommandLineAsValue());
  }
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void VersionHandler::HandleRequestPathInfo(const base::ListValue* args) {
  AllowJavascript();

  CHECK_EQ(1U, args->GetList().size());
  const std::string& callback_id = args->GetList()[0].GetString();

  // Grab the executable path on the FILE thread. It is returned in
  // OnGotFilePaths.
  std::u16string* exec_path_buffer = new std::u16string;
  std::u16string* profile_path_buffer = new std::u16string;
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&GetFilePaths, Profile::FromWebUI(web_ui())->GetPath(),
                     base::Unretained(exec_path_buffer),
                     base::Unretained(profile_path_buffer)),
      base::BindOnce(&VersionHandler::OnGotFilePaths,
                     weak_ptr_factory_.GetWeakPtr(), callback_id,
                     base::Owned(exec_path_buffer),
                     base::Owned(profile_path_buffer)));
}

void VersionHandler::OnGotFilePaths(std::string callback_id,
                                    std::u16string* executable_path_data,
                                    std::u16string* profile_path_data) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  base::Value response(base::Value::Type::DICTIONARY);
  response.SetKey(version_ui::kKeyExecPath, base::Value(*executable_path_data));
  response.SetKey(version_ui::kKeyProfilePath, base::Value(*profile_path_data));
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

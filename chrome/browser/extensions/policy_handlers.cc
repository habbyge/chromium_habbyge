// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/policy_handlers.h"

#include <stddef.h>
#include <unordered_set>
#include <utility>

#include "base/check.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/extension_management_constants.h"
#include "chrome/browser/extensions/external_policy_loader.h"
#include "components/crx_file/id_util.h"
#include "components/policy/core/browser/policy_error_map.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/schema.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_value_map.h"
#include "components/strings/grit/components_strings.h"
#include "extensions/browser/pref_names.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_urls.h"
#include "url/gurl.h"

#if defined(OS_WIN)
#include "base/enterprise_util.h"
#endif

namespace extensions {
namespace {
// Returns true if extensions_ids contains a list of valid extension ids,
// divided by comma.
bool IsValidIdList(const std::string& extension_ids) {
  std::vector<base::StringPiece> ids = base::SplitStringPiece(
      extension_ids, ",", base::WhitespaceHandling::TRIM_WHITESPACE,
      base::SplitResult::SPLIT_WANT_NONEMPTY);
  if (ids.size() == 0)
    return false;
  for (const auto& id : ids) {
    if (!crx_file::id_util::IdIsValid(std::string(id)))
      return false;
  }
  return true;
}
}  // namespace
// ExtensionListPolicyHandler implementation -----------------------------------

ExtensionListPolicyHandler::ExtensionListPolicyHandler(const char* policy_name,
                                                       const char* pref_path,
                                                       bool allow_wildcards)
    : policy::ListPolicyHandler(policy_name, base::Value::Type::STRING),
      pref_path_(pref_path),
      allow_wildcards_(allow_wildcards) {}

ExtensionListPolicyHandler::~ExtensionListPolicyHandler() {}

bool ExtensionListPolicyHandler::CheckListEntry(const base::Value& value) {
  const std::string& str = value.GetString();
  if (allow_wildcards_ && str == "*")
    return true;

  // Make sure str is an extension id.
  return crx_file::id_util::IdIsValid(str);
}

void ExtensionListPolicyHandler::ApplyList(base::Value filtered_list,
                                           PrefValueMap* prefs) {
  DCHECK(filtered_list.is_list());
  prefs->SetValue(pref_path_, std::move(filtered_list));
}

// ExtensionInstallForceListPolicyHandler implementation -----------------------

ExtensionInstallForceListPolicyHandler::ExtensionInstallForceListPolicyHandler()
    : policy::TypeCheckingPolicyHandler(policy::key::kExtensionInstallForcelist,
                                        base::Value::Type::LIST) {}

bool ExtensionInstallForceListPolicyHandler::CheckPolicySettings(
    const policy::PolicyMap& policies,
    policy::PolicyErrorMap* errors) {
  const base::Value* value;
  return CheckAndGetValue(policies, errors, &value) &&
         ParseList(value, nullptr, errors);
}

void ExtensionInstallForceListPolicyHandler::ApplyPolicySettings(
    const policy::PolicyMap& policies,
    PrefValueMap* prefs) {
  const base::Value* value = nullptr;
  base::DictionaryValue dict;
  if (CheckAndGetValue(policies, nullptr, &value) && value &&
      ParseList(value, &dict, nullptr)) {
    prefs->SetValue(pref_names::kInstallForceList, std::move(dict));
  }
}

bool ExtensionInstallForceListPolicyHandler::ParseList(
    const base::Value* policy_value,
    base::DictionaryValue* extension_dict,
    policy::PolicyErrorMap* errors) {
  if (!policy_value)
    return true;

  if (!policy_value->is_list()) {
    // This should have been caught in CheckPolicySettings.
    NOTREACHED();
    return false;
  }

  int index = -1;
  for (const auto& entry : policy_value->GetList()) {
    ++index;
    if (!entry.is_string()) {
      if (errors) {
        errors->AddError(policy_name(), index, IDS_POLICY_TYPE_ERROR,
                         base::Value::GetTypeName(base::Value::Type::STRING));
      }
      continue;
    }
    std::string entry_string = entry.GetString();

    // Each string item of the list should be of one of the following forms:
    // * <extension_id>
    // * <extension_id>;<update_url>
    // Note: The update URL might also contain semicolons.
    std::string extension_id;
    std::string update_url;
    size_t pos = entry_string.find(';');
    if (pos == std::string::npos) {
      extension_id = entry_string;
      update_url = extension_urls::kChromeWebstoreUpdateURL;
    } else {
      extension_id = entry_string.substr(0, pos);
      update_url = entry_string.substr(pos + 1);
    }

    if (!crx_file::id_util::IdIsValid(extension_id) ||
        !GURL(update_url).is_valid()) {
      if (errors) {
        errors->AddError(policy_name(), index, IDS_POLICY_VALUE_FORMAT_ERROR);
      }
      continue;
    }

    if (extension_dict) {
      ExternalPolicyLoader::AddExtension(extension_dict, extension_id,
                                         update_url);
    }
  }

  return true;
}

// ExtensionURLPatternListPolicyHandler implementation -------------------------

ExtensionURLPatternListPolicyHandler::ExtensionURLPatternListPolicyHandler(
    const char* policy_name,
    const char* pref_path)
    : policy::TypeCheckingPolicyHandler(policy_name, base::Value::Type::LIST),
      pref_path_(pref_path) {}

ExtensionURLPatternListPolicyHandler::~ExtensionURLPatternListPolicyHandler() {}

bool ExtensionURLPatternListPolicyHandler::CheckPolicySettings(
    const policy::PolicyMap& policies,
    policy::PolicyErrorMap* errors) {
  const base::Value* value = NULL;
  if (!CheckAndGetValue(policies, errors, &value))
    return false;

  if (!value)
    return true;

  if (!value->is_list()) {
    NOTREACHED();
    return false;
  }

  // Check that the list contains valid URLPattern strings only.
  int index = 0;
  for (const auto& entry : value->GetList()) {
    if (!entry.is_string()) {
      errors->AddError(policy_name(), index, IDS_POLICY_TYPE_ERROR,
                       base::Value::GetTypeName(base::Value::Type::STRING));
      return false;
    }
    std::string url_pattern_string = entry.GetString();

    URLPattern pattern(URLPattern::SCHEME_ALL);
    if (pattern.Parse(url_pattern_string) !=
        URLPattern::ParseResult::kSuccess) {
      errors->AddError(policy_name(), index, IDS_POLICY_VALUE_FORMAT_ERROR);
      return false;
    }
    ++index;
  }

  return true;
}

void ExtensionURLPatternListPolicyHandler::ApplyPolicySettings(
    const policy::PolicyMap& policies,
    PrefValueMap* prefs) {
  if (!pref_path_)
    return;
  const base::Value* value = policies.GetValue(policy_name());
  if (value)
    prefs->SetValue(pref_path_, value->Clone());
}

// ExtensionSettingsPolicyHandler implementation  ------------------------------

ExtensionSettingsPolicyHandler::ExtensionSettingsPolicyHandler(
    const policy::Schema& chrome_schema)
    : policy::SchemaValidatingPolicyHandler(
          policy::key::kExtensionSettings,
          chrome_schema.GetKnownProperty(policy::key::kExtensionSettings),
          policy::SCHEMA_ALLOW_UNKNOWN) {
}

ExtensionSettingsPolicyHandler::~ExtensionSettingsPolicyHandler() {
}

void ExtensionSettingsPolicyHandler::SanitizePolicySettings(
    base::Value* policy_value,
    policy::PolicyErrorMap* errors) {
  DCHECK(policy_value);

  // |policy_value| is expected to conform to the defined schema. But it's
  // not strictly valid since there are additional restrictions.
  const base::DictionaryValue* dict_value = NULL;
  DCHECK(policy_value->is_dict());
  policy_value->GetAsDictionary(&dict_value);

  // Dictionary entries with any invalid setting get removed at the end. We
  // can't mutate the dict while iterating, so store them here.
  std::unordered_set<std::string> invalid_keys;

  // Check each entry, populating |invalid_keys| and |errors|.
  for (auto entry : dict_value->DictItems()) {
    DCHECK(entry.first == schema_constants::kWildcard ||
           IsValidIdList(entry.first));
    DCHECK(entry.second.is_dict());

    // Extracts sub dictionary.
    const base::DictionaryValue* sub_dict = nullptr;
    entry.second.GetAsDictionary(&sub_dict);

    std::string installation_mode;
    if (sub_dict->GetString(schema_constants::kInstallationMode,
                            &installation_mode)) {
      if (installation_mode == schema_constants::kForceInstalled ||
          installation_mode == schema_constants::kNormalInstalled) {
        DCHECK(entry.first != schema_constants::kWildcard);
        // Verifies that 'update_url' is specified for 'force_installed' and
        // 'normal_installed' mode.
        std::string update_url;
        if (!sub_dict->GetString(schema_constants::kUpdateUrl, &update_url) ||
            update_url.empty()) {
          if (errors) {
            errors->AddError(policy_name(),
                             entry.first + "." + schema_constants::kUpdateUrl,
                             IDS_POLICY_NOT_SPECIFIED_ERROR);
          }
          invalid_keys.insert(entry.first);
          continue;
        }
        if (GURL(update_url).is_valid()) {
// Unless enterprise managed only extensions from the Chrome Webstore
// can be force installed.
#if defined(OS_WIN)
          // We can't use IsWebstoreUpdateUrl() here since the ExtensionClient
          // isn't set this early during startup.
          if (!base::IsMachineExternallyManaged() &&
              !base::LowerCaseEqualsASCII(
                  update_url, extension_urls::kChromeWebstoreUpdateURL)) {
            if (errors) {
              errors->AddError(policy_name(), entry.first,
                               IDS_POLICY_OFF_CWS_URL_ERROR,
                               extension_urls::kChromeWebstoreUpdateURL);
            }
            invalid_keys.insert(entry.first);
            continue;
          }
#endif
        } else {
          // Warns about an invalid update URL.
          if (errors) {
            errors->AddError(policy_name(), IDS_POLICY_INVALID_UPDATE_URL_ERROR,
                             entry.first);
          }
          invalid_keys.insert(entry.first);
          continue;
        }
      }
    }
    // Host keys that don't support user defined paths.
    const char* host_keys[] = {schema_constants::kPolicyBlockedHosts,
                               schema_constants::kPolicyAllowedHosts};
    const int extension_scheme_mask =
        URLPattern::GetValidSchemeMaskForExtensions();
    for (const char* key : host_keys) {
      const base::Value* unparsed_urls = sub_dict->FindListKey(key);
      if (unparsed_urls != nullptr) {
        for (const auto& url_value : unparsed_urls->GetList()) {
          const std::string& unparsed_url = url_value.GetString();
          URLPattern pattern(extension_scheme_mask);
          URLPattern::ParseResult parse_result = pattern.Parse(unparsed_url);
          // These keys don't support paths due to how we track the initiator
          // of a webRequest and cookie security policy. We expect a valid
          // pattern to return a PARSE_ERROR_EMPTY_PATH.
          if (parse_result == URLPattern::ParseResult::kEmptyPath) {
            // Add a wildcard path to the URL as it should match any path.
            parse_result = pattern.Parse(unparsed_url + "/*");
          } else if (parse_result == URLPattern::ParseResult::kSuccess) {
            // The user supplied a path, notify them that this is not supported.
            if (!pattern.match_all_urls()) {
              if (errors) {
                errors->AddError(
                    policy_name(), entry.first,
                    "The URL pattern '" + unparsed_url + "' for attribute " +
                        key + " has a path specified. Paths are not " +
                        "supported, please remove the path and try again. " +
                        "e.g. *://example.com/ => *://example.com");
              }
              invalid_keys.insert(entry.first);
              break;
            }
          }
          if (parse_result != URLPattern::ParseResult::kSuccess) {
            if (errors) {
              errors->AddError(policy_name(), entry.first,
                               "Invalid URL pattern '" + unparsed_url +
                                   "' for attribute " + key);
            }
            invalid_keys.insert(entry.first);
            break;
          }
        }
      }
    }

    const base::Value* runtime_blocked_hosts =
        sub_dict->FindListKey(schema_constants::kPolicyBlockedHosts);
    if (runtime_blocked_hosts != nullptr &&
        runtime_blocked_hosts->GetList().size() >
            schema_constants::kMaxItemsURLPatternSet) {
      if (errors) {
        errors->AddError(
            policy_name(),
            entry.first + "." + schema_constants::kPolicyBlockedHosts,
            IDS_POLICY_EXTENSION_SETTINGS_ORIGIN_LIMIT_WARNING,
            base::NumberToString(schema_constants::kMaxItemsURLPatternSet));
      }
    }

    const base::Value* runtime_allowed_hosts =
        sub_dict->FindListKey(schema_constants::kPolicyAllowedHosts);
    if (runtime_allowed_hosts != nullptr &&
        runtime_allowed_hosts->GetList().size() >
            schema_constants::kMaxItemsURLPatternSet) {
      if (errors) {
        errors->AddError(
            policy_name(),
            entry.first + "." + schema_constants::kPolicyAllowedHosts,
            IDS_POLICY_EXTENSION_SETTINGS_ORIGIN_LIMIT_WARNING,
            base::NumberToString(schema_constants::kMaxItemsURLPatternSet));
      }
    }
  }

  // Remove |invalid_keys| from the dictionary.
  for (const std::string& key : invalid_keys)
    policy_value->RemoveKey(key);
}

bool ExtensionSettingsPolicyHandler::CheckPolicySettings(
    const policy::PolicyMap& policies,
    policy::PolicyErrorMap* errors) {
  std::unique_ptr<base::Value> policy_value;
  if (!CheckAndGetValue(policies, errors, &policy_value))
    return false;
  if (!policy_value)
    return true;

  SanitizePolicySettings(policy_value.get(), errors);
  return true;
}

void ExtensionSettingsPolicyHandler::ApplyPolicySettings(
    const policy::PolicyMap& policies,
    PrefValueMap* prefs) {
  std::unique_ptr<base::Value> policy_value;
  if (!CheckAndGetValue(policies, NULL, &policy_value) || !policy_value)
    return;
  SanitizePolicySettings(policy_value.get(), nullptr);
  prefs->SetValue(pref_names::kExtensionManagement,
                  base::Value::FromUniquePtrValue(std::move(policy_value)));
}

}  // namespace extensions

// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Subpages
import './appearance_page/appearance_fonts_page.js';
import './autofill_page/password_check.js';
import './autofill_page/passwords_section.js';
import './autofill_page/passwords_device_section.js';
import './autofill_page/payments_section.js';
import './clear_browsing_data_dialog/clear_browsing_data_dialog.js';
import './search_engines_page/search_engines_page.js';
import './privacy_page/privacy_review/privacy_review_description_item.js';
import './privacy_page/privacy_review/privacy_review_history_sync_fragment.js';
import './privacy_page/privacy_review/privacy_review_msbb_fragment.js';
import './privacy_page/privacy_review/privacy_review_page.js';
import './privacy_page/security_keys_subpage.js';
import './privacy_page/security_page.js';
import './site_settings/all_sites.js';
import './site_settings/site_data_details_subpage.js';
import './site_settings_page/site_settings_page.js';
import './site_settings/category_default_setting.js';
import './site_settings/category_setting_exceptions.js';
import './site_settings/chooser_exception_list.js';
import './site_settings/media_picker.js';
import './site_settings/pdf_documents.js';
import './site_settings/protocol_handlers.js';
import './site_settings/settings_category_default_radio_group.js';
import './site_settings/site_data.js';
import './site_settings/site_details.js';
import './site_settings/zoom_levels.js';
// <if expr="not chromeos and not lacros">
import './people_page/import_data_dialog.js';
// </if>
// <if expr="not chromeos">
import './people_page/manage_profile.js';
// </if>
import './people_page/signout_dialog.js';
import './people_page/sync_controls.js';
import './people_page/sync_page.js';
// <if expr="use_nss_certs">
import 'chrome://resources/cr_components/certificate_manager/certificate_manager.js';
// </if>

// Sections
import './a11y_page/a11y_page.js';
import './downloads_page/downloads_page.js';
// <if expr="not chromeos">
import './languages_page/languages_page.js';
// </if>
import './reset_page/reset_page.js';
// <if expr="not chromeos and not lacros">
import './system_page/system_page.js';
// </if>

// <if expr="not chromeos and not is_macosx">
import './languages_page/edit_dictionary_page.js';

// </if>

export {CrInputElement} from 'chrome://resources/cr_elements/cr_input/cr_input.m.js';
export {CrSliderElement} from 'chrome://resources/cr_elements/cr_slider/cr_slider.js';
export {getToastManager} from 'chrome://resources/cr_elements/cr_toast/cr_toast_manager.js';
export {SettingsAppearanceFontsPageElement} from './appearance_page/appearance_fonts_page.js';
export {FontsBrowserProxy, FontsBrowserProxyImpl, FontsData} from './appearance_page/fonts_browser_proxy.js';
export {CountryDetailManagerImpl} from './autofill_page/address_edit_dialog.js';
export {AutofillManager, AutofillManagerImpl} from './autofill_page/autofill_section.js';
// <if expr="chromeos or lacros">
export {BlockingRequestManager} from './autofill_page/blocking_request_manager.js';
// </if>
export {PaymentsManager, PaymentsManagerImpl} from './autofill_page/payments_section.js';
// <if expr="_google_chrome and is_win">
export {ChromeCleanupIdleReason} from './chrome_cleanup_page/chrome_cleanup_page.js';
export {ChromeCleanupProxy, ChromeCleanupProxyImpl} from './chrome_cleanup_page/chrome_cleanup_proxy.js';
export {CHROME_CLEANUP_DEFAULT_ITEMS_TO_SHOW} from './chrome_cleanup_page/items_to_remove_list.js';
// </if>
export {ClearBrowsingDataBrowserProxy, ClearBrowsingDataBrowserProxyImpl, ClearBrowsingDataResult, InstalledApp} from './clear_browsing_data_dialog/clear_browsing_data_browser_proxy.js';
export {SettingsClearBrowsingDataDialogElement} from './clear_browsing_data_dialog/clear_browsing_data_dialog.js';
export {SettingsHistoryDeletionDialogElement} from './clear_browsing_data_dialog/history_deletion_dialog.js';
export {SettingsPasswordsDeletionDialogElement} from './clear_browsing_data_dialog/passwords_deletion_dialog.js';
export {ControlledButtonElement} from './controls/controlled_button.js';
export {SettingsCheckboxElement} from './controls/settings_checkbox.js';
export {SettingsSliderElement} from './controls/settings_slider.js';
export {SettingsTextareaElement} from './controls/settings_textarea.js';
export {DownloadsBrowserProxy, DownloadsBrowserProxyImpl} from './downloads_page/downloads_browser_proxy.js';
export {SettingsDownloadsPageElement} from './downloads_page/downloads_page.js';
// <if expr="_google_chrome and is_win">
export {IncompatibleApplication, IncompatibleApplicationsBrowserProxyImpl} from './incompatible_applications_page/incompatible_applications_browser_proxy.js';
// </if>
// <if expr="not chromeos">
export {LanguagesBrowserProxy, LanguagesBrowserProxyImpl} from './languages_page/languages_browser_proxy.js';
export {LanguageSettingsActionType, LanguageSettingsMetricsProxy, LanguageSettingsMetricsProxyImpl, LanguageSettingsPageImpressionType} from './languages_page/languages_settings_metrics_proxy.js';
export {kMenuCloseDelay, SettingsLanguagesSubpageElement} from './languages_page/languages_subpage.js';
export {LanguageHelper, LanguagesModel} from './languages_page/languages_types.js';
// </if>
// <if expr="not chromeos and not lacros">
export {ImportDataBrowserProxyImpl, ImportDataStatus} from './people_page/import_data_browser_proxy.js';
// </if>
// <if expr="not chromeos">
export {ManageProfileBrowserProxyImpl, ProfileShortcutStatus} from './people_page/manage_profile_browser_proxy.js';
// </if>
export {SettingsCollapseRadioButtonElement} from './privacy_page/collapse_radio_button.js';
export {SettingsCookiesPageElement} from './privacy_page/cookies_page.js';
export {SettingsDoNotTrackToggleElement} from './privacy_page/do_not_track_toggle.js';
export {PrivacyReviewStep} from './privacy_page/privacy_review/constants.js';
export {PrivacyReviewClearOnExitFragmentElement} from './privacy_page/privacy_review/privacy_review_clear_on_exit_fragment.js';
export {PrivacyReviewDescriptionItemElement} from './privacy_page/privacy_review/privacy_review_description_item.js';
export {PrivacyReviewHistorySyncFragmentElement} from './privacy_page/privacy_review/privacy_review_history_sync_fragment.js';
export {PrivacyReviewMsbbFragmentElement} from './privacy_page/privacy_review/privacy_review_msbb_fragment.js';
export {SettingsPrivacyReviewPageElement} from './privacy_page/privacy_review/privacy_review_page.js';
export {SettingsSecureDnsElement} from './privacy_page/secure_dns.js';
export {SecureDnsInputElement} from './privacy_page/secure_dns_input.js';
export {BioEnrollDialogPage} from './privacy_page/security_keys_bio_enroll_dialog.js';
export {Ctap2Status, SampleStatus, SecurityKeysBioEnrollProxyImpl, SecurityKeysCredentialBrowserProxyImpl, SecurityKeysPINBrowserProxyImpl, SecurityKeysResetBrowserProxyImpl} from './privacy_page/security_keys_browser_proxy.js';
export {CredentialManagementDialogPage} from './privacy_page/security_keys_credential_management_dialog.js';
export {ResetDialogPage} from './privacy_page/security_keys_reset_dialog.js';
export {SetPINDialogPage} from './privacy_page/security_keys_set_pin_dialog.js';
export {SafeBrowsingSetting, SettingsSecurityPageElement} from './privacy_page/security_page.js';
export {SettingsOmniboxExtensionEntryElement} from './search_engines_page/omnibox_extension_entry.js';
export {SettingsSearchEngineDialogElement} from './search_engines_page/search_engine_dialog.js';
export {SettingsSearchEngineEntryElement} from './search_engines_page/search_engine_entry.js';
export {SettingsSearchEnginesPageElement} from './search_engines_page/search_engines_page.js';
export {AddSiteDialogElement} from './site_settings/add_site_dialog.js';
// <if expr="chromeos">
export {AndroidInfoBrowserProxy, AndroidInfoBrowserProxyImpl, AndroidSmsInfo} from './site_settings/android_info_browser_proxy.js';
// </if>
export {CategorySettingExceptionsElement} from './site_settings/category_setting_exceptions.js';
export {ChooserType, ContentSetting, ContentSettingsTypes, CookieControlsMode, NotificationSetting, SITE_EXCEPTION_WILDCARD, SiteSettingSource, SortMethod} from './site_settings/constants.js';
export {CookieDetails, cookieInfo} from './site_settings/cookie_info.js';
export {SettingsEditExceptionDialogElement} from './site_settings/edit_exception_dialog.js';
export {LocalDataBrowserProxy, LocalDataBrowserProxyImpl, LocalDataItem} from './site_settings/local_data_browser_proxy.js';
export {AppHandlerEntry, AppProtocolEntry, HandlerEntry, ProtocolEntry} from './site_settings/protocol_handlers.js';
export {SettingsCategoryDefaultRadioGroupElement} from './site_settings/settings_category_default_radio_group.js';
export {SiteListElement} from './site_settings/site_list.js';
export {ContentSettingProvider, CookiePrimarySetting, DefaultContentSetting, OriginInfo, RawChooserException, RawSiteException, RecentSitePermissions, SiteException, SiteGroup, SiteSettingsPrefsBrowserProxy, SiteSettingsPrefsBrowserProxyImpl, ZoomLevelEntry} from './site_settings/site_settings_prefs_browser_proxy.js';
export {WebsiteUsageBrowserProxyImpl} from './site_settings/website_usage_browser_proxy.js';
export {SettingsRecentSitePermissionsElement} from './site_settings_page/recent_site_permissions.js';
export {defaultSettingLabel} from './site_settings_page/site_settings_list.js';
export {SettingsSiteSettingsPageElement} from './site_settings_page/site_settings_page.js';
// <if expr="not chromeos and not lacros">
export {SystemPageBrowserProxyImpl} from './system_page/system_page_browser_proxy.js';

// </if>

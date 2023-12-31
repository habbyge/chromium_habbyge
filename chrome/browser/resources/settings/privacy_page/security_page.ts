// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_icon_button/cr_icon_button.m.js';
import 'chrome://resources/cr_elements/cr_link_row/cr_link_row.js';
import 'chrome://resources/cr_elements/shared_style_css.m.js';
import './collapse_radio_button.js';
import './secure_dns.js';
import '../controls/settings_radio_group.js';
import '../controls/settings_toggle_button.js';
import '../icons.js';
import '../prefs/prefs.js';
import '../settings_shared_css.js';
import './disable_safebrowsing_dialog.js';

import {assert} from 'chrome://resources/js/assert.m.js';
import {focusWithoutInk} from 'chrome://resources/js/cr/ui/focus_without_ink.m.js';
import {I18nMixin, I18nMixinInterface} from 'chrome://resources/js/i18n_mixin.js';
import {html, PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {SettingsRadioGroupElement} from '../controls/settings_radio_group.js';
import {SettingsToggleButtonElement} from '../controls/settings_toggle_button.js';
import {loadTimeData} from '../i18n_setup.js';
import {MetricsBrowserProxy, MetricsBrowserProxyImpl, PrivacyElementInteractions, SafeBrowsingInteractions} from '../metrics_browser_proxy.js';
import {OpenWindowProxyImpl} from '../open_window_proxy.js';
import {PrefsMixin, PrefsMixinInterface} from '../prefs/prefs_mixin.js';
import {routes} from '../route.js';
import {Route, RouteObserverMixin, RouteObserverMixinInterface, Router} from '../router.js';

import {SettingsCollapseRadioButtonElement} from './collapse_radio_button.js';
import {SettingsDisableSafebrowsingDialogElement} from './disable_safebrowsing_dialog.js';
import {PrivacyPageBrowserProxy, PrivacyPageBrowserProxyImpl} from './privacy_page_browser_proxy.js';

/**
 * Enumeration of all safe browsing modes. Must be kept in sync with the enum
 * of the same name located in:
 * chrome/browser/safe_browsing/generated_safe_browsing_pref.h
 */
export enum SafeBrowsingSetting {
  ENHANCED = 0,
  STANDARD = 1,
  DISABLED = 2,
}

type FocusConfig = Map<string, (string|(() => void))>;

export interface SettingsSecurityPageElement {
  $: {
    passwordsLeakToggle: SettingsToggleButtonElement,
    safeBrowsingDisabled: SettingsCollapseRadioButtonElement,
    safeBrowsingEnhanced: SettingsCollapseRadioButtonElement,
    safeBrowsingRadioGroup: SettingsRadioGroupElement,
    safeBrowsingReportingToggle: SettingsToggleButtonElement,
    safeBrowsingStandard: SettingsCollapseRadioButtonElement,
  };
}

const SettingsSecurityPageElementBase =
    RouteObserverMixin(I18nMixin(PrefsMixin(PolymerElement))) as {
      new (): PolymerElement & I18nMixinInterface &
      RouteObserverMixinInterface & PrefsMixinInterface
    };

export class SettingsSecurityPageElement extends
    SettingsSecurityPageElementBase {
  static get is() {
    return 'settings-security-page';
  }

  static get template() {
    return html`{__html_template__}`;
  }

  static get properties() {
    return {
      /**
       * Preferences state.
       */
      prefs: {
        type: Object,
        notify: true,
      },

      /**
       * Whether the HTTPS-Only Mode setting should be displayed.
       */
      showHttpsOnlyModeSetting_: {
        type: Boolean,
        readOnly: true,
        value: function() {
          return loadTimeData.getBoolean('showHttpsOnlyModeSetting');
        },
      },

      /**
       * Whether the secure DNS setting should be displayed.
       */
      showSecureDnsSetting_: {
        type: Boolean,
        readOnly: true,
        value: function() {
          return loadTimeData.getBoolean('showSecureDnsSetting');
        },
      },

      // <if expr="chromeos or lacros">
      /**
       * Whether a link to secure DNS OS setting should be displayed.
       */
      showSecureDnsSettingLink_: {
        type: Boolean,
        readOnly: true,
        value: function() {
          return loadTimeData.getBoolean('showSecureDnsSettingLink');
        },
      },
      // </if>

      /**
       * Valid safe browsing states.
       */
      safeBrowsingSettingEnum_: {
        type: Object,
        value: SafeBrowsingSetting,
      },

      enableSecurityKeysSubpage_: {
        type: Boolean,
        readOnly: true,
        value() {
          return loadTimeData.getBoolean('enableSecurityKeysSubpage');
        }
      },

      focusConfig: {
        type: Object,
        observer: 'focusConfigChanged_',
      },

      showDisableSafebrowsingDialog_: Boolean,
    };
  }

  private showHttpsOnlyModeSetting_: boolean;
  private showSecureDnsSetting_: boolean;

  // <if expr="chromeos or lacros">
  private showSecureDnsSettingLink_: boolean;
  // </if>

  private enableSecurityKeysSubpage_: boolean;
  focusConfig: FocusConfig;
  private showDisableSafebrowsingDialog_: boolean;

  private browserProxy_: PrivacyPageBrowserProxy =
      PrivacyPageBrowserProxyImpl.getInstance();
  private metricsBrowserProxy_: MetricsBrowserProxy =
      MetricsBrowserProxyImpl.getInstance();

  private focusConfigChanged_(_newConfig: FocusConfig, oldConfig: FocusConfig) {
    assert(!oldConfig);
    // <if expr="use_nss_certs">
    if (routes.CERTIFICATES) {
      this.focusConfig.set(routes.CERTIFICATES.path, () => {
        focusWithoutInk(
            assert(this.shadowRoot!.querySelector('#manageCertificates')!));
      });
    }
    // </if>

    if (routes.SECURITY_KEYS) {
      this.focusConfig.set(routes.SECURITY_KEYS.path, () => {
        focusWithoutInk(assert(
            this.shadowRoot!.querySelector('#security-keys-subpage-trigger')!));
      });
    }
  }

  ready() {
    super.ready();

    // Expand initial pref value manually because automatic
    // expanding is disabled.
    const prefValue = this.getPref('generated.safe_browsing').value;
    if (prefValue === SafeBrowsingSetting.ENHANCED) {
      this.$.safeBrowsingEnhanced.expanded = true;
    } else if (prefValue === SafeBrowsingSetting.STANDARD) {
      this.$.safeBrowsingStandard.expanded = true;
    }
  }

  /**
   * RouteObserverMixin
   */
  currentRouteChanged(route: Route) {
    if (route === routes.SECURITY) {
      this.metricsBrowserProxy_.recordSafeBrowsingInteractionHistogram(
          SafeBrowsingInteractions.SAFE_BROWSING_SHOWED);
      const queryParams = Router.getInstance().getQueryParameters();
      const section = queryParams.get('q');
      if (section === 'enhanced') {
        this.$.safeBrowsingEnhanced.expanded = true;
        this.$.safeBrowsingStandard.expanded = false;
      }
    }
  }

  /**
   * Updates the buttons' expanded status by propagating previous click
   * events
   */
  private updateCollapsedButtons_() {
    this.$.safeBrowsingEnhanced.updateCollapsed();
    this.$.safeBrowsingStandard.updateCollapsed();
  }

  /**
   * Possibly displays the Safe Browsing disable dialog based on the users
   * selection.
   */
  private onSafeBrowsingRadioChange_() {
    const selected =
        Number.parseInt(this.$.safeBrowsingRadioGroup.selected, 10);
    const prefValue = this.getPref('generated.safe_browsing').value;
    if (prefValue !== selected) {
      this.recordInteractionHistogramOnRadioChange_(selected);
      this.recordActionOnRadioChange_(selected);
    }
    if (selected === SafeBrowsingSetting.DISABLED) {
      this.showDisableSafebrowsingDialog_ = true;
    } else {
      this.updateCollapsedButtons_();
      this.$.safeBrowsingRadioGroup.sendPrefChange();
    }
  }

  private getDisabledExtendedSafeBrowsing_(): boolean {
    return this.getPref('generated.safe_browsing').value !==
        SafeBrowsingSetting.STANDARD;
  }

  private getPasswordsLeakToggleSubLabel_(): string {
    let subLabel = this.i18n('passwordsLeakDetectionGeneralDescription');
    // If the backing password leak detection preference is enabled, but the
    // generated preference is off and user control is disabled, then additional
    // text explaining that the feature will be enabled if the user signs in is
    // added.
    const generatedPref = this.getPref('generated.password_leak_detection');
    if (this.getPref('profile.password_manager_leak_detection').value &&
        !generatedPref.value && generatedPref.userControlDisabled) {
      subLabel +=
          ' ' +  // Whitespace is a valid sentence separator w.r.t. i18n.
          this.i18n('passwordsLeakDetectionSignedOutEnabledDescription');
    }
    return subLabel;
  }

  private onManageCertificatesClick_() {
    // <if expr="use_nss_certs">
    Router.getInstance().navigateTo(routes.CERTIFICATES);
    // </if>
    // <if expr="is_win or is_macosx">
    this.browserProxy_.showManageSSLCertificates();
    // </if>
    this.metricsBrowserProxy_.recordSettingsPageHistogram(
        PrivacyElementInteractions.MANAGE_CERTIFICATES);
  }

  private onAdvancedProtectionProgramLinkClick_() {
    window.open(loadTimeData.getString('advancedProtectionURL'));
  }

  private onSecurityKeysClick_() {
    Router.getInstance().navigateTo(routes.SECURITY_KEYS);
  }

  private onSafeBrowsingExtendedReportingChange_() {
    this.metricsBrowserProxy_.recordSettingsPageHistogram(
        PrivacyElementInteractions.IMPROVE_SECURITY);
  }

  /**
   * Handles the closure of the disable safebrowsing dialog, reselects the
   * appropriate radio button if the user cancels the dialog, and puts focus on
   * the disable safebrowsing button.
   */
  private onDisableSafebrowsingDialogClose_() {
    const confirmed =
        this.shadowRoot!.querySelector('settings-disable-safebrowsing-dialog')!
            .wasConfirmed();
    this.recordInteractionHistogramOnSafeBrowsingDialogClose_(confirmed);
    this.recordActionOnSafeBrowsingDialogClose_(confirmed);
    // Check if the dialog was confirmed before closing it.
    if (confirmed) {
      this.$.safeBrowsingRadioGroup.sendPrefChange();
      this.updateCollapsedButtons_();
    } else {
      this.$.safeBrowsingRadioGroup.resetToPrefValue();
    }

    this.showDisableSafebrowsingDialog_ = false;

    // Set focus back to the no protection button regardless of user interaction
    // with the dialog, as it was the entry point to the dialog.
    focusWithoutInk(assert(this.$.safeBrowsingDisabled));
  }

  private onEnhancedProtectionExpandButtonClicked_() {
    this.recordInteractionHistogramOnExpandButtonClicked_(
        SafeBrowsingSetting.ENHANCED);
    this.recordActionOnExpandButtonClicked_(SafeBrowsingSetting.ENHANCED);
  }

  private onStandardProtectionExpandButtonClicked_() {
    this.recordInteractionHistogramOnExpandButtonClicked_(
        SafeBrowsingSetting.STANDARD);
    this.recordActionOnExpandButtonClicked_(SafeBrowsingSetting.STANDARD);
  }

  // <if expr="chromeos or lacros">
  private onOpenChromeOSSecureDnsSettingsClicked_() {
    const path =
        loadTimeData.getString('chromeOSPrivacyAndSecuritySectionPath');
    OpenWindowProxyImpl.getInstance().openURL(`chrome://os-settings/${path}`);
  }
  // </if>

  private recordInteractionHistogramOnRadioChange_(safeBrowsingSetting:
                                                       SafeBrowsingSetting) {
    let action;
    if (safeBrowsingSetting === SafeBrowsingSetting.ENHANCED) {
      action =
          SafeBrowsingInteractions.SAFE_BROWSING_ENHANCED_PROTECTION_CLICKED;
    } else if (safeBrowsingSetting === SafeBrowsingSetting.STANDARD) {
      action =
          SafeBrowsingInteractions.SAFE_BROWSING_STANDARD_PROTECTION_CLICKED;
    } else {
      action =
          SafeBrowsingInteractions.SAFE_BROWSING_DISABLE_SAFE_BROWSING_CLICKED;
    }
    this.metricsBrowserProxy_.recordSafeBrowsingInteractionHistogram(action);
  }

  private recordInteractionHistogramOnExpandButtonClicked_(
      safeBrowsingSetting: SafeBrowsingSetting) {
    this.metricsBrowserProxy_.recordSafeBrowsingInteractionHistogram(
        safeBrowsingSetting === SafeBrowsingSetting.ENHANCED ?
            SafeBrowsingInteractions
                .SAFE_BROWSING_ENHANCED_PROTECTION_EXPAND_ARROW_CLICKED :
            SafeBrowsingInteractions
                .SAFE_BROWSING_STANDARD_PROTECTION_EXPAND_ARROW_CLICKED);
  }

  private recordInteractionHistogramOnSafeBrowsingDialogClose_(confirmed:
                                                                   boolean) {
    this.metricsBrowserProxy_.recordSafeBrowsingInteractionHistogram(
        confirmed ? SafeBrowsingInteractions
                        .SAFE_BROWSING_DISABLE_SAFE_BROWSING_DIALOG_CONFIRMED :
                    SafeBrowsingInteractions
                        .SAFE_BROWSING_DISABLE_SAFE_BROWSING_DIALOG_DENIED);
  }

  private recordActionOnRadioChange_(safeBrowsingSetting: SafeBrowsingSetting) {
    let actionName;
    if (safeBrowsingSetting === SafeBrowsingSetting.ENHANCED) {
      actionName = 'SafeBrowsing.Settings.EnhancedProtectionClicked';
    } else if (safeBrowsingSetting === SafeBrowsingSetting.STANDARD) {
      actionName = 'SafeBrowsing.Settings.StandardProtectionClicked';
    } else {
      actionName = 'SafeBrowsing.Settings.DisableSafeBrowsingClicked';
    }
    this.metricsBrowserProxy_.recordAction(actionName);
  }

  private recordActionOnExpandButtonClicked_(safeBrowsingSetting:
                                                 SafeBrowsingSetting) {
    this.metricsBrowserProxy_.recordAction(
        safeBrowsingSetting === SafeBrowsingSetting.ENHANCED ?
            'SafeBrowsing.Settings.EnhancedProtectionExpandArrowClicked' :
            'SafeBrowsing.Settings.StandardProtectionExpandArrowClicked');
  }

  private recordActionOnSafeBrowsingDialogClose_(confirmed: boolean) {
    this.metricsBrowserProxy_.recordAction(
        confirmed ? 'SafeBrowsing.Settings.DisableSafeBrowsingDialogConfirmed' :
                    'SafeBrowsing.Settings.DisableSafeBrowsingDialogDenied');
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-security-page': SettingsSecurityPageElement;
  }
}

customElements.define(
    SettingsSecurityPageElement.is, SettingsSecurityPageElement);

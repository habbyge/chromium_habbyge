// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Polymer element for displaying material design marketing
 * opt-in screen.
 */

/* #js_imports_placeholder */

/**
 * @constructor
 * @extends {PolymerElement}
 * @implements {LoginScreenBehaviorInterface}
 */
const MarketingScreenElementBase = Polymer.mixinBehaviors(
  [OobeI18nBehavior, OobeDialogHostBehavior, LoginScreenBehavior],
  Polymer.Element);

/**
 * @typedef {{
 *   marketingOptInOverviewDialog:  OobeAdaptiveDialogElement,
 *   chromebookUpdatesOption:  CrToggleElement,
 *   a11yNavButtonToggle:  OobeA11yOption,
 * }}
 */
MarketingScreenElementBase.$;

class MarketingOptIn extends MarketingScreenElementBase {

  static get is() { return 'marketing-opt-in-element'; }

  /* #html_template_placeholder */

  static get properties() {
    return {
      isAccessibilitySettingsShown_: {
        type: Boolean,
      },

      /**
       * Whether the accessibility button is shown. This button is only shown
       * if the gesture EDU screen was shown before the marketing screen.
       */
      isA11ySettingsButtonVisible_: {
        type: Boolean,
      },

      /**
       * Whether the marketing opt in toggles should be shown, which will be the
       * case only if marketing opt in feature is enabled AND if the current user
       * is a non-managed user.
       * When this is false, the screen will only contain UI related to the
       * tablet mode gestural navigation settings.
       */
      marketingOptInVisible_: {
        type: Boolean,
      },

      /**
       * Whether a verbose footer will be shown to the user containing some legal
       * information such as the Google address. Currently shown for Canada only.
       */
      hasLegalFooter_: {
        type: Boolean,
      },
    };
  }

  constructor() {
    super();
    this.isAccessibilitySettingsShown_ = false;
    this.marketingOptInVisible_ = false;
    this.hasLegalFooter_ = false;
    this.isA11ySettingsButtonVisible_ = false;
  }


  /** Overridden from LoginScreenBehavior. */
  // clang-format off
  get EXTERNAL_API() {
    return ['updateA11ySettingsButtonVisibility',
            'updateA11yNavigationButtonToggle'];
  }

  // clang-format on

  /** @override */
  ready() {
    super.ready();
    this.initializeLoginScreen('MarketingOptInScreen', {resetAllowed: true});
  }

  /** Shortcut method to control animation */
  setAnimationPlay_(played) {
      this.$.animation.setPlay(played);
  }

  onBeforeShow(data) {
    this.marketingOptInVisible_ =
        'optInVisibility' in data && data.optInVisibility;
    this.$.chromebookUpdatesOption.checked =
        'optInDefaultState' in data && data.optInDefaultState;
    this.hasLegalFooter_ =
        'legalFooterVisibility' in data && data.legalFooterVisibility;
    this.isAccessibilitySettingsShown_ = false;
    this.setAnimationPlay_(true);
    this.$.marketingOptInOverviewDialog.show();
  }

  get defaultControl() {
    return this.$.marketingOptInOverviewDialog;
  }

  /**
   * This is 'on-tap' event handler for 'AcceptAndContinue/Next' buttons.
   * @private
   */
  onGetStarted_() {
    this.setAnimationPlay_(false);
    chrome.send(
        'login.MarketingOptInScreen.onGetStarted',
        [this.$.chromebookUpdatesOption.checked]);
  }

  /**
   * @param {boolean} shown Whether the A11y Settings button should be shown.
   */
  updateA11ySettingsButtonVisibility(shown) {
    this.isA11ySettingsButtonVisible_ = shown;
  }

  /**
   * @param {boolean} enabled Whether the a11y setting for shownig shelf
   * navigation buttons is enabled.
   */
  updateA11yNavigationButtonToggle(enabled) {
    this.$.a11yNavButtonToggle.checked = enabled;
  }

  /**
   * This is the 'on-tap' event handler for the accessibility settings link and
   * for the back button on the accessibility page.
   * @private
   */
  onToggleAccessibilityPage_() {
    this.isAccessibilitySettingsShown_ = !this.isAccessibilitySettingsShown_;
    this.setAnimationPlay_(!this.isAccessibilitySettingsShown_);
  }

  /**
   * The 'on-change' event handler for when the a11y navigation button setting
   * is toggled on or off.
   * @private
   */
  onA11yNavButtonsSettingChanged_() {
    chrome.send('login.MarketingOptInScreen.setA11yNavigationButtonsEnabled', [
      this.$.a11yNavButtonToggle.checked
    ]);
  }
}

customElements.define(MarketingOptIn.is, MarketingOptIn);
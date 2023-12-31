// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_button/cr_button.m.js';
import './firmware_shared_css.js';
import './firmware_shared_fonts.js';

import {html, PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import {FirmwareUpdate, UpdatePriority} from './firmware_update_types.js';

/**
 * @fileoverview
 * 'update-card' displays information about a peripheral update.
 */
export class UpdateCardElement extends PolymerElement {
  static get is() {
    return 'update-card';
  }

  static get template() {
    return html`{__html_template__}`;
  }

  static get properties() {
    return {
      /** @type {!FirmwareUpdate} */
      update: {
        type: Object,
      },
    };
  }

  /**
   * @protected
   * @return {boolean}
   */
  isCriticalUpdate_() {
    return this.update.priority === UpdatePriority.kCritical;
  }

  /** @protected */
  onUpdateButtonClicked_() {
    const eventName = this.update.updateModeInstructions ?
        'open-device-prep-dialog' :
        'open-update-dialog';
    this.dispatchEvent(new CustomEvent(
        eventName,
        {bubbles: true, composed: true, detail: {update: this.update}}));
  }
}

customElements.define(UpdateCardElement.is, UpdateCardElement);

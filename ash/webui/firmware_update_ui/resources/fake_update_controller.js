// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {FakeObservables} from 'chrome://resources/ash/common/fake_observables.js';

import {fakeFirmwareUpdates, fakeInstallationProgress} from './fake_data.js';
import {InstallationProgress, UpdateControllerInterface, UpdateProgressObserver, UpdateProviderInterface} from './firmware_update_types.js';
import {getUpdateProvider} from './mojo_interface_provider.js';

// Method names.
export const ON_PROGRESS_CHANGED = 'UpdateProgressObserver_onProgressChanged';

/**
 * @fileoverview
 * Implements a fake version of the UpdateController mojo interface.
 */

/** @implements {UpdateControllerInterface} */
export class FakeUpdateController {
  constructor() {
    this.observables_ = new FakeObservables();

    /** @private {UpdateProviderInterface} */
    this.fakeUpdateProvider_ = getUpdateProvider();

    /** @private {!Set<string>} */
    this.completedFirmwareUpdates_ = new Set();

    /** @private {?Promise} */
    this.startUpdatePromise_ = null;

    /** @private {string} */
    this.deviceId_ = '';

    /** @private {boolean} */
    this.isUpdateInProgress_ = false;

    /** @private {number} */
    this.updateIntervalInMs_ = 1000;

    this.registerObservables();
  }

  /*
   * Implements UpdateControllerInterface.startUpdate.
   * @param {string} deviceId
   * @param {!UpdateProgressObserver} remote
   * @return {!Promise}
   */
  startUpdate(deviceId, remote) {
    this.deviceId_ = deviceId;
    this.isUpdateInProgress_ = true;
    this.startUpdatePromise_ = this.observeWithArg_(
        ON_PROGRESS_CHANGED, deviceId, (installationProgress) => {
          remote.onProgressChanged(installationProgress);
          if (installationProgress.percentage === 100) {
            this.isUpdateInProgress_ = false;
            this.completedFirmwareUpdates_.add(this.deviceId_);
            this.updateDeviceList_();
            this.observables_.stopTriggerOnIntervalWithArg(
                ON_PROGRESS_CHANGED, this.deviceId_);
          }
        });
    this.startUpdatePromise_.then(() => this.triggerProgressChangedObserver());
  }

  /**
   * Sets the values that will be observed from observePeripheralUpdates.
   * @param {string} deviceId
   * @param {!Array<!InstallationProgress>} installationProgress
   */
  setFakeInstallationProgress(deviceId, installationProgress) {
    this.observables_.setObservableDataForArg(
        ON_PROGRESS_CHANGED, deviceId, installationProgress);
  }

  /**
   * Returns the promise for the most recent startUpdate observation.
   * @return {?Promise}
   */
  getStartUpdatePromiseForTesting() {
    return this.startUpdatePromise_;
  }

  /**
   * Causes the progress changed observer to fire.
   */
  triggerProgressChangedObserver() {
    this.observables_.startTriggerOnIntervalWithArg(
        ON_PROGRESS_CHANGED, this.deviceId_, this.updateIntervalInMs_);
  }

  registerObservables() {
    this.observables_.registerObservableWithArg(ON_PROGRESS_CHANGED);
    // Set up fake installation progress data for each fake firmware update.
    fakeFirmwareUpdates.flat().forEach(({deviceId}) => {
      this.setFakeInstallationProgress(deviceId, fakeInstallationProgress);
    });
  }

  /**
   * Disables all observers and resets controller to its initial state.
   */
  reset() {
    this.stopTriggerIntervals();
    this.observables_ = new FakeObservables();
    this.completedFirmwareUpdates_.clear();
    this.deviceId_ = '';
    this.isUpdateInProgress_ = false;
    this.registerObservables();
  }

  /**
   * Stop automatically triggering observables.
   */
  stopTriggerIntervals() {
    this.observables_.stopAllTriggerIntervals();
  }

  /*
   * Sets up an observer for a methodName that takes an additional arg.
   * @template T
   * @param {string} methodName
   * @param {string} arg
   * @param {!function(!T)} callback
   * @return {!Promise}
   * @private
   */
  observeWithArg_(methodName, arg, callback) {
    return new Promise((resolve) => {
      this.observables_.observeWithArg(methodName, arg, callback);
      resolve();
    });
  }

  /** @return {boolean} */
  isUpdateInProgress() {
    return this.isUpdateInProgress_;
  }

  /**
   * @param {number} intervalMs
   */
  setUpdateIntervalInMs(intervalMs) {
    this.updateIntervalInMs_ = intervalMs;
  }

  /**
   * Remove the completed firmware update and trigger the list observer.
   *  @private
   */
  updateDeviceList_() {
    const updatedFakeFirmwareUpdates = fakeFirmwareUpdates.flat().filter(
        u => !this.completedFirmwareUpdates_.has(u.deviceId));
    this.fakeUpdateProvider_.setFakeFirmwareUpdates(
        [updatedFakeFirmwareUpdates]);
    this.fakeUpdateProvider_.triggerDeviceAddedObserver();
  }
}

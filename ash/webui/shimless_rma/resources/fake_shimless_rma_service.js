// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {FakeMethodResolver} from 'chrome://resources/ash/common/fake_method_resolver.js';
import {FakeObservables} from 'chrome://resources/ash/common/fake_observables.js';
import {assert} from 'chrome://resources/js/assert.m.js';

import {CalibrationComponentStatus, CalibrationObserverRemote, CalibrationOverallStatus, CalibrationSetupInstruction, CalibrationStatus, Component, ComponentType, ErrorObserverRemote, FinalizationObserverRemote, FinalizationStatus, HardwareVerificationStatusObserverRemote, HardwareWriteProtectionStateObserverRemote, OsUpdateObserverRemote, OsUpdateOperation, PowerCableStateObserverRemote, ProvisioningObserverRemote, ProvisioningStatus, QrCode, RmadErrorCode, RmaState, ShimlessRmaServiceInterface, StateResult, WriteProtectDisableCompleteState} from './shimless_rma_types.js';

/** @implements {ShimlessRmaServiceInterface} */
export class FakeShimlessRmaService {
  constructor() {
    this.methods_ = new FakeMethodResolver();
    this.observables_ = new FakeObservables();

    /**
     * The list of states for this RMA flow.
     * @private {!Array<!StateResult>}
     */
    this.states_ = [];

    /**
     * The index into states_ for the current fake state.
     * @private {number}
     */
    this.stateIndex_ = 0;

    /**
     * The list of components.
     * @private {!Array<!Component>}
     */
    this.components_ = [];

    /**
     * Control automatically triggering a HWWP disable observation.
     * @private {boolean}
     */
    this.automaticallyTriggerDisableWriteProtectionObservation_ = false;

    /**
     * Control automatically triggering provisioning observations.
     * @private {boolean}
     */
    this.automaticallyTriggerProvisioningObservation_ = false;

    /**
     * Control automatically triggering calibration observations.
     * @private {boolean}
     */
    this.automaticallyTriggerCalibrationObservation_ = false;

    /**
     * Control automatically triggering OS update observations.
     * @private {boolean}
     */
    this.automaticallyTriggerOsUpdateObservation_ = false;

    /**
     * Control automatically triggering a hardware verification observation.
     * @private {boolean}
     */
    this.automaticallyTriggerHardwareVerificationStatusObservation_ = false;

    /**
     * Control automatically triggering a finalization observation.
     * @private {boolean}
     */
    this.automaticallyTriggerFinalizationObservation_ = false;

    /**
     * The fake result of calling UpdatesOs, used to determine if fake
     * observations should be triggered.
     * @private {boolean}
     */
    this.osCanUpdate_ = false;

    /**
     * Both abortRma and forward state transitions can have significant delays
     * that are useful to fake for manual testing.
     * Defaults to no delay for unit tests.
     * @private {number}
     */
    this.resolveMethodDelayMs_ = 0;

    this.reset();
  }

  /** @param {number} delayMs */
  setAsyncOperationDelayMs(delayMs) {
    this.resolveMethodDelayMs_ = delayMs;
  }

  /**
   * Set the ordered list of states end error codes for this fake.
   * Setting an empty list (the default) returns kRmaNotRequired for any state
   * function.
   * Next state functions and transitionPreviousState will move through the fake
   * state through the list, and return kTransitionFailed if it would move off
   * either end. getCurrentState always return the state at the current index.
   *
   * @param {!Array<!StateResult>} states
   */
  setStates(states) {
    this.states_ = states;
    this.stateIndex_ = 0;
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  getCurrentState() {
    // As next state functions and transitionPreviousState can modify the result
    // of this function the result must be set at the time of the call.
    if (this.states_.length === 0) {
      this.setFakeCurrentState_(
          RmaState.kUnknown, false, false, RmadErrorCode.kRmaNotRequired);
    } else {
      // It should not be possible for stateIndex_ to be out of range unless
      // there is a bug in the fake.
      assert(this.stateIndex_ < this.states_.length);
      const state = this.states_[this.stateIndex_];
      this.setFakeCurrentState_(
          state.state, state.canCancel, state.canGoBack, state.error);
    }
    return this.methods_.resolveMethodWithDelay(
        'getCurrentState', this.resolveMethodDelayMs_);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  transitionPreviousState() {
    // As next state methods and transitionPreviousState can modify the result
    // of this function the result must be set at the time of the call.
    if (this.states_.length === 0) {
      this.setFakePrevState_(
          RmaState.kUnknown, false, false, RmadErrorCode.kRmaNotRequired);
    } else if (this.stateIndex_ === 0) {
      // It should not be possible for stateIndex_ to be out of range unless
      // there is a bug in the fake.
      assert(this.stateIndex_ < this.states_.length);
      const state = this.states_[this.stateIndex_];
      this.setFakePrevState_(
          state.state, state.canCancel, state.canGoBack,
          RmadErrorCode.kTransitionFailed);
    } else {
      this.stateIndex_--;
      const state = this.states_[this.stateIndex_];
      this.setFakePrevState_(
          state.state, state.canCancel, state.canGoBack, state.error);
    }
    return this.methods_.resolveMethod('transitionPreviousState');
  }

  /**
   * @return {!Promise<!{error: !RmadErrorCode}>}
   */
  abortRma() {
    return this.methods_.resolveMethodWithDelay(
        'abortRma', this.resolveMethodDelayMs_);
  }

  /**
   * Sets the value that will be returned when calling abortRma().
   * @param {!RmadErrorCode} error
   */
  setAbortRmaResult(error) {
    this.methods_.setResult('abortRma', {error: error});
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  beginFinalization() {
    return this.getNextStateForMethod_(
        'beginFinalization', RmaState.kWelcomeScreen);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  networkSelectionComplete() {
    return this.getNextStateForMethod_(
        'networkSelectionComplete', RmaState.kConfigureNetwork);
  }

  /**
   * @return {!Promise<!{version: string}>}
   */
  getCurrentOsVersion() {
    return this.methods_.resolveMethod('getCurrentOsVersion');
  }

  /**
   * @param {string} version
   */
  setGetCurrentOsVersionResult(version) {
    this.methods_.setResult('getCurrentOsVersion', {version: version});
  }

  /**
   * @return {!Promise<!{updateAvailable: boolean, version: string}>}
   */
  checkForOsUpdates() {
    return this.methods_.resolveMethod('checkForOsUpdates');
  }

  /**
   * @param {boolean} available
   * @param {string} version
   */
  setCheckForOsUpdatesResult(available, version) {
    this.methods_.setResult(
        'checkForOsUpdates', {updateAvailable: available, version: version});
  }

  /**
   * @return {!Promise<!{updateStarted: boolean}>}
   */
  updateOs() {
    if (this.osCanUpdate_ && this.automaticallyTriggerOsUpdateObservation_) {
      this.triggerOsUpdateObserver(
          OsUpdateOperation.kCheckingForUpdate, 0.1, 500);
      this.triggerOsUpdateObserver(
          OsUpdateOperation.kUpdateAvailable, 0.3, 1000);
      this.triggerOsUpdateObserver(OsUpdateOperation.kDownloading, 0.5, 1500);
      this.triggerOsUpdateObserver(OsUpdateOperation.kVerifying, 0.7, 2000);
      this.triggerOsUpdateObserver(OsUpdateOperation.kFinalizing, 0.9, 2500);
      this.triggerOsUpdateObserver(
          OsUpdateOperation.kUpdatedNeedReboot, 1.0, 3000);
    }
    return this.methods_.resolveMethod('updateOs');
  }

  /**
   * @param {boolean} started
   */
  setUpdateOsResult(started) {
    this.osCanUpdate_ = started;
    this.methods_.setResult('updateOs', {updateStarted: started});
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  updateOsSkipped() {
    return this.getNextStateForMethod_('updateOsSkipped', RmaState.kUpdateOs);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  setSameOwner() {
    return this.getNextStateForMethod_(
        'setSameOwner', RmaState.kChooseDestination);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  setDifferentOwner() {
    return this.getNextStateForMethod_(
        'setDifferentOwner', RmaState.kChooseDestination);
  }

  /**
   * @return {!Promise<!{available: boolean}>}
   */
  manualDisableWriteProtectAvailable() {
    return this.methods_.resolveMethod('manualDisableWriteProtectAvailable');
  }

  /**
   * @param {boolean} available
   */
  setManualDisableWriteProtectAvailableResult(available) {
    this.methods_.setResult(
        'manualDisableWriteProtectAvailable', {available: available});
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  chooseManuallyDisableWriteProtect() {
    return this.getNextStateForMethod_(
        'chooseManuallyDisableWriteProtect',
        RmaState.kChooseWriteProtectDisableMethod);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  chooseRsuDisableWriteProtect() {
    return this.getNextStateForMethod_(
        'chooseRsuDisableWriteProtect',
        RmaState.kChooseWriteProtectDisableMethod);
  }

  /**
   * @return {!Promise<!{challenge: string}>}
   */
  getRsuDisableWriteProtectChallenge() {
    return this.methods_.resolveMethod('getRsuDisableWriteProtectChallenge');
  }

  /**
   * @param {string} challenge
   */
  setGetRsuDisableWriteProtectChallengeResult(challenge) {
    this.methods_.setResult(
        'getRsuDisableWriteProtectChallenge', {challenge: challenge});
  }

  /**
   * @return {!Promise<!{hwid: string}>}
   */
   getRsuDisableWriteProtectHwid() {
    return this.methods_.resolveMethod('getRsuDisableWriteProtectHwid');
  }

  /**
   * @param {string} hwid
   */
  setGetRsuDisableWriteProtectHwidResult(hwid) {
    this.methods_.setResult(
        'getRsuDisableWriteProtectHwid', {hwid: hwid});
  }

  /**
   * @return {!Promise<!{qrCode: QrCode}>}
   */
  getRsuDisableWriteProtectChallengeQrCode() {
    return this.methods_.resolveMethod(
        'getRsuDisableWriteProtectChallengeQrCode');
  }

  /**
   * @param {!QrCode} qrCode
   */
  setGetRsuDisableWriteProtectChallengeQrCodeResponse(qrCode) {
    this.methods_.setResult(
        'getRsuDisableWriteProtectChallengeQrCode', {qrCode: qrCode});
  }

  /**
   * @param {string} code
   * @return {!Promise<!StateResult>}
   */
  setRsuDisableWriteProtectCode(code) {
    return this.getNextStateForMethod_(
        'setRsuDisableWriteProtectCode', RmaState.kEnterRSUWPDisableCode);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  writeProtectManuallyDisabled() {
    return this.getNextStateForMethod_(
        'writeProtectManuallyDisabled', RmaState.kWaitForManualWPDisable);
  }

  /**
   * @return {!Promise<!{displayUrl: string, qrCode: ?QrCode}>}
   */
  getWriteProtectManuallyDisabledInstructions() {
    return this.methods_.resolveMethod(
        'getWriteProtectManuallyDisabledInstructions');
  }

  /**
   * @param {string} displayUrl
   * @param {!QrCode} qrCode
   */
  setGetWriteProtectManuallyDisabledInstructionsResult(displayUrl, qrCode) {
    this.methods_.setResult(
        'getWriteProtectManuallyDisabledInstructions',
        {displayUrl: displayUrl, qrCode: qrCode});
  }

  /** @return {!Promise<!{state: !WriteProtectDisableCompleteState}>} */
  getWriteProtectDisableCompleteState() {
    return this.methods_.resolveMethod('getWriteProtectDisableCompleteState');
  }

  /** @param {!WriteProtectDisableCompleteState} state */
  setGetWriteProtectDisableCompleteState(state) {
    this.methods_.setResult(
        'getWriteProtectDisableCompleteState', {state: state});
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  confirmManualWpDisableComplete() {
    return this.getNextStateForMethod_(
        'confirmManualWpDisableComplete', RmaState.kWPDisableComplete);
  }

  /**
   * @return {!Promise<!{components: !Array<!Component>}>}
   */
  getComponentList() {
    this.methods_.setResult('getComponentList', {components: this.components_});
    return this.methods_.resolveMethod('getComponentList');
  }

  /**
   * @param {!Array<!Component>} components
   */
  setGetComponentListResult(components) {
    this.components_ = components;
  }

  /**
   * @param {!Array<!Component>} components
   * @return {!Promise<!StateResult>}
   */
  setComponentList(components) {
    return this.getNextStateForMethod_(
        'setComponentList', RmaState.kSelectComponents);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  reworkMainboard() {
    return this.getNextStateForMethod_(
        'reworkMainboard', RmaState.kSelectComponents);
  }

  /**
   * @return {!Promise<!{required: boolean}>}
   */
  reimageRequired() {
    return this.methods_.resolveMethod('reimageRequired');
  }

  /**
   * @param {boolean} required
   */
  setReimageRequiredResult(required) {
    this.methods_.setResult('reimageRequired', {required: required});
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  reimageSkipped() {
    return this.getNextStateForMethod_(
        'reimageSkipped', RmaState.kChooseFirmwareReimageMethod);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  reimageFromDownload() {
    return this.getNextStateForMethod_(
        'reimageFromDownload', RmaState.kChooseFirmwareReimageMethod);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  reimageFromUsb() {
    return this.getNextStateForMethod_(
        'reimageFromUsb', RmaState.kChooseFirmwareReimageMethod);
  }

  /**
   * @return {!Promise<!StateResult>}
   *
   */
  shutdownForRestock() {
    return this.getNextStateForMethod_('shutdownForRestock', RmaState.kRestock);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  continueFinalizationAfterRestock() {
    return this.getNextStateForMethod_(
        'continueFinalizationAfterRestock', RmaState.kRestock);
  }

  /**
   * @return {!Promise<!{regions: !Array<string>}>}
   */
  getRegionList() {
    return this.methods_.resolveMethod('getRegionList');
  }

  /**
   * @param {!Array<string>} regions
   */
  setGetRegionListResult(regions) {
    this.methods_.setResult('getRegionList', {regions: regions});
  }

  /**
   * @return {!Promise<!{skus: !Array<string>}>}
   */
  getSkuList() {
    return this.methods_.resolveMethod('getSkuList');
  }

  /**
   * @param {!Array<string>} skus
   */
  setGetSkuListResult(skus) {
    this.methods_.setResult('getSkuList', {skus: skus});
  }

  /**
   * @return {!Promise<!{serialNumber: string}>}
   */
  getOriginalSerialNumber() {
    return this.methods_.resolveMethod('getOriginalSerialNumber');
  }

  /**
   * @param {string} serialNumber
   */
  setGetOriginalSerialNumberResult(serialNumber) {
    this.methods_.setResult(
        'getOriginalSerialNumber', {serialNumber: serialNumber});
  }

  /**
   * @return {!Promise<!{regionIndex: number}>}
   */
  getOriginalRegion() {
    return this.methods_.resolveMethod('getOriginalRegion');
  }

  /**
   * @param {number} regionIndex
   */
  setGetOriginalRegionResult(regionIndex) {
    this.methods_.setResult('getOriginalRegion', {regionIndex: regionIndex});
  }

  /**
   * @return {!Promise<!{skuIndex: number}>}
   */
  getOriginalSku() {
    return this.methods_.resolveMethod('getOriginalSku');
  }

  /**
   * @param {number} skuIndex
   */
  setGetOriginalSkuResult(skuIndex) {
    this.methods_.setResult('getOriginalSku', {skuIndex: skuIndex});
  }

  /**
   * @param {string} serialNumber
   * @param {number} regionIndex
   * @param {number} skuIndex
   * @return {!Promise<!StateResult>}
   */
  setDeviceInformation(serialNumber, regionIndex, skuIndex) {
    // TODO(gavindodd): Validate range of region and sku.
    return this.getNextStateForMethod_(
        'setDeviceInformation', RmaState.kUpdateDeviceInformation);
  }

  /**
   * @return {!Promise<!{components: !Array<!CalibrationComponentStatus>}>}
   */
  getCalibrationComponentList() {
    return this.methods_.resolveMethod('getCalibrationComponentList');
  }

  /**
   * @param {!Array<!CalibrationComponentStatus>} components
   */
  setGetCalibrationComponentListResult(components) {
    this.methods_.setResult(
        'getCalibrationComponentList', {components: components});
  }

  /**
   * @return {!Promise<!{instructions: CalibrationSetupInstruction}>}
   */
  getCalibrationSetupInstructions() {
    return this.methods_.resolveMethod('getCalibrationSetupInstructions');
  }

  /**
   * @param {CalibrationSetupInstruction} instructions
   */
  setGetCalibrationSetupInstructionsResult(instructions) {
    this.methods_.setResult(
        'getCalibrationSetupInstructions', {instructions: instructions});
  }

  /**
   * The fake does not use the status list parameter, the fake data is never
   * updated.
   * @param {!Array<!CalibrationComponentStatus>} unused
   * @return {!Promise<!StateResult>}
   */
  startCalibration(unused) {
    return this.getNextStateForMethod_(
        'startCalibration', RmaState.kCheckCalibration);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  runCalibrationStep() {
    return this.getNextStateForMethod_(
        'runCalibrationStep', RmaState.kSetupCalibration);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  continueCalibration() {
    return this.getNextStateForMethod_(
        'continueCalibration', RmaState.kRunCalibration);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  calibrationComplete() {
    return this.getNextStateForMethod_(
        'calibrationComplete', RmaState.kRunCalibration);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  provisioningComplete() {
    return this.getNextStateForMethod_(
        'provisioningComplete', RmaState.kProvisionDevice);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  finalizationComplete() {
    return this.getNextStateForMethod_(
        'finalizationComplete', RmaState.kFinalize);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  writeProtectManuallyEnabled() {
    return this.getNextStateForMethod_(
        'writeProtectManuallyEnabled', RmaState.kWaitForManualWPEnable);
  }

  /** @return {!Promise<{log: string}>} */
  getLog() {
    return this.methods_.resolveMethod('getLog');
  }

  /** @param {string} log */
  setGetLogResult(log) {
    this.methods_.setResult('getLog', {log: log});
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  endRmaAndReboot() {
    return this.getNextStateForMethod_(
        'endRmaAndReboot', RmaState.kRepairComplete);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  endRmaAndShutdown() {
    return this.getNextStateForMethod_(
        'endRmaAndShutdown', RmaState.kRepairComplete);
  }

  /**
   * @return {!Promise<!StateResult>}
   */
  endRmaAndCutoffBattery() {
    return this.getNextStateForMethod_(
        'endRmaAndCutoffBattery', RmaState.kRepairComplete);
  }

  /**
   * Implements ShimlessRmaServiceInterface.ObserveError.
   * @param {!ErrorObserverRemote} remote
   */
  observeError(remote) {
    this.observables_.observe('ErrorObserver_onError', (error) => {
      remote.onError(
          /** @type {!RmadErrorCode} */ (error));
    });
  }

  /**
   * Implements ShimlessRmaServiceInterface.ObserveOsUpdate.
   * @param {!OsUpdateObserverRemote} remote
   */
  observeOsUpdateProgress(remote) {
    this.observables_.observe(
        'OsUpdateObserver_onOsUpdateProgressUpdated', (operation, progress) => {
          remote.onOsUpdateProgressUpdated(
              /** @type {!OsUpdateOperation} */ (operation),
              /** @type {number} */ (progress));
        });
  }

  /**
   * Implements ShimlessRmaServiceInterface.ObserveCalibration.
   * @param {!CalibrationObserverRemote} remote
   */
  observeCalibrationProgress(remote) {
    this.observables_.observe(
        'CalibrationObserver_onCalibrationUpdated', (componentStatus) => {
          remote.onCalibrationUpdated(
              /** @type {!CalibrationComponentStatus} */ (componentStatus));
        });
    this.observables_.observe(
        'CalibrationObserver_onCalibrationStepComplete', (status) => {
          remote.onCalibrationStepComplete(
              /** @type {!CalibrationOverallStatus} */ (status));
        });
    if (this.automaticallyTriggerCalibrationObservation_) {
      this.triggerCalibrationObserver(
          {
            component: ComponentType.kBaseAccelerometer,
            status: CalibrationStatus.kCalibrationWaiting,
            progress: 0.0
          },
          1000);
      this.triggerCalibrationObserver(
          {
            component: ComponentType.kBaseAccelerometer,
            status: CalibrationStatus.kCalibrationInProgress,
            progress: 0.2
          },
          2000);
      this.triggerCalibrationObserver(
          {
            component: ComponentType.kBaseAccelerometer,
            status: CalibrationStatus.kCalibrationInProgress,
            progress: 0.4
          },
          3000);
      this.triggerCalibrationObserver(
          {
            component: ComponentType.kBaseAccelerometer,
            status: CalibrationStatus.kCalibrationInProgress,
            progress: 0.6
          },
          4000);
      this.triggerCalibrationObserver(
          {
            component: ComponentType.kBaseAccelerometer,
            status: CalibrationStatus.kCalibrationInProgress,
            progress: 0.8
          },
          5000);
      this.triggerCalibrationObserver(
          {
            component: ComponentType.kLidAccelerometer,
            status: CalibrationStatus.kCalibrationWaiting,
            progress: 0.0
          },
          6000);
      this.triggerCalibrationObserver(
          {
            component: ComponentType.kBaseAccelerometer,
            status: CalibrationStatus.kCalibrationComplete,
            progress: 0.5
          },
          7000);
      this.triggerCalibrationObserver(
          {
            component: ComponentType.kBaseAccelerometer,
            status: CalibrationStatus.kCalibrationFailed,
            progress: 1.0
          },
          8000);
      this.triggerCalibrationObserver(
          {
            component: ComponentType.kBaseGyroscope,
            status: CalibrationStatus.kCalibrationSkip,
            progress: 1.0
          },
          9000);
      this.triggerCalibrationOverallObserver(
          CalibrationOverallStatus.kCalibrationOverallCurrentRoundComplete,
          10000);
    }
  }

  /**
   * Implements ShimlessRmaServiceInterface.ObserveProvisioning.
   * @param {!ProvisioningObserverRemote} remote
   */
  observeProvisioningProgress(remote) {
    this.observables_.observe(
        'ProvisioningObserver_onProvisioningUpdated', (status, progress) => {
          remote.onProvisioningUpdated(
              /** @type {!ProvisioningStatus} */ (status),
              /** @type {number} */ (progress));
        });
    if (this.automaticallyTriggerProvisioningObservation_) {
      // Fake progress over 4 seconds.
      this.triggerProvisioningObserver(
          ProvisioningStatus.kInProgress, 0.25, 1000);
      this.triggerProvisioningObserver(
          ProvisioningStatus.kInProgress, 0.5, 2000);
      this.triggerProvisioningObserver(
          ProvisioningStatus.kInProgress, 0.75, 3000);
      this.triggerProvisioningObserver(ProvisioningStatus.kComplete, 1.0, 4000);
    }
  }

  /**
   * Trigger provisioning observations when an observer is added.
   */
  automaticallyTriggerProvisioningObservation() {
    this.automaticallyTriggerProvisioningObservation_ = true;
  }

  /**
   * Trigger calibration observations when an observer is added.
   */
  automaticallyTriggerCalibrationObservation() {
    this.automaticallyTriggerCalibrationObservation_ = true;
  }

  /**
   * Trigger OS update observations when an OS update is started.
   */
  automaticallyTriggerOsUpdateObservation() {
    this.automaticallyTriggerOsUpdateObservation_ = true;
  }

  /**
   * Implements ShimlessRmaServiceInterface.ObserveHardwareWriteProtectionState.
   * @param {!HardwareWriteProtectionStateObserverRemote} remote
   */
  observeHardwareWriteProtectionState(remote) {
    this.observables_.observe(
        'HardwareWriteProtectionStateObserver_onHardwareWriteProtectionStateChanged',
        (enabled) => {
          remote.onHardwareWriteProtectionStateChanged(
              /** @type {boolean} */ (enabled));
        });
    if (this.states_ &&
        this.automaticallyTriggerDisableWriteProtectionObservation_) {
      assert(this.stateIndex_ < this.states_.length);
      this.triggerHardwareWriteProtectionObserver(
          this.states_[this.stateIndex_].state ===
              RmaState.kWaitForManualWPEnable,
          3000);
    }
  }

  /**
   * Trigger a disable write protection observation when an observer is added.
   */
  automaticallyTriggerDisableWriteProtectionObservation() {
    this.automaticallyTriggerDisableWriteProtectionObservation_ = true;
  }

  /**
   * Implements ShimlessRmaServiceInterface.ObservePowerCableState.
   * @param {!PowerCableStateObserverRemote} remote
   */
  observePowerCableState(remote) {
    this.observables_.observe(
        'PowerCableStateObserver_onPowerCableStateChanged', (pluggedIn) => {
          remote.onPowerCableStateChanged(/** @type {boolean} */ (pluggedIn));
        });
  }

  /**
   * Implements ShimlessRmaServiceInterface.ObserveHardwareVerificationStatus.
   * @param {!HardwareVerificationStatusObserverRemote} remote
   */
  observeHardwareVerificationStatus(remote) {
    this.observables_.observe(
        'HardwareVerificationStatusObserver_onHardwareVerificationResult',
        (is_compliant, error_message) => {
          remote.onHardwareVerificationResult(
              /** @type {boolean} */ (is_compliant),
              /** @type {string} */ (error_message));
        });
    if (this.automaticallyTriggerHardwareVerificationStatusObservation_) {
      this.triggerHardwareVerificationStatusObserver(true, '', 3000);
    }
  }


  /**
   * Trigger a hardware verification observation when an observer is added.
   */
  automaticallyTriggerHardwareVerificationStatusObservation() {
    this.automaticallyTriggerHardwareVerificationStatusObservation_ = true;
  }

  /**
   * Implements ShimlessRmaServiceInterface.ObserveFinalizationStatus.
   * @param {!FinalizationObserverRemote} remote
   */
  observeFinalizationStatus(remote) {
    this.observables_.observe(
        'FinalizationObserver_onFinalizationUpdated', (status, progress) => {
          remote.onFinalizationUpdated(
              /** @type {!FinalizationStatus} */ (status),
              /** @type {number} */ (progress));
        });
    if (this.automaticallyTriggerFinalizationObservation_) {
      this.triggerFinalizationObserver(
          FinalizationStatus.kInProgress, 0.25, 1000);
      this.triggerFinalizationObserver(
          FinalizationStatus.kInProgress, 0.75, 2000);
      this.triggerFinalizationObserver(FinalizationStatus.kComplete, 1.0, 3000);
    }
  }

  /**
   * Trigger a finalization progress observation when an observer is added.
   */
  automaticallyTriggerFinalizationObservation() {
    this.automaticallyTriggerFinalizationObservation_ = true;
  }

  /**
   * Causes the error observer to fire after a delay.
   * @param {!RmadErrorCode} error
   * @param {number} delayMs
   */
  triggerErrorObserver(error, delayMs) {
    return this.triggerObserverAfterMs('ErrorObserver_onError', error, delayMs);
  }

  /**
   * Causes the OS update observer to fire after a delay.
   * @param {!OsUpdateOperation} operation
   * @param {number} progress
   * @param {number} delayMs
   */
  triggerOsUpdateObserver(operation, progress, delayMs) {
    return this.triggerObserverAfterMs(
        'OsUpdateObserver_onOsUpdateProgressUpdated', [operation, progress],
        delayMs);
  }

  /**
   * Causes the calibration observer to fire after a delay.
   * @param {!CalibrationComponentStatus} componentStatus
   * @param {number} delayMs
   */
  triggerCalibrationObserver(componentStatus, delayMs) {
    return this.triggerObserverAfterMs(
        'CalibrationObserver_onCalibrationUpdated', componentStatus, delayMs);
  }

  /**
   * Causes the calibration overall observer to fire after a delay.
   * @param {!CalibrationOverallStatus} status
   * @param {number} delayMs
   */
  triggerCalibrationOverallObserver(status, delayMs) {
    return this.triggerObserverAfterMs(
        'CalibrationObserver_onCalibrationStepComplete', status, delayMs);
  }

  /**
   * Causes the provisioning observer to fire after a delay.
   * @param {!ProvisioningStatus} status
   * @param {number} progress
   * @param {number} delayMs
   */
  triggerProvisioningObserver(status, progress, delayMs) {
    return this.triggerObserverAfterMs(
        'ProvisioningObserver_onProvisioningUpdated', [status, progress],
        delayMs);
  }

  /**
   * Causes the hardware write protection observer to fire after a delay.
   * @param {boolean} enabled
   * @param {number} delayMs
   */
  triggerHardwareWriteProtectionObserver(enabled, delayMs) {
    return this.triggerObserverAfterMs(
        'HardwareWriteProtectionStateObserver_onHardwareWriteProtectionStateChanged',
        enabled, delayMs);
  }

  /**
   * Causes the power cable observer to fire after a delay.
   * @param {boolean} pluggedIn
   * @param {number} delayMs
   */
  triggerPowerCableObserver(pluggedIn, delayMs) {
    return this.triggerObserverAfterMs(
        'PowerCableStateObserver_onPowerCableStateChanged', pluggedIn, delayMs);
  }

  /**
   * Causes the hardware verification observer to fire after a delay.
   * @param {boolean} is_compliant
   * @param {string} error_message
   * @param {number} delayMs
   */
  triggerHardwareVerificationStatusObserver(
      is_compliant, error_message, delayMs) {
    return this.triggerObserverAfterMs(
        'HardwareVerificationStatusObserver_onHardwareVerificationResult',
        [is_compliant, error_message], delayMs);
  }

  /**
   * Causes the finalization observer to fire after a delay.
   * @param {!FinalizationStatus} status
   * @param {number} progress
   * @param {number} delayMs
   */
  triggerFinalizationObserver(status, progress, delayMs) {
    return this.triggerObserverAfterMs(
        'FinalizationObserver_onFinalizationUpdated', [status, progress],
        delayMs);
  }

  /**
   * Causes an observer to fire after a delay.
   * @param {string} method
   * @param {!T} result
   * @param {number} delayMs
   * @template T
   */
  triggerObserverAfterMs(method, result, delayMs) {
    const setDataTriggerAndResolve = function(service, resolve) {
      service.observables_.setObservableData(method, [result]);
      service.observables_.trigger(method);
      resolve();
    };

    return new Promise((resolve) => {
      if (delayMs === 0) {
        setDataTriggerAndResolve(this, resolve);
      } else {
        setTimeout(() => {
          setDataTriggerAndResolve(this, resolve);
        }, delayMs);
      }
    });
  }

  /**
   * Disables all observers and resets provider to its initial state.
   */
  reset() {
    this.registerMethods_();
    this.registerObservables_();

    this.states_ = [];
    this.stateIndex_ = 0;

    // This state data is more complicated so the behavior of the get/set
    // methods is a little different than other fakes in that they don't return
    // undefined by default.
    this.components_ = [];
    this.setGetLogResult('');
  }

  /**
   * Setup method resolvers.
   * @private
   */
  registerMethods_() {
    this.methods_ = new FakeMethodResolver();

    this.methods_.register('getCurrentState');
    this.methods_.register('transitionPreviousState');

    this.methods_.register('abortRma');

    this.methods_.register('canCancel');
    this.methods_.register('canGoBack');

    this.methods_.register('beginFinalization');

    this.methods_.register('networkSelectionComplete');

    this.methods_.register('getCurrentOsVersion');
    this.methods_.register('checkForOsUpdates');
    this.methods_.register('updateOs');
    this.methods_.register('updateOsSkipped');

    this.methods_.register('setSameOwner');
    this.methods_.register('setDifferentOwner');

    this.methods_.register('chooseManuallyDisableWriteProtect');
    this.methods_.register('chooseRsuDisableWriteProtect');
    this.methods_.register('getRsuDisableWriteProtectChallenge');
    this.methods_.register('getRsuDisableWriteProtectHwid');
    this.methods_.register('getRsuDisableWriteProtectChallengeQrCode');
    this.methods_.register('setRsuDisableWriteProtectCode');

    this.methods_.register('writeProtectManuallyDisabled');
    this.methods_.register('getWriteProtectManuallyDisabledInstructions');
    this.methods_.register(
        'setGetWriteProtectManuallyDisabledInstructionsResult');

    this.methods_.register('getWriteProtectDisableCompleteState');
    this.methods_.register('confirmManualWpDisableComplete');

    this.methods_.register('shutdownForRestock');
    this.methods_.register('continueFinalizationAfterRestock');

    this.methods_.register('getComponentList');
    this.methods_.register('setComponentList');
    this.methods_.register('reworkMainboard');

    this.methods_.register('reimageRequired');
    this.methods_.register('reimageSkipped');
    this.methods_.register('reimageFromDownload');
    this.methods_.register('reimageFromUsb');

    this.methods_.register('getRegionList');
    this.methods_.register('getSkuList');
    this.methods_.register('getOriginalSerialNumber');
    this.methods_.register('getOriginalRegion');
    this.methods_.register('getOriginalSku');
    this.methods_.register('setDeviceInformation');

    this.methods_.register('getCalibrationComponentList');
    this.methods_.register('getCalibrationSetupInstructions');
    this.methods_.register('startCalibration');
    this.methods_.register('runCalibrationStep');
    this.methods_.register('continueCalibration');
    this.methods_.register('calibrationComplete');

    this.methods_.register('provisioningComplete');

    this.methods_.register('finalizationComplete');

    this.methods_.register('writeProtectManuallyEnabled');

    this.methods_.register('getLog');
    this.methods_.register('endRmaAndReboot');
    this.methods_.register('endRmaAndShutdown');
    this.methods_.register('endRmaAndCutoffBattery');
  }

  /**
   * Setup observables.
   * @private
   */
  registerObservables_() {
    if (this.observables_) {
      this.observables_.stopAllTriggerIntervals();
    }
    this.observables_ = new FakeObservables();
    this.observables_.register('ErrorObserver_onError');
    this.observables_.register('OsUpdateObserver_onOsUpdateProgressUpdated');
    this.observables_.register('CalibrationObserver_onCalibrationUpdated');
    this.observables_.register('CalibrationObserver_onCalibrationStepComplete');
    this.observables_.register('ProvisioningObserver_onProvisioningUpdated');
    this.observables_.register(
        'HardwareWriteProtectionStateObserver_onHardwareWriteProtectionStateChanged');
    this.observables_.register(
        'PowerCableStateObserver_onPowerCableStateChanged');
    this.observables_.register(
        'HardwareVerificationStatusObserver_onHardwareVerificationResult');
    this.observables_.register('FinalizationObserver_onFinalizationUpdated');
  }

  /**
   * @private
   * @param {string} method
   * @param {!RmaState} expectedState
   * @returns {!Promise<!StateResult>}
   */
  getNextStateForMethod_(method, expectedState) {
    if (this.states_.length === 0) {
      this.setFakeStateForMethod_(
          method, RmaState.kUnknown, false, false,
          RmadErrorCode.kRmaNotRequired);
    } else if (this.stateIndex_ >= this.states_.length - 1) {
      // It should not be possible for stateIndex_ to be out of range unless
      // there is a bug in the fake.
      assert(this.stateIndex_ < this.states_.length);
      const state = this.states_[this.stateIndex_];
      this.setFakeStateForMethod_(
          method, state.state, state.canCancel, state.canGoBack,
          RmadErrorCode.kTransitionFailed);
    } else if (this.states_[this.stateIndex_].state !== expectedState) {
      // Error: Called in wrong state.
      const state = this.states_[this.stateIndex_];
      this.setFakeStateForMethod_(
          method, state.state, state.canCancel, state.canGoBack,
          RmadErrorCode.kRequestInvalid);
    } else {
      // Success.
      this.stateIndex_++;
      const state = this.states_[this.stateIndex_];
      this.setFakeStateForMethod_(
          method, state.state, state.canCancel, state.canGoBack, state.error);
    }
    return this.methods_.resolveMethodWithDelay(
        method, this.resolveMethodDelayMs_);
  }

  /**
   * Sets the value that will be returned when calling getCurrent().
   * @private
   * @param {!RmaState} state
   * @param {boolean} canCancel,
   * @param {boolean} canGoBack,
   * @param {!RmadErrorCode} error
   */
  setFakeCurrentState_(state, canCancel, canGoBack, error) {
    this.setFakeStateForMethod_(
        'getCurrentState', state, canCancel, canGoBack, error);
  }

  /**
   * Sets the value that will be returned when calling
   * transitionPreviousState().
   * @private
   * @param {!RmaState} state
   * @param {boolean} canCancel,
   * @param {boolean} canGoBack,
   * @param {!RmadErrorCode} error
   */
  setFakePrevState_(state, canCancel, canGoBack, error) {
    this.setFakeStateForMethod_(
        'transitionPreviousState', state, canCancel, canGoBack, error);
  }

  /**
   * Sets the value that will be returned when calling state specific functions
   * that update state. e.g. setSameOwner()
   * @private
   * @param {string} method
   * @param {!RmaState} state
   * @param {boolean} canCancel,
   * @param {boolean} canGoBack,
   * @param {!RmadErrorCode} error
   */
  setFakeStateForMethod_(method, state, canCancel, canGoBack, error) {
    this.methods_.setResult(method, /** @type {!StateResult} */ ({
                              state: state,
                              canCancel: canCancel,
                              canGoBack: canGoBack,
                              error: error
                            }));
  }
}

// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {
  CaptureIntent,
} from '/media/capture/video/chromeos/mojom/camera_app.mojom-webui.js';

import {
  assert,
  assertInstanceof,
} from '../../../chrome_util.js';
import {
  CaptureCandidate,           // eslint-disable-line no-unused-vars
  ConstraintsPreferrer,       // eslint-disable-line no-unused-vars
  PhotoConstraintsPreferrer,  // eslint-disable-line no-unused-vars
  VideoConstraintsPreferrer,  // eslint-disable-line no-unused-vars
} from '../../../device/constraints_preferrer.js';
// eslint-disable-next-line no-unused-vars
import {StreamConstraints} from '../../../device/stream_constraints.js';
import * as dom from '../../../dom.js';
// eslint-disable-next-line no-unused-vars
import {DeviceOperator} from '../../../mojo/device_operator.js';
import * as state from '../../../state.js';
import {
  Facing,  // eslint-disable-line no-unused-vars
  Mode,
  Resolution,  // eslint-disable-line no-unused-vars
} from '../../../type.js';

import {
  ModeBase,     // eslint-disable-line no-unused-vars
  ModeFactory,  // eslint-disable-line no-unused-vars
} from './mode_base.js';
import {
  PhotoFactory,
  PhotoHandler,  // eslint-disable-line no-unused-vars
} from './photo.js';
import {PortraitFactory} from './portrait.js';
import {
  ScanFactory,
  ScanHandler,  // eslint-disable-line no-unused-vars
} from './scan.js';
import {SquareFactory} from './square.js';
import {
  VideoFactory,
  VideoHandler,  // eslint-disable-line no-unused-vars
} from './video.js';

export {PhotoHandler, PhotoResult} from './photo.js';
export {getDefaultScanCorners, ScanHandler} from './scan.js';
export {setAvc1Parameters, Video, VideoHandler, VideoResult} from './video.js';

/**
 * Callback to trigger mode switching.
 * return {!Promise}
 * @typedef {function(): !Promise}
 */
export let DoSwitchMode;

/* eslint-disable no-unused-vars */

/**
 * Parameters for capture settings.
 * @typedef {{
 *   mode: !Mode,
 *   constraints: !StreamConstraints,
 *   captureResolution: !Resolution,
 *   videoSnapshotResolution: !Resolution,
 * }}
 */
let CaptureParams;

/**
 * The abstract interface for the mode configuration.
 * @interface
 */
class ModeConfig {
  /**
   * @param {string} deviceId
   * @return {!Promise<boolean>} Resolves to boolean indicating whether the mode
   *     is supported by video device with specified device id.
   * @abstract
   */
  async isSupported(deviceId) {}

  /**
   * @param {!Resolution} captureResolution
   * @param {!Resolution} previewResolution
   * @return {boolean}
   * @abstract
   */
  isSupportPTZ(captureResolution, previewResolution) {}

  /**
   * Makes video capture device prepared for capturing in this mode.
   * @param {!StreamConstraints} constraints Constraints for preview
   *     stream.
   * @param {!Resolution} captureResolution
   * @return {!Promise}
   * @abstract
   */
  async prepareDevice(constraints, captureResolution) {}

  /**
   * Get general stream constraints of this mode for fake cameras.
   * @param {?string} deviceId
   * @return {!Array<!StreamConstraints>}
   * @abstract
   */
  getConstraintsForFakeCamera(deviceId) {}

  /* eslint-disable getter-return */

  /**
   * Gets factory to create capture object for this mode.
   * @return {!ModeFactory}
   * @abstract
   */
  getCaptureFactory() {}

  /**
   * HALv3 constraints preferrer for this mode.
   * @return {!ConstraintsPreferrer}
   * @abstract
   */
  get constraintsPreferrer() {}

  /**
   * Mode to be fallbacked to when fail to configure this mode.
   * @return {!Mode}
   * @abstract
   */
  get fallbackMode() {}

  /* eslint-enable getter-return */
}

/* eslint-enable no-unused-vars */

/**
 * Mode controller managing capture sequence of different camera mode.
 */
export class Modes {
  /**
   * @param {!Mode} defaultMode Default mode to be switched to.
   * @param {!PhotoConstraintsPreferrer} photoPreferrer
   * @param {!VideoConstraintsPreferrer} videoPreferrer
   * @param {!DoSwitchMode} doSwitchMode
   * @param {!PhotoHandler} photoHandler
   * @param {!VideoHandler} videoHandler
   * @param {!ScanHandler} scanHandler
   */
  constructor(
      defaultMode, photoPreferrer, videoPreferrer, doSwitchMode, photoHandler,
      videoHandler, scanHandler) {
    /**
     * @type {!DoSwitchMode}
     * @private
     */
    this.doSwitchMode_ = doSwitchMode;

    /**
     * Capture controller of current camera mode.
     * @type {?ModeBase}
     */
    this.current = null;

    /**
     * @type {!HTMLElement}
     * @private
     */
    this.modesGroup_ = dom.get('#modes-group', HTMLElement);

    /**
     * Parameters to create mode capture controller.
     * @type {?CaptureParams}
     * @private
     */
    this.captureParams_ = null;

    /**
     * Returns a set of general constraints for fake cameras.
     * @param {boolean} videoMode Is getting constraints for video mode.
     * @param {string} deviceId Id of video device.
     * @return {!Array<!StreamConstraints>} Result of
     *     constraints-candidates.
     */
    const getConstraintsForFakeCamera = function(videoMode, deviceId) {
      const frameRate = {min: 20, ideal: 30};
      return [
        {
          deviceId,
          audio: videoMode,
          video: {
            aspectRatio: {ideal: videoMode ? 1.7777777778 : 1.3333333333},
            width: {min: 1280},
            frameRate,
          },
        },
        {
          deviceId,
          audio: videoMode,
          video: {
            width: {min: 640},
            frameRate,
          },
        },
      ];
    };

    // Workaround for b/184089334 on PTZ camera to use preview frame as photo
    // result.
    const checkSupportPTZForPhotoMode =
        (captureResolution, previewResolution) =>
            captureResolution.equals(previewResolution);

    /**
     * @param {!StreamConstraints} constraints
     * @param {!Resolution} resolution
     * @param {CaptureIntent} captureIntent
     * @return {!Promise}
     */
    const prepareDeviceForPhoto =
        async (constraints, resolution, captureIntent) => {
      const deviceOperator = await DeviceOperator.getInstance();
      if (deviceOperator === null) {
        return;
      }
      const deviceId = constraints.deviceId;
      await deviceOperator.setCaptureIntent(deviceId, captureIntent);
      await deviceOperator.setStillCaptureResolution(deviceId, resolution);
    };

    /**
     * Mode classname and related functions and attributes.
     * @type {!Object<!Mode, !ModeConfig>}
     * @private
     */
    this.allModes_ = {
      [Mode.VIDEO]: {
        getCaptureFactory: () => {
          const params = this.getCaptureParams();
          return new VideoFactory(
              params.constraints, params.captureResolution,
              params.videoSnapshotResolution, videoHandler);
        },
        isSupported: async () => true,
        isSupportPTZ: () => true,
        prepareDevice: async (constraints, resolution) => {
          const deviceOperator = await DeviceOperator.getInstance();
          if (deviceOperator === null) {
            return;
          }
          const deviceId = constraints.deviceId;
          await deviceOperator.setCaptureIntent(
              deviceId, CaptureIntent.VIDEO_RECORD);
          if (await deviceOperator.isBlobVideoSnapshotEnabled(deviceId)) {
            await deviceOperator.setStillCaptureResolution(
                deviceId, this.getCaptureParams().videoSnapshotResolution);
          }

          let /** number */ minFrameRate = 0;
          let /** number */ maxFrameRate = 0;
          if (constraints.video && constraints.video.frameRate) {
            const frameRate = constraints.video.frameRate;
            if (frameRate.exact) {
              minFrameRate = frameRate.exact;
              maxFrameRate = frameRate.exact;
            } else if (frameRate.min && frameRate.max) {
              minFrameRate = frameRate.min;
              maxFrameRate = frameRate.max;
            }
            // TODO(wtlee): To set the fps range to the default value, we should
            // remove the frameRate from constraints instead of using incomplete
            // range.
          }
          await deviceOperator.setFpsRange(
              deviceId, minFrameRate, maxFrameRate);
        },
        constraintsPreferrer: videoPreferrer,
        getConstraintsForFakeCamera:
            getConstraintsForFakeCamera.bind(this, true),
        fallbackMode: Mode.PHOTO,
      },
      [Mode.PHOTO]: {
        getCaptureFactory: () => {
          const params = this.getCaptureParams();
          return new PhotoFactory(
              params.constraints, params.captureResolution, photoHandler);
        },
        isSupported: async () => true,
        isSupportPTZ: checkSupportPTZForPhotoMode,
        prepareDevice: async (constraints, resolution) => prepareDeviceForPhoto(
            constraints, resolution, CaptureIntent.STILL_CAPTURE),
        constraintsPreferrer: photoPreferrer,
        getConstraintsForFakeCamera:
            getConstraintsForFakeCamera.bind(this, false),
        fallbackMode: Mode.SQUARE,
      },
      [Mode.SQUARE]: {
        getCaptureFactory: () => {
          const params = this.getCaptureParams();
          return new SquareFactory(
              params.constraints, params.captureResolution, photoHandler);
        },
        isSupported: async () => true,
        isSupportPTZ: checkSupportPTZForPhotoMode,
        prepareDevice: async (constraints, resolution) => prepareDeviceForPhoto(
            constraints, resolution, CaptureIntent.STILL_CAPTURE),
        constraintsPreferrer: photoPreferrer,
        getConstraintsForFakeCamera:
            getConstraintsForFakeCamera.bind(this, false),
        fallbackMode: Mode.PHOTO,
      },
      [Mode.PORTRAIT]: {
        getCaptureFactory: () => {
          const params = this.getCaptureParams();
          return new PortraitFactory(
              params.constraints, params.captureResolution, photoHandler);
        },
        isSupported: async (deviceId) => {
          if (deviceId === null) {
            return false;
          }
          const deviceOperator = await DeviceOperator.getInstance();
          if (deviceOperator === null) {
            return false;
          }
          return await deviceOperator.isPortraitModeSupported(deviceId);
        },
        isSupportPTZ: checkSupportPTZForPhotoMode,
        prepareDevice: async (constraints, resolution) => prepareDeviceForPhoto(
            constraints, resolution, CaptureIntent.STILL_CAPTURE),
        constraintsPreferrer: photoPreferrer,
        getConstraintsForFakeCamera:
            getConstraintsForFakeCamera.bind(this, false),
        fallbackMode: Mode.PHOTO,
      },
      [Mode.SCAN]: {
        getCaptureFactory: () => {
          const params = this.getCaptureParams();
          return new ScanFactory(
              params.constraints, params.captureResolution, scanHandler);
        },
        isSupported: async () => state.get(state.State.SHOW_SCAN_MODE),
        isSupportPTZ: checkSupportPTZForPhotoMode,
        prepareDevice: async (constraints, resolution) => prepareDeviceForPhoto(
            constraints, resolution, CaptureIntent.DOCUMENT),
        constraintsPreferrer: photoPreferrer,
        getConstraintsForFakeCamera:
            getConstraintsForFakeCamera.bind(this, false),
        fallbackMode: Mode.PHOTO,
      },
    };

    dom.getAll('.mode-item>input', HTMLInputElement).forEach((element) => {
      element.addEventListener('click', (event) => {
        if (!state.get(state.State.STREAMING) ||
            state.get(state.State.TAKING)) {
          event.preventDefault();
        }
      });
      element.addEventListener('change', async (event) => {
        if (element.checked) {
          const mode = /** @type {!Mode} */ (element.dataset['mode']);
          this.updateModeUI_(mode);
          state.set(state.State.MODE_SWITCHING, true);
          const isSuccess = await this.doSwitchMode_();
          state.set(state.State.MODE_SWITCHING, false, {hasError: !isSuccess});
        }
      });
    });

    [state.State.EXPERT, state.State.SAVE_METADATA].forEach(
        (/** !state.State */ s) => {
          state.addObserver(s, () => {
            this.updateSaveMetadata_();
          });
        });

    // Set default mode when app started.
    this.updateModeUI_(defaultMode);
  }

  /**
   * @return {!Array<!Mode>}
   * @private
   */
  get allModeNames_() {
    return Object.keys(this.allModes_);
  }

  /**
   * @return {!CaptureParams}
   * @private
   */
  getCaptureParams() {
    assert(this.captureParams_ !== null);
    return this.captureParams_;
  }

  /**
   * Updates state of mode related UI to the target mode.
   * @param {!Mode} mode Mode to be toggled.
   * @private
   */
  updateModeUI_(mode) {
    this.allModeNames_.forEach((m) => state.set(m, m === mode));
    const element =
        dom.get(`.mode-item>input[data-mode=${mode}]`, HTMLInputElement);
    element.checked = true;
    const wrapper = assertInstanceof(element.parentElement, HTMLDivElement);
    const scrollLeft = wrapper.offsetLeft -
        (this.modesGroup_.offsetWidth - wrapper.offsetWidth) / 2;
    this.modesGroup_.scrollTo({
      left: scrollLeft,
      top: 0,
      behavior: 'smooth',
    });
  }

  /**
   * Gets all mode candidates. Desired trying sequence of candidate modes is
   * reflected in the order of the returned array.
   * @return {!Array<!Mode>} Mode candidates to be tried out.
   */
  getModeCandidates() {
    const tried = {};
    const /** !Array<!Mode> */ results = [];
    let mode = this.allModeNames_.find(state.get);
    assert(mode !== undefined);
    while (!tried[mode]) {
      tried[mode] = true;
      results.push(mode);
      mode = this.allModes_[mode].fallbackMode;
    }
    return results;
  }

  /**
   * Gets all available capture resolution and its corresponding preview
   * constraints for the given |mode| and |deviceId|.
   * @param {!Mode} mode
   * @param {string} deviceId
   * @return {!Array<!CaptureCandidate>}
   */
  getResolutionCandidates(mode, deviceId) {
    return this.allModes_[mode].constraintsPreferrer.getSortedCandidates(
        deviceId);
  }

  /**
   * Gets a general set of resolution candidates given by |mode| and |deviceId|
   * for fake cameras.
   * @param {!Mode} mode
   * @param {string} deviceId
   * @return {!Array<!CaptureCandidate>}
   */
  getFakeResolutionCandidates(mode, deviceId) {
    const previewCandidates =
        this.allModes_[mode].getConstraintsForFakeCamera(deviceId);
    return [{resolution: null, previewCandidates}];
  }

  /**
   * Gets factory to create mode capture object.
   * @param {!Mode} mode
   * @return {!ModeFactory}
   */
  getModeFactory(mode) {
    return this.allModes_[mode].getCaptureFactory();
  }

  /**
   * @param {!Mode} mode
   * @param {!StreamConstraints} constraints Constraints for preview
   *     stream.
   * @param {!Resolution} captureResolution
   * @param {!Resolution} videoSnapshotResolution
   */
  setCaptureParams(
      mode, constraints, captureResolution, videoSnapshotResolution) {
    this.captureParams_ =
        {mode, constraints, captureResolution, videoSnapshotResolution};
  }

  /**
   * Makes video capture device prepared for capturing in this mode.
   * @return {!Promise}
   */
  async prepareDevice() {
    if (state.get(state.State.USE_FAKE_CAMERA)) {
      return;
    }
    const {mode, captureResolution, constraints} = this.getCaptureParams();
    return this.allModes_[mode].prepareDevice(
        constraints, assertInstanceof(captureResolution, Resolution));
  }

  /**
   * Gets supported modes for video device of given device id.
   * @param {string} deviceId Device id of the video device.
   * @return {!Promise<!Array<!Mode>>} All supported mode for
   *     the video device.
   */
  async getSupportedModes(deviceId) {
    const /** !Array<!Mode> */ supportedModes = [];
    for (const mode of this.allModeNames_) {
      const obj = this.allModes_[mode];
      if (await obj.isSupported(deviceId)) {
        supportedModes.push(mode);
      }
    }
    return supportedModes;
  }

  /**
   * @param {!Mode} mode
   * @param {!Resolution} captureResolution
   * @param {!Resolution} previewResolution
   * @return {boolean}
   */
  isSupportPTZ(mode, captureResolution, previewResolution) {
    return this.allModes_[mode].isSupportPTZ(
        captureResolution, previewResolution);
  }

  /**
   * Updates mode selection UI according to given device id.
   * @param {string} deviceId
   * @return {!Promise}
   */
  async updateModeSelectionUI(deviceId) {
    const supportedModes = await this.getSupportedModes(deviceId);
    const items = dom.getAll('div.mode-item', HTMLDivElement);
    let first = null;
    let last = null;
    items.forEach((el) => {
      const radio = dom.getFrom(el, 'input[type=radio]', HTMLInputElement);
      const supported =
          supportedModes.includes(/** @type {!Mode} */ (radio.dataset['mode']));
      el.classList.toggle('hide', !supported);
      if (supported) {
        if (first === null) {
          first = el;
        }
        last = el;
      }
    });
    items.forEach((el) => {
      el.classList.toggle('first', el === first);
      el.classList.toggle('last', el === last);
    });
  }

  /**
   * Creates and updates current mode object.
   * @param {!ModeFactory} factory The factory ready for producing mode capture
   *     object.
   * @param {!MediaStream} stream Stream of the new switching mode.
   * @param {!Facing} facing Camera facing of the current mode.
   * @param {?string} deviceId Device id of currently working video device.
   * @return {!Promise}
   */
  async updateMode(factory, stream, facing, deviceId) {
    if (this.current !== null) {
      await this.current.clear();
      await this.disableSaveMetadata_();
    }
    const {mode, captureResolution} = this.getCaptureParams();
    this.updateModeUI_(mode);
    this.current = factory.produce();
    if (deviceId && captureResolution) {
      this.allModes_[mode].constraintsPreferrer.updateValues(
          deviceId, stream, facing, captureResolution);
    }
    await this.updateSaveMetadata_();
  }

  /**
   * Clears everything when mode is not needed anymore.
   * @return {!Promise}
   */
  async clear() {
    if (this.current !== null) {
      await this.current.clear();
      await this.disableSaveMetadata_();
    }
    this.captureParams_ = null;
    this.current = null;
  }

  /**
   * Checks whether to save image metadata or not.
   * @return {!Promise} Promise for the operation.
   * @private
   */
  async updateSaveMetadata_() {
    if (state.get(state.State.EXPERT) && state.get(state.State.SAVE_METADATA)) {
      await this.enableSaveMetadata_();
    } else {
      await this.disableSaveMetadata_();
    }
  }

  /**
   * Enables save metadata of subsequent photos in the current mode.
   * @return {!Promise} Promise for the operation.
   * @private
   */
  async enableSaveMetadata_() {
    if (this.current !== null) {
      await this.current.addMetadataObserver();
    }
  }

  /**
   * Disables save metadata of subsequent photos in the current mode.
   * @return {!Promise} Promise for the operation.
   * @private
   */
  async disableSaveMetadata_() {
    if (this.current !== null) {
      await this.current.removeMetadataObserver();
    }
  }
}

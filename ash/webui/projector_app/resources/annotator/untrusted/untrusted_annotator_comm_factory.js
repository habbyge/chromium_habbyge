// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {PostMessageAPIClient} from 'chrome-untrusted://projector/js/post_message_api_client.m.js';
import {RequestHandler} from 'chrome-untrusted://projector/js/post_message_api_request_handler.m.js';

const TARGET_URL = 'chrome://projector/';

// A client that sends messages to the chrome://projector embedder.
export class TrustedAnnotatorClient extends PostMessageAPIClient {
  /**
   * @param {!Window} parentWindow The embedder window from which requests
   *     come.
   */
  constructor(parentWindow) {
    super(TARGET_URL, parentWindow);
    // TODO(b/196245932) Register the onUndoRedoAvailabilityChanged as callback
    // to the ink library wrapper.
  }

  /**
   * Notifies the native ui that undo/redo has become available.
   * @param {boolean} undoAvailable
   * @param {boolean} redoAvailable
   * @return {Promise}
   */
  onUndoRedoAvailabilityChanged(undoAvailable, redoAvailable) {
    return this.callApiFn(
        'onUndoRedoAvailabilityChanged', [undoAvailable, redoAvailable]);
  }
}

/**
 * Class that implements the RequestHandler inside the Projector untrusted
 * scheme for Annotator.
 */
export class UntrustedAnnotatorRequestHandler extends RequestHandler {
  /**
   * @param {!Window} parentWindow The embedder window from which requests
   *     come.
   */
  constructor(parentWindow) {
    super(null, TARGET_URL, TARGET_URL);
    this.targetWindow_ = parentWindow;

    this.registerMethod('setTool', (tool) => {
      // TODO(b/196245932) Call into the Ink library to set tool.
      return true;
    });

    this.registerMethod('undo', () => {
      // TODO(b/196245932) Call into the Ink wrapper to undo.
      return true;
    });

    this.registerMethod('redo', () => {
      // TODO(b/196245932) Call into Ink wrapper to redo.
      return true;
    });

    this.registerMethod('clear', () => {
      // TODO(b/196245932) call into Ink wrapper to clear.
      return true;
    });
  }

  /** @override */
  targetWindow() {
    return this.targetWindow_;
  }
}

/**
 * This is a class that is used to setup the duplex communication channels
 * between this origin, chrome-untrusted://projector/* and the embedder content.
 */
export class AnnotatorUntrustedCommFactory {
  /**
   * Creates the instances of PostMessageAPIClient and Requesthandler.
   */
  static maybeCreateInstances() {
    if (AnnotatorUntrustedCommFactory.client_ ||
        AnnotatorUntrustedCommFactory.requestHandler_) {
      return;
    }

    AnnotatorUntrustedCommFactory.client_ =
        new TrustedAnnotatorClient(window.parent);

    AnnotatorUntrustedCommFactory.requestHandler_ =
        new UntrustedAnnotatorRequestHandler(window.parent);
  }

  /**
   * In order to use this class, please do the following (e.g. To notify when
   * undo-redo becomes available):
   * AnnotatorUntrustedCommFactory.
   *     getPostMessageAPIClient().
   *     onUndoRedoAvailabilityChanged(true, true);
   * @return {!TrustedAnnotatorClient}
   */
  static getPostMessageAPIClient() {
    // AnnotatorUntrustedCommFactory.client_ should be available. However to be
    // on the cautious side create an instance here if getPostMessageAPIClient
    // is triggered before the page finishes loading.
    AnnotatorUntrustedCommFactory.maybeCreateInstances();
    return AnnotatorUntrustedCommFactory.client_;
  }
}

document.addEventListener('DOMContentLoaded', () => {
  // Create instances of the singletons(PostMessageAPIClient and
  // RequestHandler) when the document has finished loading.
  AnnotatorUntrustedCommFactory.maybeCreateInstances();
});

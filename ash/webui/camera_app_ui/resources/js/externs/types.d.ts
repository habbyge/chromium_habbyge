// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(pihsun): Remove this once we fully specify all the types.
/* eslint-disable @typescript-eslint/no-explicit-any */

// ESLint doesn't like "declare class" without jsdoc.
/* eslint-disable require-jsdoc */

// TODO(b/172340451): Use the types generated by Mojo TypeScript binding
// generator once https://crbug.com/1002798 is finished.
declare module '*.mojom-webui.js';

// This is currently a Chrome only API, and the spec is still in working draft
// stage.
// https://developer.mozilla.org/en-US/docs/Web/API/UIEvent/sourceCapabilities

interface UIEvent extends Event {
  readonly sourceCapabilities: InputDeviceCapabilities|null;
}

interface InputDeviceCapabilities {
  readonly firesTouchEvents: boolean;
  readonly pointerMovementScrolls: boolean;
}

// The new "subtree" option is not published in the latest spec yet.
// Ref: https://github.com/w3c/csswg-drafts/pull/3902

interface GetAnimationsOptions {
  subtree: boolean;
}

interface Animatable {
  getAnimations(options?: GetAnimationsOptions): Animation[];
}

// File System Access API: This is currently a Chrome only API, and the spec is
// still in working draft stage.
// https://wicg.github.io/file-system-access/

// close() is only implemented in Chrome so it's not in upstream type
// definitions. Ref:
// https://github.com/microsoft/TypeScript-DOM-lib-generator/pull/827.
interface WritableStream {
  close(): Promise<void>;
}

interface FileSystemHandleBase {
  readonly name: string;
}

type FileSystemWriteChunkType = BufferSource|Blob|string;

interface FileSystemWritableFileStream extends WritableStream {
  seek(position: number): Promise<void>;
  truncate(size: number): Promise<void>;
  write(data: FileSystemWriteChunkType): Promise<void>;
}

interface FileSystemCreateWritableOptions {
  keepExistingData?: boolean;
}

interface FileSystemFileHandle extends FileSystemHandleBase {
  readonly kind: 'file';
  createWritable(options?: FileSystemCreateWritableOptions):
      Promise<FileSystemWritableFileStream>;
  getFile(): Promise<File>;
}

interface FileSystemGetDirectoryOptions {
  create?: boolean;
}

interface FileSystemGetFileOptions {
  create?: boolean;
}

interface FileSystemDirectoryHandle extends FileSystemHandleBase {
  readonly kind: 'directory';
  getDirectoryHandle(name: string, options?: FileSystemGetDirectoryOptions):
      Promise<FileSystemDirectoryHandle>;
  getFileHandle(name: string, options?: FileSystemGetFileOptions):
      Promise<FileSystemFileHandle>;
  removeEntry(name: string): Promise<void>;
  values(): IterableIterator<FileSystemHandle>;
}

type FileSystemHandle = FileSystemFileHandle|FileSystemDirectoryHandle;

type VarFor<T> = {
  prototype: T;
  // clang-format parses "new" in a wrong way.
  // clang-format off
  new(): T;
  // clang-format on
};

declare const FileSystemDirectoryHandle: VarFor<FileSystemDirectoryHandle>;
declare const FileSystemFileHandle: VarFor<FileSystemFileHandle>;

interface StorageManager {
  getDirectory(): Promise<FileSystemDirectoryHandle>;
}

// Chrome WebUI specific helper.
// https://source.chromium.org/chromium/chromium/src/+/main:ui/webui/resources/js/load_time_data.js

interface Window {
  loadTimeData: {
    getBoolean(id: string): boolean; getString(id: string): string;
    getStringF(id: string, ...args: (number|string)[]): string;
  }
}

// v8 specific stack information.
interface CallSite {
  getFileName(): string|undefined;
  getFunctionName(): string|undefined;
  getLineNumber(): number|undefined;
  getColumnNumber(): number|undefined;
}

// v8 specific stack trace customizing, see https://v8.dev/docs/stack-trace-api.
interface ErrorConstructor {
  prepareStackTrace(error: Error, structuredStackTrace: CallSite[]): void;
}

// Chrome private API for crash report.
declare namespace chrome.crashReportPrivate {
  export type ErrorInfo = {
    message: string,
    url: string,
    columnNumber?: number,
    debugId?: string,
    lineNumber?: number,
    product?: string,
    stackTrace?: string,
    version?: string,
  };
  export const reportError: (info: ErrorInfo, callback: () => void) => void;
}

// Idle Detection API: This is currently a Chrome only API gated behind Origin
// Trial, and the spec is still in working draft stage.
// https://wicg.github.io/idle-detection/
declare class IdleDetector extends EventTarget {
  screenState: 'locked'|'unlocked';
  start: () => Promise<void>;
}

// CSS Properties and Values API Level 1: The spec is still in working draft
// stage.
// https://www.w3.org/TR/css-properties-values-api-1/
interface PropertyDefinition {
  name: string;
  inherits: boolean;
  syntax?: string;
  initialValue?: string;
}

declare namespace CSS {
  function registerProperty(definition: PropertyDefinition): void;
}

// File handling API: This is currently a Chrome only API.
// https://github.com/WICG/file-handling/blob/main/explainer.md
interface Window {
  readonly launchQueue: LaunchQueue;
}

interface LaunchQueue {
  setConsumer(consumer: LaunchConsumer): void;
}

type LaunchConsumer = (params: LaunchParams) => void;

interface LaunchParams {
  readonly files: ReadonlyArray<FileSystemHandle>;
}

// HTMLVideoElement.requestVideoFrameCallback, this is currently available in
// Chrome and the spec is still in draft stage.
// https://wicg.github.io/video-rvfc/
interface VideoFrameMetadata {
  expectedDisplayTime: DOMHighResTimeStamp;
  height: number;
  mediaTime: number;
  presentationTime: DOMHighResTimeStamp;
  presentedFrames: number;
  width: number;
  captureTime?: DOMHighResTimeStamp;
  processingDuration?: number;
  receiveTime?: DOMHighResTimeStamp;
  rtpTimestamp?: number;
}

type VideoFrameRequestCallback =
    (now: DOMHighResTimeStamp, metadata: VideoFrameMetadata) => void;

interface HTMLVideoElement {
  requestVideoFrameCallback(callback: VideoFrameRequestCallback): number;
  cancelVideoFrameCallback(handle: number): undefined;
}

// Barcode Detection API, this is currently only supported in Chrome on
// ChromeOS, Android or macOS.
// https://wicg.github.io/shape-detection-api/
declare class BarcodeDetector {
  static getSupportedFormats(): Promise<BarcodeFormat[]>;
  constructor(barcodeDetectorOptions?: BarcodeDetectorOptions);
  detect(image: ImageBitmapSource): Promise<DetectedBarcode[]>;
}

interface BarcodeDetectorOptions {
  formats?: BarcodeFormat[];
}

interface DetectedBarcode {
  boundingBox: DOMRectReadOnly;
  rawValue: string;
  format: BarcodeFormat;
  cornerPoints: ReadonlyArray<Point2D>;
}

type BarcodeFormat =
    'aztec'|'code_128'|'code_39'|'code_93'|'codabar'|'data_matrix'|'ean_13'|
    'ean_8'|'itf'|'pdf417'|'qr_code'|'unknown'|'upc_a'|'upc_e';

// Trusted Types, this spec is still in draft stage.
// https://w3c.github.io/webappsec-trusted-types/dist/spec/
interface TrustedScriptURL {
  toJSON(): string;
}

interface TrustedHTML {
  toJSON(): string;
}

interface TrustedScript {
  toJSON(): string;
}

interface TrustedTypePolicyOptions {
  createHTML?: CreateHTMLCallback;
  createScript?: CreateScriptCallback;
  createScriptURL?: CreateScriptURLCallback;
}

type CreateHTMLCallback = (input: string, arguments: any) => string;
type CreateScriptCallback = (input: string, arguments: any) => string;
type CreateScriptURLCallback = (input: string, arguments: any) => string;

interface TrustedTypePolicy {
  readonly name: string;
  createHTML(input: string, arguments: any): TrustedHTML;
  createScript(input: string, arguments: any): TrustedScript;
  createScriptURL(input: string, arguments: any): TrustedScriptURL;
}

interface TrustedTypePolicyFactory {
  createPolicy(policyName: string, policyOptions?: TrustedTypePolicyOptions):
      TrustedTypePolicy;
  isHTML(value: any): boolean;
  isScript(value: any): boolean;
  isScriptURL(value: any): boolean;
  readonly emptyHTML: TrustedHTML;
  readonly emptyScript: TrustedScript;
  getAttributeType(
      tagName: string, attribute: string, elementNs?: string,
      attrNs?: string): string;
  getPropertyType(tagName: string, property: string, elementNs?: string):
      string;
  readonly defaultPolicy: TrustedTypePolicy;
}

declare const trustedTypes: TrustedTypePolicyFactory;

// Web Share API. TypeScript only includes types for share() without the files
// field.
// https://w3c.github.io/web-share/
interface ShareData {
  files?: File[];
  text?: string;
  title?: string;
  url?: string;
}

interface Navigator {
  canShare: (data: ShareData) => boolean;
}

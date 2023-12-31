// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/messages/android/message_wrapper.h"

#include "base/android/jni_string.h"
#include "base/logging.h"
#include "components/messages/android/jni_headers/MessageWrapper_jni.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/android/java_bitmap.h"

namespace messages {

MessageWrapper::MessageWrapper(MessageIdentifier message_identifier)
    : MessageWrapper(message_identifier,
                     base::NullCallback(),
                     base::NullCallback()) {}

MessageWrapper::MessageWrapper(MessageIdentifier message_identifier,
                               base::OnceClosure action_callback,
                               DismissCallback dismiss_callback)
    : action_callback_(std::move(action_callback)),
      dismiss_callback_(std::move(dismiss_callback)),
      message_enqueued_(false) {
  JNIEnv* env = base::android::AttachCurrentThread();
  java_message_wrapper_ =
      Java_MessageWrapper_create(env, reinterpret_cast<int64_t>(this),
                                 static_cast<int>(message_identifier));
}

MessageWrapper::~MessageWrapper() {
  CHECK(!message_enqueued_);
}

std::u16string MessageWrapper::GetTitle() {
  JNIEnv* env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jstring> jtitle =
      Java_MessageWrapper_getTitle(env, java_message_wrapper_);
  return jtitle.is_null() ? std::u16string()
                          : base::android::ConvertJavaStringToUTF16(jtitle);
}

void MessageWrapper::SetTitle(const std::u16string& title) {
  JNIEnv* env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jstring> jtitle =
      base::android::ConvertUTF16ToJavaString(env, title);
  Java_MessageWrapper_setTitle(env, java_message_wrapper_, jtitle);
}

std::u16string MessageWrapper::GetDescription() {
  JNIEnv* env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jstring> jdescription =
      Java_MessageWrapper_getDescription(env, java_message_wrapper_);
  return jdescription.is_null()
             ? std::u16string()
             : base::android::ConvertJavaStringToUTF16(jdescription);
}

void MessageWrapper::SetDescription(const std::u16string& description) {
  JNIEnv* env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jstring> jdescription =
      base::android::ConvertUTF16ToJavaString(env, description);
  Java_MessageWrapper_setDescription(env, java_message_wrapper_, jdescription);
}

int MessageWrapper::GetDescriptionMaxLines() {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_MessageWrapper_getDescriptionMaxLines(env, java_message_wrapper_);
}

void MessageWrapper::SetDescriptionMaxLines(int max_lines) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_MessageWrapper_setDescriptionMaxLines(env, java_message_wrapper_,
                                             max_lines);
}

std::u16string MessageWrapper::GetPrimaryButtonText() {
  JNIEnv* env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jstring> jprimary_button_text =
      Java_MessageWrapper_getPrimaryButtonText(env, java_message_wrapper_);
  return jprimary_button_text.is_null()
             ? std::u16string()
             : base::android::ConvertJavaStringToUTF16(jprimary_button_text);
}

void MessageWrapper::SetPrimaryButtonText(
    const std::u16string& primary_button_text) {
  JNIEnv* env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jstring> jprimary_button_text =
      base::android::ConvertUTF16ToJavaString(env, primary_button_text);
  Java_MessageWrapper_setPrimaryButtonText(env, java_message_wrapper_,
                                           jprimary_button_text);
}

std::u16string MessageWrapper::GetSecondaryButtonMenuText() {
  JNIEnv* env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jstring> jsecondary_button_menu_text =
      Java_MessageWrapper_getSecondaryButtonMenuText(env,
                                                     java_message_wrapper_);
  return jsecondary_button_menu_text.is_null()
             ? std::u16string()
             : base::android::ConvertJavaStringToUTF16(
                   jsecondary_button_menu_text);
}

void MessageWrapper::SetSecondaryButtonMenuText(
    const std::u16string& secondary_button_menu_text) {
  JNIEnv* env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jstring> jsecondary_button_menu_text =
      base::android::ConvertUTF16ToJavaString(env, secondary_button_menu_text);
  Java_MessageWrapper_setSecondaryButtonMenuText(env, java_message_wrapper_,
                                                 jsecondary_button_menu_text);
}

int MessageWrapper::GetIconResourceId() {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_MessageWrapper_getIconResourceId(env, java_message_wrapper_);
}

void MessageWrapper::SetIconResourceId(int resource_id) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_MessageWrapper_setIconResourceId(env, java_message_wrapper_,
                                        resource_id);
}

bool MessageWrapper::IsValidIcon() {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_MessageWrapper_isValidIcon(env, java_message_wrapper_);
}

void MessageWrapper::SetIcon(const SkBitmap& icon) {
  JNIEnv* env = base::android::AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> java_bitmap =
      gfx::ConvertToJavaBitmap(icon);
  Java_MessageWrapper_setIcon(env, java_message_wrapper_, java_bitmap);
}

void MessageWrapper::DisableIconTint() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_MessageWrapper_disableIconTint(env, java_message_wrapper_);
}

int MessageWrapper::GetSecondaryIconResourceId() {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_MessageWrapper_getSecondaryIconResourceId(env,
                                                        java_message_wrapper_);
}

void MessageWrapper::SetSecondaryIconResourceId(int resource_id) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_MessageWrapper_setSecondaryIconResourceId(env, java_message_wrapper_,
                                                 resource_id);
}

void MessageWrapper::SetSecondaryActionCallback(base::OnceClosure callback) {
  secondary_action_callback_ = std::move(callback);
}

void MessageWrapper::SetDuration(long customDuration) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_MessageWrapper_setDuration(env, java_message_wrapper_, customDuration);
}

void MessageWrapper::SetActionClick(base::OnceClosure callback) {
  action_callback_ = std::move(callback);
}

void MessageWrapper::SetDismissCallback(DismissCallback callback) {
  dismiss_callback_ = std::move(callback);
}

void MessageWrapper::HandleActionClick(JNIEnv* env) {
  if (!action_callback_.is_null())
    std::move(action_callback_).Run();
}

void MessageWrapper::HandleSecondaryActionClick(JNIEnv* env) {
  if (!secondary_action_callback_.is_null())
    std::move(secondary_action_callback_).Run();
}

void MessageWrapper::HandleDismissCallback(JNIEnv* env, int dismiss_reason) {
  // Make sure message dismissed callback is called exactly once.
  message_enqueued_ = false;
  Java_MessageWrapper_clearNativePtr(env, java_message_wrapper_);
  if (!dismiss_callback_.is_null())
    std::move(dismiss_callback_)
        .Run(static_cast<DismissReason>(dismiss_reason));
  // Dismiss callback typically deletes the instance of MessageWrapper,
  // invalidating |this| pointer. Don't call any methods after dismiss_callback_
  // is invoked.
}

const base::android::JavaRef<jobject>& MessageWrapper::GetJavaMessageWrapper()
    const {
  return java_message_wrapper_;
}

void MessageWrapper::SetMessageEnqueued(
    const base::android::JavaRef<jobject>& java_window_android) {
  message_enqueued_ = true;
  java_window_android_ = java_window_android;
}

}  // namespace messages

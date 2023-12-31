// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_VIEW_PUBLIC_CWV_TRUSTED_VAULT_UTILS_H_
#define IOS_WEB_VIEW_PUBLIC_CWV_TRUSTED_VAULT_UTILS_H_

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Possible states of the trusted vault. Keep in sync with
// syncer::TrustedVaultDeviceRegistrationStateForUMA.
typedef NS_ENUM(NSInteger, CWVTrustedVaultState) {
  CWVTrustedVaultStateAlreadyRegistered = 0,
  CWVTrustedVaultStateLocalKeysAreStale,
  CWVTrustedVaultStateThrottledClientSide,
  CWVTrustedVaultStateAttemptingRegistrationWithNewKeyPair,
  CWVTrustedVaultStateAttemptingRegistrationWithExistingKeyPair,
  CWVTrustedVaultStateAttemptingRegistrationWithPersistentAuthError,
};

// Utility methods for trusted vault.
@interface CWVTrustedVaultUtils : NSObject

// Call to log to UMA when trusted vault state changes.
// TODO(crbug.com/1266130): See if these functions can be implemented by a
// CWVTrustedVaultObserver instead.
+ (void)logTrustedVaultDidUpdateState:(CWVTrustedVaultState)state;

// Call to log to UMA when trusted vault receives a http status code.
// TODO(crbug.com/1266130): See if these functions can be implemented by a
// CWVTrustedVaultObserver instead.
+ (void)logTrustedVaultDidReceiveHTTPStatusCode:(NSInteger)statusCode;

// Call to log to UMA when trusted vault fails key distribution.
// TODO(crbug.com/1266130): See if these functions can be implemented by a
// CWVTrustedVaultObserver instead.
+ (void)logTrustedVaultDidFailKeyDistribution:(NSError*)error;

@end

NS_ASSUME_NONNULL_END

#endif  // IOS_WEB_VIEW_PUBLIC_CWV_TRUSTED_VAULT_UTILS_H_

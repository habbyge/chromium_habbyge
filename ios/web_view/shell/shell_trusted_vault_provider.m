// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web_view/shell/shell_trusted_vault_provider.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@implementation ShellTrustedVaultProvider

#pragma mark - CWVTrustedVaultProvider

- (void)addTrustedVaultObserver:(CWVTrustedVaultObserver*)observer {
  // No op.
}

- (void)removeTrustedVaultObserver:(CWVTrustedVaultObserver*)observer {
  // No op.
}

- (void)fetchKeysForIdentity:(CWVIdentity*)identity
                  completion:(void (^)(NSArray<NSData*>* _Nullable,
                                       NSError* _Nullable))completion {
  NSError* error = [NSError errorWithDomain:@"org.chromium.ios-web-view-shell"
                                       code:-1
                                   userInfo:nil];
  completion(nil, error);
}

- (void)markLocalKeysAsStaleForIdentity:(CWVIdentity*)identity
                             completion:
                                 (void (^)(NSError* _Nullable))completion {
  NSError* error = [NSError errorWithDomain:@"org.chromium.ios-web-view-shell"
                                       code:-1
                                   userInfo:nil];
  completion(error);
}

- (void)isRecoverabilityDegradedForIdentity:(CWVIdentity*)identity
                                 completion:(void (^)(BOOL, NSError* _Nullable))
                                                completion {
  NSError* error = [NSError errorWithDomain:@"org.chromium.ios-web-view-shell"
                                       code:-1
                                   userInfo:nil];
  completion(NO, error);
}

@end

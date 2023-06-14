// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/authentication/signin_sync/signin_sync_mediator.h"

#import "ios/chrome/browser/signin/authentication_service.h"
#import "ios/chrome/browser/signin/chrome_account_manager_service_observer_bridge.h"
#import "ios/chrome/browser/ui/authentication/authentication_flow.h"
#import "ios/chrome/browser/ui/authentication/enterprise/enterprise_utils.h"
#import "ios/chrome/browser/ui/authentication/signin/user_signin/logging/first_run_signin_logger.h"
#import "ios/chrome/browser/ui/authentication/signin/user_signin/logging/user_signin_logger.h"
#import "ios/chrome/browser/ui/authentication/signin_sync/signin_sync_consumer.h"
#import "ios/public/provider/chrome/browser/signin/chrome_identity.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface SigninSyncMediator () <ChromeAccountManagerServiceObserver> {
  std::unique_ptr<ChromeAccountManagerServiceObserverBridge>
      _accountManagerServiceObserver;
}

// Logger used to record sign in metrics.
@property(nonatomic, strong) UserSigninLogger* logger;
// Account manager service to retrieve Chrome identities.
@property(nonatomic, assign) ChromeAccountManagerService* accountManagerService;
// Authentication service for sign in.
@property(nonatomic, assign) AuthenticationService* authenticationService;

@end

@implementation SigninSyncMediator

- (instancetype)initWithAccountManagerService:
                    (ChromeAccountManagerService*)accountManagerService
                        authenticationService:
                            (AuthenticationService*)authenticationService {
  self = [super init];
  if (self) {
    DCHECK(accountManagerService);
    DCHECK(authenticationService);

    _accountManagerService = accountManagerService;
    _authenticationService = authenticationService;
    _accountManagerServiceObserver =
        std::make_unique<ChromeAccountManagerServiceObserverBridge>(
            self, _accountManagerService);

    _logger = [[FirstRunSigninLogger alloc]
          initWithAccessPoint:signin_metrics::AccessPoint::
                                  ACCESS_POINT_START_PAGE
                  promoAction:signin_metrics::PromoAction::
                                  PROMO_ACTION_NO_SIGNIN_PROMO
        accountManagerService:accountManagerService];

    [_logger logSigninStarted];
  }
  return self;
}

- (void)dealloc {
  DCHECK(!self.accountManagerService);
}

- (void)disconnect {
  [self.logger disconnect];
  self.accountManagerService = nullptr;
  _accountManagerServiceObserver.reset();
}

- (void)startSignInWithAuthenticationFlow:
            (AuthenticationFlow*)authenticationFlow
                               completion:(ProceduralBlock)completion {
  [self.consumer setUIEnabled:NO];
  __weak __typeof(self) weakSelf = self;
  [authenticationFlow startSignInWithCompletion:^(BOOL success) {
    [weakSelf.consumer setUIEnabled:YES];
    if (!success)
      return;
    [weakSelf.logger logSigninCompletedWithResult:SigninCoordinatorResultSuccess
                                     addedAccount:weakSelf.addedAccount
                            advancedSettingsShown:NO];
    if (completion)
      completion();
  }];
}

#pragma mark - Properties

- (void)setSelectedIdentity:(ChromeIdentity*)selectedIdentity {
  if ([self.selectedIdentity isEqual:selectedIdentity])
    return;
  // nil is allowed only if there is no other identity.
  DCHECK(selectedIdentity || !self.accountManagerService->HasIdentities());
  _selectedIdentity = selectedIdentity;

  [self updateConsumerIdentity];
}

- (void)setConsumer:(id<SigninSyncConsumer>)consumer {
  if (consumer == _consumer)
    return;
  _consumer = consumer;

  [self updateConsumerIdentity];
}

#pragma mark - ChromeAccountManagerServiceObserver

- (void)identityListChanged {
  if (!self.accountManagerService) {
    return;
  }

  if (!self.selectedIdentity ||
      !self.accountManagerService->IsValidIdentity(self.selectedIdentity)) {
    self.selectedIdentity = self.accountManagerService->GetDefaultIdentity();
  }
}

- (void)identityChanged:(ChromeIdentity*)identity {
  if ([self.selectedIdentity isEqual:identity]) {
    [self updateConsumerIdentity];
  }
}

#pragma mark - Private

// Updates the identity displayed by the consumer.
- (void)updateConsumerIdentity {
  if (!self.selectedIdentity) {
    [self.consumer noIdentityAvailable];
  } else {
    UIImage* avatar = self.accountManagerService->GetIdentityAvatarWithIdentity(
        self.selectedIdentity, IdentityAvatarSize::DefaultLarge);
    [self.consumer
        setSelectedIdentityUserName:self.selectedIdentity.userFullName
                              email:self.selectedIdentity.userEmail
                          givenName:self.selectedIdentity.userGivenName
                             avatar:avatar];
  }
}

@end

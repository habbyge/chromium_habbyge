// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.firstrun;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.accounts.Account;
import android.app.Activity;
import android.os.Bundle;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.Robolectric;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.multidex.ShadowMultiDex;

import org.chromium.base.metrics.UmaRecorder;
import org.chromium.base.metrics.UmaRecorderHolder;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.net.spdyproxy.DataReductionProxySettings;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.signin.services.IdentityServicesProvider;
import org.chromium.chrome.test.util.browser.Features;
import org.chromium.chrome.test.util.browser.signin.AccountManagerTestRule;
import org.chromium.components.signin.ChildAccountStatus;
import org.chromium.components.signin.identitymanager.ConsentLevel;
import org.chromium.components.signin.identitymanager.IdentityManager;

/**
 * Tests FirstRunFlowSequencer which contains the core logic of what should be shown during the
 * first run.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE, shadows = {ShadowMultiDex.class})
public class FirstRunFlowSequencerTest {
    private static final String ADULT_ACCOUNT = "adult.account@gmail.com";
    private static final Account CHILD_ACCOUNT =
            AccountManagerTestRule.createChildAccount(/*baseName=*/"account@gmail.com");

    @Rule
    public TestRule mFeaturesProcessorRule = new Features.JUnitProcessor();

    @Rule
    public final TestRule mCommandLindFlagRule = CommandLineFlags.getTestRule();

    @Rule
    public final MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Rule
    public final AccountManagerTestRule mAccountManagerTestRule = new AccountManagerTestRule();

    /**
     * Testing version of FirstRunFlowSequencer that allows us to override all needed checks.
     */
    private static class TestFirstRunFlowSequencerDelegate
            extends FirstRunFlowSequencer.FirstRunFlowSequencerDelegate {
        public boolean isSyncAllowed;
        public boolean shouldSkipFirstUseHints;
        public boolean shouldShowSearchEnginePage;

        @Override
        public boolean shouldShowSearchEnginePage() {
            return shouldShowSearchEnginePage;
        }

        @Override
        public boolean isSyncAllowed() {
            return isSyncAllowed;
        }

        @Override
        public boolean shouldSkipFirstUseHints(Activity activity) {
            return shouldSkipFirstUseHints;
        }
    }

    private static class TestFirstRunFlowSequencer extends FirstRunFlowSequencer {
        public Bundle returnedBundle;
        public boolean calledOnFlowIsKnown;
        public boolean calledSetFirstRunFlowSignInComplete;

        public TestFirstRunFlowSequencer(Activity activity) {
            super(activity);
        }

        @Override
        public void onFlowIsKnown(Bundle freProperties) {
            calledOnFlowIsKnown = true;
            if (freProperties != null) updateFirstRunProperties(freProperties);
            returnedBundle = freProperties;
        }

        @Override
        protected void setFirstRunFlowSignInComplete() {
            calledSetFirstRunFlowSignInComplete = true;
        }
    }

    @Mock
    private DataReductionProxySettings mDataReductionProxySettingsMock;

    @Mock
    private UmaRecorder mUmaRecorderMock;

    @Mock
    private IdentityManager mIdentityManagerMock;

    private ActivityController<Activity> mActivityController;
    private TestFirstRunFlowSequencer mSequencer;
    private TestFirstRunFlowSequencerDelegate mDelegate;

    @Before
    public void setUp() {
        UmaRecorderHolder.setNonNativeDelegate(mUmaRecorderMock);
        Profile.setLastUsedProfileForTesting(mock(Profile.class));
        IdentityServicesProvider.setInstanceForTests(mock(IdentityServicesProvider.class));
        when(IdentityServicesProvider.get().getIdentityManager(Profile.getLastUsedRegularProfile()))
                .thenReturn(mIdentityManagerMock);
        when(mIdentityManagerMock.hasPrimaryAccount(ConsentLevel.SYNC)).thenReturn(false);

        // Show data reduction page by default.
        when(mDataReductionProxySettingsMock.isDataReductionProxyManaged()).thenReturn(false);
        when(mDataReductionProxySettingsMock.isDataReductionProxyFREPromoAllowed())
                .thenReturn(true);
        DataReductionProxySettings.setInstanceForTesting(mDataReductionProxySettingsMock);

        mActivityController = Robolectric.buildActivity(Activity.class);
        Activity activity = mActivityController.setup().get();
        mDelegate = new TestFirstRunFlowSequencerDelegate();
        FirstRunFlowSequencer.setDelegateForTesting(mDelegate);
        mSequencer = new TestFirstRunFlowSequencer(activity);
    }

    @After
    public void tearDown() {
        mActivityController.pause().stop().destroy();
        FirstRunFlowSequencer.setDelegateForTesting(null);
        UmaRecorderHolder.resetForTesting();
    }

    @Test
    @Feature({"FirstRun"})
    public void testStandardFlowTosNotSeen() {
        when(mDataReductionProxySettingsMock.isDataReductionProxyFREPromoAllowed())
                .thenReturn(false);
        mDelegate.isSyncAllowed = true;
        mDelegate.shouldSkipFirstUseHints = false;

        mSequencer.start();

        verifyNumberOfAccountsRecorded(0);
        assertTrue(mSequencer.calledOnFlowIsKnown);
        assertFalse(mSequencer.calledSetFirstRunFlowSignInComplete);

        Bundle bundle = mSequencer.returnedBundle;
        assertTrue(bundle.getBoolean(FirstRunActivityBase.SHOW_SYNC_CONSENT_PAGE));
        assertFalse(bundle.getBoolean(FirstRunActivityBase.SHOW_DATA_REDUCTION_PAGE));
        assertFalse(bundle.getBoolean(FirstRunActivityBase.SHOW_SEARCH_ENGINE_PAGE));
        assertEquals(ChildAccountStatus.NOT_CHILD,
                bundle.getInt(SyncConsentFirstRunFragment.CHILD_ACCOUNT_STATUS));
        assertEquals(4, bundle.size());
    }

    @Test
    @Feature({"FirstRun"})
    public void testStandardFlowOneChildAccount() {
        mAccountManagerTestRule.addAccount(CHILD_ACCOUNT);
        when(mDataReductionProxySettingsMock.isDataReductionProxyFREPromoAllowed())
                .thenReturn(false);
        mDelegate.isSyncAllowed = true;
        mDelegate.shouldSkipFirstUseHints = false;

        mSequencer.start();

        verifyNumberOfAccountsRecorded(1);
        assertTrue(mSequencer.calledOnFlowIsKnown);
        assertTrue(mSequencer.calledSetFirstRunFlowSignInComplete);

        Bundle bundle = mSequencer.returnedBundle;
        assertTrue(bundle.getBoolean(FirstRunActivityBase.SHOW_SYNC_CONSENT_PAGE));
        assertFalse(bundle.getBoolean(FirstRunActivityBase.SHOW_DATA_REDUCTION_PAGE));
        assertFalse(bundle.getBoolean(FirstRunActivityBase.SHOW_SEARCH_ENGINE_PAGE));
        assertEquals(ChildAccountStatus.REGULAR_CHILD,
                bundle.getInt(SyncConsentFirstRunFragment.CHILD_ACCOUNT_STATUS));
        assertEquals(4, bundle.size());
    }

    @Test
    @Feature({"FirstRun"})
    public void testStandardFlowShowDataReductionPage() {
        mAccountManagerTestRule.addAccount(ADULT_ACCOUNT);
        mAccountManagerTestRule.addAccount("second.test.account@gmail.com");
        mDelegate.isSyncAllowed = true;
        mDelegate.shouldSkipFirstUseHints = false;
        mDelegate.shouldShowSearchEnginePage = false;

        mSequencer.start();

        verifyNumberOfAccountsRecorded(2);
        assertTrue(mSequencer.calledOnFlowIsKnown);
        assertFalse(mSequencer.calledSetFirstRunFlowSignInComplete);

        Bundle bundle = mSequencer.returnedBundle;
        assertTrue(bundle.getBoolean(FirstRunActivityBase.SHOW_SYNC_CONSENT_PAGE));
        assertTrue(bundle.getBoolean(FirstRunActivityBase.SHOW_DATA_REDUCTION_PAGE));
        assertFalse(bundle.getBoolean(FirstRunActivityBase.SHOW_SEARCH_ENGINE_PAGE));
        assertEquals(ChildAccountStatus.NOT_CHILD,
                bundle.getInt(SyncConsentFirstRunFragment.CHILD_ACCOUNT_STATUS));
        assertEquals(4, bundle.size());
    }

    @Test
    @Feature({"FirstRun"})
    public void testStandardFlowShowSearchEnginePage() {
        mDelegate.isSyncAllowed = true;
        mDelegate.shouldSkipFirstUseHints = false;
        mDelegate.shouldShowSearchEnginePage = true;

        mSequencer.start();

        verifyNumberOfAccountsRecorded(0);
        assertTrue(mSequencer.calledOnFlowIsKnown);
        assertFalse(mSequencer.calledSetFirstRunFlowSignInComplete);

        Bundle bundle = mSequencer.returnedBundle;
        assertTrue(bundle.getBoolean(FirstRunActivityBase.SHOW_SYNC_CONSENT_PAGE));
        assertTrue(bundle.getBoolean(FirstRunActivityBase.SHOW_DATA_REDUCTION_PAGE));
        assertTrue(bundle.getBoolean(FirstRunActivityBase.SHOW_SEARCH_ENGINE_PAGE));
        assertEquals(ChildAccountStatus.NOT_CHILD,
                bundle.getInt(SyncConsentFirstRunFragment.CHILD_ACCOUNT_STATUS));
        assertEquals(4, bundle.size());
    }

    @Test
    @Feature({"FirstRun"})
    @CommandLineFlags.Add({ChromeSwitches.FORCE_ENABLE_SIGNIN_FRE})
    public void testFlowHideSyncConsentPageWhenUserIsNotSignedIn() {
        when(mIdentityManagerMock.hasPrimaryAccount(ConsentLevel.SIGNIN)).thenReturn(false);
        mDelegate.isSyncAllowed = true;
        mDelegate.shouldSkipFirstUseHints = false;
        mDelegate.shouldShowSearchEnginePage = false;

        mSequencer.start();

        verifyNumberOfAccountsRecorded(0);
        assertTrue(mSequencer.calledOnFlowIsKnown);
        assertFalse(mSequencer.calledSetFirstRunFlowSignInComplete);
        final Bundle bundle = mSequencer.returnedBundle;
        assertFalse(bundle.getBoolean(FirstRunActivityBase.SHOW_SYNC_CONSENT_PAGE));
        assertTrue(bundle.getBoolean(FirstRunActivityBase.SHOW_DATA_REDUCTION_PAGE));
        assertFalse(bundle.getBoolean(FirstRunActivityBase.SHOW_SEARCH_ENGINE_PAGE));
        assertEquals(ChildAccountStatus.NOT_CHILD,
                bundle.getInt(SyncConsentFirstRunFragment.CHILD_ACCOUNT_STATUS));
        assertEquals(4, bundle.size());
    }

    @Test
    @Feature({"FirstRun"})
    @CommandLineFlags.Add({ChromeSwitches.FORCE_ENABLE_SIGNIN_FRE})
    public void testFlowShowSyncConsentPageWhenUserIsSignedIn() {
        mAccountManagerTestRule.addAccount(ADULT_ACCOUNT);
        when(mIdentityManagerMock.hasPrimaryAccount(ConsentLevel.SIGNIN)).thenReturn(true);
        mDelegate.isSyncAllowed = true;
        mDelegate.shouldSkipFirstUseHints = false;
        mDelegate.shouldShowSearchEnginePage = false;

        mSequencer.start();

        verifyNumberOfAccountsRecorded(1);
        assertTrue(mSequencer.calledOnFlowIsKnown);
        assertFalse(mSequencer.calledSetFirstRunFlowSignInComplete);
        final Bundle bundle = mSequencer.returnedBundle;
        assertTrue(bundle.getBoolean(FirstRunActivityBase.SHOW_SYNC_CONSENT_PAGE));
        assertTrue(bundle.getBoolean(FirstRunActivityBase.SHOW_DATA_REDUCTION_PAGE));
        assertFalse(bundle.getBoolean(FirstRunActivityBase.SHOW_SEARCH_ENGINE_PAGE));
        assertEquals(ChildAccountStatus.NOT_CHILD,
                bundle.getInt(SyncConsentFirstRunFragment.CHILD_ACCOUNT_STATUS));
        assertEquals(4, bundle.size());
    }

    private void verifyNumberOfAccountsRecorded(int numberOfAccounts) {
        verify(mUmaRecorderMock)
                .recordExponentialHistogram("Signin.AndroidDeviceAccountsNumberWhenEnteringFRE",
                        numberOfAccounts, 1, 1_000_000, 50);
    }
}

// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.content_creation.reactions;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.os.Build;
import android.view.View;

import androidx.fragment.app.FragmentActivity;

import org.chromium.base.Callback;
import org.chromium.chrome.browser.content_creation.reactions.LightweightReactionsMediator.GifGeneratorHost;
import org.chromium.chrome.browser.content_creation.reactions.scene.SceneCoordinator;
import org.chromium.chrome.browser.content_creation.reactions.toolbar.ToolbarControlsDelegate;
import org.chromium.chrome.browser.content_creation.reactions.toolbar.ToolbarCoordinator;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.share.BaseScreenshotCoordinator;
import org.chromium.chrome.browser.share.ChromeShareExtras;
import org.chromium.chrome.browser.share.share_sheet.ChromeOptionShareCallback;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.share.ShareParams;
import org.chromium.components.content_creation.reactions.ReactionMetadata;
import org.chromium.components.content_creation.reactions.ReactionService;
import org.chromium.components.image_fetcher.ImageFetcher;
import org.chromium.components.image_fetcher.ImageFetcherConfig;
import org.chromium.components.image_fetcher.ImageFetcherFactory;

import java.text.DateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Responsible for reactions main UI and its subcomponents.
 */
public class LightweightReactionsCoordinatorImpl extends BaseScreenshotCoordinator
        implements LightweightReactionsCoordinator, ToolbarControlsDelegate {
    private static final String GIF_MIME_TYPE = "image/gif";

    private final ReactionService mReactionService;
    private final LightweightReactionsMediator mMediator;
    private final LightweightReactionsDialog mDialog;
    private final SceneCoordinator mSceneCoordinator;

    private List<ReactionMetadata> mAvailableReactions;
    private Bitmap[] mThumbnails;
    private ToolbarCoordinator mToolbarCoordinator;

    private boolean mDialogViewCreated;
    private boolean mAssetsFetched;

    /**
     * Constructs a new LightweightReactionsCoordinatorImpl which initializes and displays the
     * Lightweight Reactions scene.
     *
     * @param activity The parent activity.
     * @param tab The Tab which contains the content to share.
     * @param shareUrl The URL associated with the screenshot.
     * @param chromeOptionShareCallback An interface to share sheet APIs.
     * @param sheetController The {@link BottomSheetController} for the current activity.
     * @param reactionService The {@link ReactionService} to use for Reaction operations.
     */
    public LightweightReactionsCoordinatorImpl(Activity activity, Tab tab, String shareUrl,
            ChromeOptionShareCallback chromeOptionShareCallback,
            BottomSheetController sheetController, ReactionService reactionService) {
        super(activity, tab, shareUrl, chromeOptionShareCallback, sheetController);
        mDialogViewCreated = false;
        mAssetsFetched = false;
        mReactionService = reactionService;

        Profile profile = Profile.fromWebContents(tab.getWebContents());
        ImageFetcher imageFetcher = ImageFetcherFactory.createImageFetcher(
                ImageFetcherConfig.DISK_CACHE_ONLY, profile.getProfileKey());
        mMediator = new LightweightReactionsMediator(imageFetcher);

        mDialog = new LightweightReactionsDialog();
        mSceneCoordinator = new SceneCoordinator(activity, mMediator);

        mReactionService.getReactions((reactions) -> {
            assert reactions.size() > 0;
            mAvailableReactions = reactions;
            mMediator.fetchAssetsAndGetThumbnails(reactions, this::onAssetsFetched);
        });
    }

    /**
     * Creates the toolbar coordinator after the root dialog view is ready, then attempts to finish
     * the initialization of the feature.
     * @param view The root {@link View} of the dialog.
     */
    private void onViewCreated(View view) {
        mDialogViewCreated = true;
        mToolbarCoordinator = new ToolbarCoordinator(view, this, mSceneCoordinator);
        mSceneCoordinator.addReactionInDefaultLocation(mAvailableReactions.get(0));
        maybeFinishInitialization();
    }

    /**
     * Stores the thumbnails of the reactions, then attempts to finish the initialization of the
     * feature.
     * @param thumbnails The list of thumbnails. The order must be the same as
     *                   {@code mAvailableReactions}.
     */
    private void onAssetsFetched(Bitmap[] thumbnails) {
        assert thumbnails != null;
        assert thumbnails.length == mAvailableReactions.size();

        mAssetsFetched = true;
        mThumbnails = thumbnails;
        maybeFinishInitialization();
    }

    /**
     * Performs the remaining initialization for the feature, namely creating the toolbar carousel
     * for the reactions, hooking up the remaining event handlers, and dismissing the loading view.
     *
     * <p><b>Note:</b> The dialog view must be ready and the assets must have been fetched. If
     * either is missing, this is a no-op.
     */
    private void maybeFinishInitialization() {
        if (!mDialogViewCreated || !mAssetsFetched) {
            // Wait until both operations have completed.
            return;
        }
        mToolbarCoordinator.initReactions(mAvailableReactions, mThumbnails);
    }

    /**
     * Creates the share sheet title based on a localized title and the current date formatted for
     * the user's preferred locale.
     */
    private String getShareSheetTitle() {
        Date now = new Date(System.currentTimeMillis());
        String currentDateString =
                DateFormat.getDateInstance(DateFormat.SHORT, getPreferredLocale()).format(now);
        // TODO(crbug.com/1213923): get final string from UX, and localize it here.
        return "Generated GIF " + currentDateString;
    }

    /**
     * Retrieves the user's preferred locale from the app's configurations.
     */
    private Locale getPreferredLocale() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.N
                ? mActivity.getResources().getConfiguration().getLocales().get(0)
                : mActivity.getResources().getConfiguration().locale;
    }

    // LightweightReactionsCoordinator implementation.
    @Override
    public void showDialog() {
        FragmentActivity fragmentActivity = (FragmentActivity) mActivity;
        mDialog.show(fragmentActivity.getSupportFragmentManager(), null);
    }

    // BaseScreenshotCoordinator implementation.
    @Override
    protected void handleScreenshot() {
        mDialog.init(mScreenshot, mSceneCoordinator, this::onViewCreated);
        showDialog();
    }

    // ToolbarControlsDelegate implementation.
    @Override
    public void cancelButtonTapped() {
        mDialog.dismiss();
    }

    @Override
    public void doneButtonTapped() {
        GifGeneratorHost gifHost = new GifGeneratorHost() {
            @Override
            public void prepareFrame(Callback<Void> cb) {
                mSceneCoordinator.stepReactions(cb);
            }

            @Override
            public void drawFrame(Canvas canvas) {
                mSceneCoordinator.drawScene(canvas);
            }
        };

        mSceneCoordinator.clearSelection();
        mMediator.generateGif(gifHost, mSceneCoordinator.getFrameCount(),
                mSceneCoordinator.getWidth(), mSceneCoordinator.getHeight(), (imageUri) -> {
                    final String sheetTitle = getShareSheetTitle();
                    ShareParams params =
                            new ShareParams.Builder(mTab.getWindowAndroid(), sheetTitle, /*url=*/"")
                                    .setFileUris(
                                            new ArrayList<>(Collections.singletonList(imageUri)))
                                    .setFileContentType(GIF_MIME_TYPE)
                                    .build();

                    long shareStartTime = System.currentTimeMillis();
                    ChromeShareExtras extras =
                            new ChromeShareExtras.Builder().setSkipPageSharingActions(true).build();

                    // Dismiss current dialog before showing the share sheet.
                    mDialog.dismiss();
                    mChromeOptionShareCallback.showShareSheet(params, extras, shareStartTime);
                });
    }
}

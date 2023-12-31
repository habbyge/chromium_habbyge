// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_EXTENSIONS_EXTENSION_ACTION_VIEW_CONTROLLER_H_
#define CHROME_BROWSER_UI_EXTENSIONS_EXTENSION_ACTION_VIEW_CONTROLLER_H_

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/extensions/extension_action_icon_factory.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/ui/toolbar/toolbar_action_view_controller.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_host_observer.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_id.h"
#include "ui/gfx/image/image.h"

class Browser;
class ExtensionActionPlatformDelegate;
class GURL;
class IconWithBadgeImageSource;
class ExtensionsContainer;

namespace extensions {
class Command;
class Extension;
class ExtensionAction;
class ExtensionRegistry;
class ExtensionViewHost;
}

// The platform-independent controller for an ExtensionAction that is shown on
// the toolbar (such as a page or browser action).
// Since this class doesn't own the extension or extension action in question,
// be sure to check for validity using ExtensionIsValid() before using those
// members (see also comments above ExtensionIsValid()).
class ExtensionActionViewController
    : public ToolbarActionViewController,
      public ExtensionActionIconFactory::Observer,
      public extensions::ExtensionContextMenuModel::PopupDelegate,
      public extensions::ExtensionHostObserver {
 public:
  // The different options for showing a popup.
  enum PopupShowAction { SHOW_POPUP, SHOW_POPUP_AND_INSPECT };

  static std::unique_ptr<ExtensionActionViewController> Create(
      const extensions::ExtensionId& extension_id,
      Browser* browser,
      ExtensionsContainer* extensions_container);

  // Returns whether any of `actions` given have access to the `web_contents`.
  static bool AnyActionHasCurrentSiteAccess(
      const std::vector<std::unique_ptr<ToolbarActionViewController>>& actions,
      content::WebContents* web_contents);

  ExtensionActionViewController(const ExtensionActionViewController&) = delete;
  ExtensionActionViewController& operator=(
      const ExtensionActionViewController&) = delete;

  ~ExtensionActionViewController() override;

  // ToolbarActionViewController:
  std::string GetId() const override;
  void SetDelegate(ToolbarActionViewDelegate* delegate) override;
  gfx::Image GetIcon(content::WebContents* web_contents,
                     const gfx::Size& size) override;
  std::u16string GetActionName() const override;
  std::u16string GetAccessibleName(
      content::WebContents* web_contents) const override;
  std::u16string GetTooltip(content::WebContents* web_contents) const override;
  PageInteractionStatus GetPageInteractionStatus(
      content::WebContents* web_contents) const override;
  bool IsEnabled(content::WebContents* web_contents) const override;
  bool IsShowingPopup() const override;
  void HidePopup() override;
  gfx::NativeView GetPopupNativeView() override;
  ui::MenuModel* GetContextMenu() override;
  void OnContextMenuShown() override;
  void OnContextMenuClosed() override;
  bool ExecuteAction(bool by_user, InvocationSource source) override;
  void UpdateState() override;
  void RegisterCommand() override;
  void UnregisterCommand() override;

  // ExtensionContextMenuModel::PopupDelegate:
  void InspectPopup() override;

  // Populates |command| with the command associated with |extension|, if one
  // exists. Returns true if |command| was populated.
  bool GetExtensionCommand(extensions::Command* command) const;

  // Returns true if this controller can handle accelerators (i.e., keyboard
  // commands) on the currently-active WebContents.
  // This must only be called if the extension has an associated command.
  // TODO(devlin): Move accelerator logic out of the platform delegate and into
  // this class.
  bool CanHandleAccelerators() const;

  const extensions::Extension* extension() const { return extension_.get(); }
  Browser* browser() { return browser_; }
  extensions::ExtensionAction* extension_action() { return extension_action_; }
  const extensions::ExtensionAction* extension_action() const {
    return extension_action_;
  }
  ToolbarActionViewDelegate* view_delegate() { return view_delegate_; }

  std::unique_ptr<IconWithBadgeImageSource> GetIconImageSourceForTesting(
      content::WebContents* web_contents,
      const gfx::Size& size);
  bool HasBeenBlockedForTesting(content::WebContents* web_contents) const;

 private:
  // New instances should be instantiated with Create().
  ExtensionActionViewController(
      scoped_refptr<const extensions::Extension> extension,
      Browser* browser,
      extensions::ExtensionAction* extension_action,
      extensions::ExtensionRegistry* extension_registry,
      ExtensionsContainer* extensions_container);

  // ExtensionActionIconFactory::Observer:
  void OnIconUpdated() override;

  // ExtensionHostObserver:
  void OnExtensionHostDestroyed(extensions::ExtensionHost* host) override;

  // Checks if the associated |extension| is still valid by checking its
  // status in the registry. Since the OnExtensionUnloaded() notifications are
  // not in a deterministic order, it's possible that the view tries to refresh
  // itself before we're notified to remove it.
  bool ExtensionIsValid() const;

  // In some cases (such as when an action is shown in a menu), a substitute
  // ToolbarActionViewController should be used for showing popups. This
  // returns the preferred controller.
  ExtensionActionViewController* GetPreferredPopupViewController();

  // Executes the extension action with |show_action|. If
  // |grant_tab_permissions| is true, this will grant the extension active tab
  // permissions. Only do this if this was done through a user action (and not
  // e.g. an API). Returns true if a popup is shown.
  bool ExecuteAction(PopupShowAction show_action, bool grant_tab_permissions);

  // Begins the process of showing the popup for the extension action, given the
  // associated |popup_url|. |grant_tab_permissions| is true if active tab
  // permissions should be given to the extension; this is only true if the
  // popup is opened through a user action.
  // The popup may not be shown synchronously if the extension is hidden and
  // first needs to slide itself out.
  // Returns true if a popup will be shown.
  bool TriggerPopupWithUrl(PopupShowAction show_action,
                           const GURL& popup_url,
                           bool grant_tab_permissions);

  // Shows the popup with the given |host|.
  void ShowPopup(std::unique_ptr<extensions::ExtensionViewHost> host,
                 bool grant_tab_permissions,
                 PopupShowAction show_action);

  // Handles cleanup after the popup closes.
  void OnPopupClosed();

  // Returns the image source for the icon.
  std::unique_ptr<IconWithBadgeImageSource> GetIconImageSource(
      content::WebContents* web_contents,
      const gfx::Size& size);

  // Returns true if this extension uses the activeTab permission and would
  // probably be able to to access the given |url|. The actual checks when an
  // activeTab extension tries to run are a little more complicated and can be
  // seen in ExtensionActionRunner and ActiveTabPermissionGranter.
  // Note: The rare cases where this gets it wrong should only be for false
  // positives, where it reports that the extension wants access but it can't
  // actually be given access when it tries to run.
  bool HasActiveTabAndCanAccess(const GURL& url) const;

  // Returns true if this extension has been blocked on the given
  // |web_contents|.
  bool HasBeenBlocked(content::WebContents* web_contents) const;

  // The extension associated with the action we're displaying.
  scoped_refptr<const extensions::Extension> extension_;

  // The corresponding browser.
  Browser* const browser_;

  // The browser action this view represents. The ExtensionAction is not owned
  // by this class.
  extensions::ExtensionAction* const extension_action_;

  // The corresponding ExtensionsContainer on the toolbar.
  ExtensionsContainer* const extensions_container_;

  // The extension popup's host if the popup is visible; null otherwise.
  extensions::ExtensionViewHost* popup_host_;

  // The context menu model for the extension.
  std::unique_ptr<extensions::ExtensionContextMenuModel> context_menu_model_;

  // Our view delegate.
  ToolbarActionViewDelegate* view_delegate_;

  // The delegate to handle platform-specific implementations.
  std::unique_ptr<ExtensionActionPlatformDelegate> platform_delegate_;

  // The object that will be used to get the browser action icon for us.
  // It may load the icon asynchronously (in which case the initial icon
  // returned by the factory will be transparent), so we have to observe it for
  // updates to the icon.
  ExtensionActionIconFactory icon_factory_;

  // The associated ExtensionRegistry; cached for quick checking.
  extensions::ExtensionRegistry* extension_registry_;

  base::ScopedObservation<extensions::ExtensionHost,
                          extensions::ExtensionHostObserver>
      popup_host_observation_{this};

  base::WeakPtrFactory<ExtensionActionViewController> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_EXTENSIONS_EXTENSION_ACTION_VIEW_CONTROLLER_H_

// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/containers/adapters.h"
#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_group.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/bubble_menu_item_factory.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/browser/ui/views/chrome_typography.h"
#include "chrome/browser/ui/views/hover_button.h"
#include "chrome/browser/ui/views/tabs/color_picker_view.h"
#include "chrome/browser/ui/views/tabs/tab_group_editor_bubble_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_ink_drop_util.h"
#include "chrome/grit/generated_resources.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model_field.h"
#include "ui/base/models/image_model.h"
#include "ui/base/pointer/touch_ui_controller.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/range/range.h"
#include "ui/gfx/text_constants.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"

namespace {

std::unique_ptr<views::LabelButton> CreateMenuItem(
    int button_id,
    const std::u16string& name,
    views::Button::PressedCallback callback,
    const gfx::VectorIcon* icon = nullptr) {
  const auto* layout_provider = ChromeLayoutProvider::Get();
  const int horizontal_spacing = layout_provider->GetDistanceMetric(
      views::DISTANCE_RELATED_CONTROL_HORIZONTAL);
  const int vertical_spacing = layout_provider->GetDistanceMetric(
      views::DISTANCE_RELATED_CONTROL_VERTICAL);

  const gfx::Insets control_insets =
      ui::TouchUiController::Get()->touch_ui()
          ? gfx::Insets(5 * vertical_spacing / 4, horizontal_spacing)
          : gfx::Insets(vertical_spacing, horizontal_spacing);

  auto button = CreateBubbleMenuItem(button_id, name, callback, icon);
  button->SetBorder(views::CreateEmptyBorder(control_insets));

  return button;
}

class MenuItemFactory : public views::BubbleDialogModelHost::CustomViewFactory {
 public:
  MenuItemFactory(const std::u16string& name,
                  views::Button::PressedCallback callback,
                  const gfx::VectorIcon* icon = nullptr)
      : name_(std::move(name)), callback_(std::move(callback)), icon_(icon) {}
  ~MenuItemFactory() override = default;

  // views::BubbleDialogModelHost::CustomViewFactory:
  std::unique_ptr<views::View> CreateView() override {
    // TODO(pbos): See if dialog_model()->host()->Close(); can be handled by the
    // menu item itself (after calling callback_). All menu-item actions close
    // the dialog. We should be able to chain some calls if we have access to
    // DialogModelDelegate here.

    return CreateMenuItem(-1, name_, callback_, icon_);
  }

  views::BubbleDialogModelHost::FieldType GetFieldType() const override {
    return views::BubbleDialogModelHost::FieldType::kMenuItem;
  }

 private:
  const std::u16string name_;
  const views::Button::PressedCallback callback_;
  const gfx::VectorIcon* const icon_;
};

class TabGroupEditorBubbleDelegate : public ui::DialogModelDelegate {
 public:
  TabGroupEditorBubbleDelegate(const Browser* browser,
                               const tab_groups::TabGroupId& group)
      : browser_(browser), group_(group) {}

  TabGroupEditorBubbleDelegate(const TabGroupEditorBubbleDelegate&) = delete;
  TabGroupEditorBubbleDelegate& operator=(const TabGroupEditorBubbleDelegate&) =
      delete;

  ~TabGroupEditorBubbleDelegate() override = default;

  void NewTabInGroupPressed() {
    base::RecordAction(
        base::UserMetricsAction("TabGroups_TabGroupBubble_NewTabInGroup"));
    TabStripModel* const model = browser_->tab_strip_model();
    const auto tabs = model->group_model()->GetTabGroup(group_)->ListTabs();
    model->delegate()->AddTabAt(GURL(), tabs.end(), true, group_);
    // Close the widget to allow users to continue their work in their newly
    // created tab.
    dialog_model()->host()->Close();
  }

  void UngroupPressed(TabGroupHeader* header_view) {
    base::RecordAction(
        base::UserMetricsAction("TabGroups_TabGroupBubble_Ungroup"));
    if (header_view) {
      // TODO(pbos): See if this can be managed outside this dialog to prevent
      // upcasting to BubbleDialogModelHost.
      header_view->RemoveObserverFromWidget(
          static_cast<views::BubbleDialogModelHost*>(dialog_model()->host())
              ->GetWidget());
    }
    TabStripModel* const model = browser_->tab_strip_model();

    const gfx::Range tab_range =
        model->group_model()->GetTabGroup(group_)->ListTabs();

    std::vector<int> tabs;
    tabs.reserve(tab_range.length());
    for (auto i = tab_range.start(); i < tab_range.end(); ++i)
      tabs.push_back(i);

    model->RemoveFromGroup(tabs);
    // Close the widget because it is no longer applicable.
    dialog_model()->host()->Close();
  }

  void CloseGroupPressed() {
    base::RecordAction(
        base::UserMetricsAction("TabGroups_TabGroupBubble_CloseGroup"));
    browser_->tab_strip_model()->CloseAllTabsInGroup(group_);
    // Close the widget because it is no longer applicable.
    dialog_model()->host()->Close();
  }

  void MoveGroupToNewWindowPressed() {
    browser_->tab_strip_model()->delegate()->MoveGroupToNewWindow(group_);
    dialog_model()->host()->Close();
  }

  void SendFeedbackPressed() {
    base::RecordAction(
        base::UserMetricsAction("TabGroups_TabGroupBubble_SendFeedback"));
    chrome::ShowFeedbackPage(
        browser_, chrome::FeedbackSource::kFeedbackSourceDesktopTabGroups,
        /*description_template=*/std::string(),
        /*description_placeholder_text=*/std::string(),
        /*category_tag=*/std::string(),
        /*extra_diagnostics=*/std::string());
    dialog_model()->host()->Close();
  }

 private:
  const Browser* const browser_;
  const tab_groups::TabGroupId group_;
};

}  // namespace

// static
views::Widget* TabGroupEditorBubbleView::Show(
    const Browser* browser,
    const tab_groups::TabGroupId& group,
    TabGroupHeader* header_view,
    absl::optional<gfx::Rect> anchor_rect,
    views::View* anchor_view,
    bool stop_context_menu_propagation) {
  // TODO(pbos): Clean this duplicate implementation up. This is only here while
  // development of a DialogModel version of this bubble is in progress. This is
  // also only checked in so that development on DialogModel and
  // BubbleDialogModelHost can happen in chunks and be checked in instead of
  // landed as a gargantuan change.
  static constexpr bool kUseDialogModel = false;
  if (kUseDialogModel) {
    auto bubble_delegate_unique =
        std::make_unique<TabGroupEditorBubbleDelegate>(browser, group);
    TabGroupEditorBubbleDelegate* const bubble_delegate =
        bubble_delegate_unique.get();

    ui::DialogModel::Builder dialog_builder(std::move(bubble_delegate_unique));

    // TODO(pbos): This still does not include the title field, divider or color
    // picker.

    dialog_builder.OverrideShowCloseButton(false)
        .AddCustomField(
            std::make_unique<MenuItemFactory>(
                l10n_util::GetStringUTF16(
                    IDS_TAB_GROUP_HEADER_CXMENU_NEW_TAB_IN_GROUP),
                base::BindRepeating(
                    &TabGroupEditorBubbleDelegate::NewTabInGroupPressed,
                    base::Unretained(bubble_delegate)),
                &kNewTabInGroupIcon),
            TAB_GROUP_HEADER_CXMENU_NEW_TAB_IN_GROUP)
        .AddCustomField(
            std::make_unique<MenuItemFactory>(
                l10n_util::GetStringUTF16(IDS_TAB_GROUP_HEADER_CXMENU_UNGROUP),
                base::BindRepeating(
                    &TabGroupEditorBubbleDelegate::UngroupPressed,
                    base::Unretained(bubble_delegate), header_view),
                &kUngroupIcon),
            TAB_GROUP_HEADER_CXMENU_UNGROUP)
        .AddCustomField(
            std::make_unique<MenuItemFactory>(
                l10n_util::GetStringUTF16(
                    IDS_TAB_GROUP_HEADER_CXMENU_CLOSE_GROUP),
                base::BindRepeating(
                    &TabGroupEditorBubbleDelegate::CloseGroupPressed,
                    base::Unretained(bubble_delegate)),
                &kCloseGroupIcon),
            TAB_GROUP_HEADER_CXMENU_CLOSE_GROUP)
        .AddCustomField(
            std::make_unique<MenuItemFactory>(
                l10n_util::GetStringUTF16(
                    IDS_TAB_GROUP_HEADER_CXMENU_MOVE_GROUP_TO_NEW_WINDOW),
                base::BindRepeating(
                    &TabGroupEditorBubbleDelegate::MoveGroupToNewWindowPressed,
                    base::Unretained(bubble_delegate)),
                &kMoveGroupToNewWindowIcon),
            TAB_GROUP_HEADER_CXMENU_MOVE_GROUP_TO_NEW_WINDOW);

    // TODO(pbos): Add enabling/disabling of
    // TAB_GROUP_HEADER_CXMENU_MOVE_GROUP_TO_NEW_WINDOW item.

    if (base::FeatureList::IsEnabled(features::kTabGroupsFeedback)) {
      dialog_builder.AddCustomField(
          std::make_unique<MenuItemFactory>(
              l10n_util::GetStringUTF16(
                  IDS_TAB_GROUP_HEADER_CXMENU_SEND_FEEDBACK),
              base::BindRepeating(
                  &TabGroupEditorBubbleDelegate::SendFeedbackPressed,
                  base::Unretained(bubble_delegate))),
          TAB_GROUP_HEADER_CXMENU_FEEDBACK);
    }

    std::unique_ptr<ui::DialogModel> dialog_model = dialog_builder.Build();

    // If |header_view| is not null, use |header_view| as the |anchor_view|.
    auto bubble = std::make_unique<views::BubbleDialogModelHost>(
        std::move(dialog_model), header_view ? header_view : anchor_view,
        views::BubbleBorder::TOP_LEFT);
    if (anchor_rect)
      bubble->SetAnchorRect(*anchor_rect);
    views::BubbleDialogModelHost* const bubble_ptr = bubble.get();
    views::Widget* const widget =
        views::BubbleDialogDelegate::CreateBubble(std::move(bubble));
    bubble_ptr->set_adjust_if_offscreen(true);
    bubble_ptr->GetBubbleFrameView()->SetPreferredArrowAdjustment(
        views::BubbleFrameView::PreferredArrowAdjustment::kOffset);
    widget->Show();
    return widget;
  }

  // If |header_view| is not null, use |header_view| as the |anchor_view|.
  TabGroupEditorBubbleView* tab_group_editor_bubble_view =
      new TabGroupEditorBubbleView(
          browser, group, header_view ? header_view : anchor_view, anchor_rect,
          header_view, stop_context_menu_propagation);
  views::Widget* const widget =
      BubbleDialogDelegateView::CreateBubble(tab_group_editor_bubble_view);
  tab_group_editor_bubble_view->set_adjust_if_offscreen(true);
  tab_group_editor_bubble_view->GetBubbleFrameView()
      ->SetPreferredArrowAdjustment(
          views::BubbleFrameView::PreferredArrowAdjustment::kOffset);
  tab_group_editor_bubble_view->SizeToContents();
  widget->Show();
  return widget;
}

views::View* TabGroupEditorBubbleView::GetInitiallyFocusedView() {
  return title_field_;
}

gfx::Rect TabGroupEditorBubbleView::GetAnchorRect() const {
  // We want to avoid calling BubbleDialogDelegateView::GetAnchorRect() if
  // |anchor_rect_| has been set. This is because the default behavior uses the
  // anchor view's bounds and also updates |anchor_rect_| to the views bounds.
  // It does this so that the bubble does not jump when the anchoring view is
  // deleted.
  if (use_set_anchor_rect_)
    return anchor_rect().value();
  return BubbleDialogDelegateView::GetAnchorRect();
}

void TabGroupEditorBubbleView::AddedToWidget() {
  if (!move_menu_item_->GetEnabled()) {
    const SkColor disabled_color = move_menu_item_->GetCurrentTextColor();
    move_menu_item_->SetImageModel(
        views::Button::STATE_DISABLED,
        ui::ImageModel::FromVectorIcon(kMoveGroupToNewWindowIcon,
                                       disabled_color));
  }
}

TabGroupEditorBubbleView::TabGroupEditorBubbleView(
    const Browser* browser,
    const tab_groups::TabGroupId& group,
    views::View* anchor_view,
    absl::optional<gfx::Rect> anchor_rect,
    TabGroupHeader* header_view,
    bool stop_context_menu_propagation)
    : browser_(browser),
      group_(group),
      title_field_controller_(this),
      use_set_anchor_rect_(anchor_rect) {
  // |anchor_view| should always be defined as it will be used to source the
  // |anchor_widget_|.
  DCHECK(anchor_view);
  SetAnchorView(anchor_view);
  if (anchor_rect)
    SetAnchorRect(anchor_rect.value());

  set_margins(gfx::Insets());

  SetButtons(ui::DIALOG_BUTTON_NONE);
  SetModalType(ui::MODAL_TYPE_NONE);

  TabStripModel* const tab_strip_model = browser_->tab_strip_model();
  const std::u16string title = tab_strip_model->group_model()
                                   ->GetTabGroup(group_)
                                   ->visual_data()
                                   ->title();
  title_at_opening_ = title;
  SetCloseCallback(base::BindOnce(&TabGroupEditorBubbleView::OnBubbleClose,
                                  base::Unretained(this)));

  // Create view hierarchy.

  title_field_ =
      AddChildView(std::make_unique<TitleField>(stop_context_menu_propagation));
  title_field_->SetText(title);
  title_field_->SetAccessibleName(u"Group title");
  title_field_->SetPlaceholderText(
      l10n_util::GetStringUTF16(IDS_TAB_GROUP_HEADER_BUBBLE_TITLE_PLACEHOLDER));
  title_field_->set_controller(&title_field_controller_);
  title_field_->SetProperty(views::kElementIdentifierKey,
                            kEditorBubbleIdentifier);

  const tab_groups::TabGroupColorId initial_color_id = InitColorSet();
  color_selector_ = AddChildView(std::make_unique<ColorPickerView>(
      this, colors_, initial_color_id,
      base::BindRepeating(&TabGroupEditorBubbleView::UpdateGroup,
                          base::Unretained(this))));

  auto* const separator = AddChildView(std::make_unique<views::Separator>());

  views::ImageView* save_group_icon = nullptr;
  views::View* save_group_line_container = nullptr;
  views::Label* save_group_label = nullptr;

  if (base::FeatureList::IsEnabled(features::kTabGroupsSave)) {
    save_group_line_container = AddChildView(std::make_unique<views::View>());

    // The save_group_icon is put in differently than the rest because it
    // utilizes a different view (view::Label) that does not have an option to
    // take in an image like the other line items do.
    save_group_icon = save_group_line_container->AddChildView(
        std::make_unique<views::ImageView>(
            ui::ImageModel::FromVectorIcon(kSaveGroupIcon)));

    save_group_label =
        save_group_line_container->AddChildView(std::make_unique<views::Label>(
            l10n_util::GetStringUTF16(IDS_TAB_GROUP_HEADER_CXMENU_SAVE_GROUP)));
    save_group_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    save_group_toggle_ = save_group_line_container->AddChildView(
        std::make_unique<views::ToggleButton>(
            base::BindRepeating(&TabGroupEditorBubbleView::OnSaveTogglePressed,
                                base::Unretained(this))));

    bool is_saved =
        tab_strip_model->group_model()->GetTabGroup(group_)->IsSaved();
    save_group_toggle_->SetIsOn(is_saved);
  }

  auto* const new_tab_menu_item = AddChildView(CreateMenuItem(
      TAB_GROUP_HEADER_CXMENU_NEW_TAB_IN_GROUP,
      l10n_util::GetStringUTF16(IDS_TAB_GROUP_HEADER_CXMENU_NEW_TAB_IN_GROUP),
      base::BindRepeating(&TabGroupEditorBubbleView::NewTabInGroupPressed,
                          base::Unretained(this)),
      &kNewTabInGroupIcon));

  AddChildView(CreateMenuItem(
      TAB_GROUP_HEADER_CXMENU_UNGROUP,
      l10n_util::GetStringUTF16(IDS_TAB_GROUP_HEADER_CXMENU_UNGROUP),
      base::BindRepeating(&TabGroupEditorBubbleView::UngroupPressed,
                          base::Unretained(this), header_view),
      &kUngroupIcon));

  AddChildView(CreateMenuItem(
      TAB_GROUP_HEADER_CXMENU_CLOSE_GROUP,
      l10n_util::GetStringUTF16(IDS_TAB_GROUP_HEADER_CXMENU_CLOSE_GROUP),
      base::BindRepeating(&TabGroupEditorBubbleView::CloseGroupPressed,
                          base::Unretained(this)),
      &kCloseGroupIcon));

  move_menu_item_ = AddChildView(
      CreateMenuItem(TAB_GROUP_HEADER_CXMENU_MOVE_GROUP_TO_NEW_WINDOW,
                     l10n_util::GetStringUTF16(
                         IDS_TAB_GROUP_HEADER_CXMENU_MOVE_GROUP_TO_NEW_WINDOW),
                     base::BindRepeating(
                         &TabGroupEditorBubbleView::MoveGroupToNewWindowPressed,
                         base::Unretained(this)),
                     &kMoveGroupToNewWindowIcon));
  move_menu_item_->SetEnabled(
      tab_strip_model->count() !=
      tab_strip_model->group_model()->GetTabGroup(group_)->tab_count());

  if (base::FeatureList::IsEnabled(features::kTabGroupsFeedback)) {
    AddChildView(CreateMenuItem(
        TAB_GROUP_HEADER_CXMENU_FEEDBACK,
        l10n_util::GetStringUTF16(IDS_TAB_GROUP_HEADER_CXMENU_SEND_FEEDBACK),
        base::BindRepeating(&TabGroupEditorBubbleView::SendFeedbackPressed,
                            base::Unretained(this))));
  }

  // Setting up the layout.

  const gfx::Insets control_insets = new_tab_menu_item->GetInsets();
  const int vertical_spacing = control_insets.top();
  const int horizontal_spacing = control_insets.left();

  SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical)
      .SetInteriorMargin(gfx::Insets(vertical_spacing, 0));

  title_field_->SetProperty(views::kMarginsKey,
                            gfx::Insets(vertical_spacing, horizontal_spacing));

  color_selector_->SetProperty(views::kMarginsKey,
                               gfx::Insets(0, horizontal_spacing));

  separator->SetProperty(views::kMarginsKey, gfx::Insets(vertical_spacing, 0));

  // The save_group_line_container is only created if the
  // feature::kTabGroupsSave is enabled.
  if (save_group_line_container) {
    gfx::Insets save_group_margins = control_insets;
    const int label_height = new_tab_menu_item->GetPreferredSize().height();
    const int control_height =
        std::max(save_group_label->GetPreferredSize().height(),
                 save_group_toggle_->GetPreferredSize().height());
    save_group_margins.set_top((label_height - control_height) / 2);
    save_group_margins.set_bottom(save_group_margins.top());

    save_group_icon->SetProperty(
        views::kMarginsKey,
        gfx::Insets(0, 0, 0, new_tab_menu_item->GetImageLabelSpacing()));

    save_group_line_container
        ->SetLayoutManager(std::make_unique<views::FlexLayout>())
        ->SetOrientation(views::LayoutOrientation::kHorizontal)
        .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
        .SetInteriorMargin(save_group_margins);

    save_group_label->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,
                                 views::MaximumFlexSizeRule::kUnbounded));
  }
}

TabGroupEditorBubbleView::~TabGroupEditorBubbleView() = default;

tab_groups::TabGroupColorId TabGroupEditorBubbleView::InitColorSet() {
  const tab_groups::ColorLabelMap& color_map =
      tab_groups::GetTabGroupColorLabelMap();

  // TODO(tluk) remove the reliance on the ordering of the color pairs in the
  // vector and use the ColorLabelMap structure instead.
  std::copy(color_map.begin(), color_map.end(), std::back_inserter(colors_));

  // Keep track of the current group's color, to be returned as the initial
  // selected value.
  auto* const group_model = browser_->tab_strip_model()->group_model();
  return group_model->GetTabGroup(group_)->visual_data()->color();
}

void TabGroupEditorBubbleView::UpdateGroup() {
  absl::optional<int> selected_element = color_selector_->GetSelectedElement();
  TabGroup* tab_group =
      browser_->tab_strip_model()->group_model()->GetTabGroup(group_);

  const tab_groups::TabGroupVisualData* current_visual_data =
      tab_group->visual_data();
  const tab_groups::TabGroupColorId updated_color =
      selected_element.has_value() ? colors_[selected_element.value()].first
                                   : current_visual_data->color();

  if (current_visual_data->color() != updated_color) {
    base::RecordAction(
        base::UserMetricsAction("TabGroups_TabGroupBubble_ColorChanged"));
  }

  tab_groups::TabGroupVisualData new_data(title_field_->GetText(),
                                          updated_color,
                                          current_visual_data->is_collapsed());
  tab_group->SetVisualData(new_data, true);
}

void TabGroupEditorBubbleView::OnSaveTogglePressed() {
  auto* group = browser_->tab_strip_model()->group_model()->GetTabGroup(group_);

  if (save_group_toggle_->GetIsOn()) {
    base::RecordAction(
        base::UserMetricsAction("TabGroups_TabGroupBubble_GroupSaved"));
    group->SaveGroup();
  } else {
    base::RecordAction(
        base::UserMetricsAction("TabGroups_TabGroupBubble_GroupUnsaved"));
    group->UnsaveGroup();
  }
}

void TabGroupEditorBubbleView::NewTabInGroupPressed() {
  base::RecordAction(
      base::UserMetricsAction("TabGroups_TabGroupBubble_NewTabInGroup"));
  TabStripModel* const model = browser_->tab_strip_model();
  const auto tabs = model->group_model()->GetTabGroup(group_)->ListTabs();
  model->delegate()->AddTabAt(GURL(), tabs.end(), true, group_);
  // Close the widget to allow users to continue their work in their newly
  // created tab.
  GetWidget()->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
}

void TabGroupEditorBubbleView::UngroupPressed(TabGroupHeader* header_view) {
  base::RecordAction(
      base::UserMetricsAction("TabGroups_TabGroupBubble_Ungroup"));
  if (header_view)
    header_view->RemoveObserverFromWidget(GetWidget());
  TabStripModel* const model = browser_->tab_strip_model();

  const gfx::Range tab_range =
      model->group_model()->GetTabGroup(group_)->ListTabs();

  std::vector<int> tabs;
  tabs.reserve(tab_range.length());
  for (auto i = tab_range.start(); i < tab_range.end(); ++i)
    tabs.push_back(i);

  model->RemoveFromGroup(tabs);
  // Close the widget because it is no longer applicable.
  GetWidget()->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
}

void TabGroupEditorBubbleView::CloseGroupPressed() {
  base::RecordAction(
      base::UserMetricsAction("TabGroups_TabGroupBubble_CloseGroup"));
  browser_->tab_strip_model()->CloseAllTabsInGroup(group_);
  // Close the widget because it is no longer applicable.
  GetWidget()->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
}

void TabGroupEditorBubbleView::MoveGroupToNewWindowPressed() {
  browser_->tab_strip_model()->delegate()->MoveGroupToNewWindow(group_);
  GetWidget()->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
}

void TabGroupEditorBubbleView::SendFeedbackPressed() {
  base::RecordAction(
      base::UserMetricsAction("TabGroups_TabGroupBubble_SendFeedback"));
  chrome::ShowFeedbackPage(
      browser_, chrome::FeedbackSource::kFeedbackSourceDesktopTabGroups,
      /*description_template=*/std::string(),
      /*description_placeholder_text=*/std::string(),
      /*category_tag=*/std::string(),
      /*extra_diagnostics=*/std::string());
  GetWidget()->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
}

void TabGroupEditorBubbleView::OnBubbleClose() {
  if (title_at_opening_ != title_field_->GetText()) {
    base::RecordAction(
        base::UserMetricsAction("TabGroups_TabGroupBubble_NameChanged"));
  }
}

BEGIN_METADATA(TabGroupEditorBubbleView, views::BubbleDialogDelegateView)
END_METADATA

void TabGroupEditorBubbleView::TitleFieldController::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  DCHECK_EQ(sender, parent_->title_field_);
  parent_->UpdateGroup();
}

bool TabGroupEditorBubbleView::TitleFieldController::HandleKeyEvent(
    views::Textfield* sender,
    const ui::KeyEvent& key_event) {
  DCHECK_EQ(sender, parent_->title_field_);

  // For special actions, only respond to key pressed events, to be consistent
  // with other views like buttons and dialogs.
  if (key_event.type() == ui::EventType::ET_KEY_PRESSED) {
    const ui::KeyboardCode key_code = key_event.key_code();
    if (key_code == ui::VKEY_ESCAPE) {
      parent_->GetWidget()->CloseWithReason(
          views::Widget::ClosedReason::kEscKeyPressed);
      return true;
    }
    if (key_code == ui::VKEY_RETURN) {
      parent_->GetWidget()->CloseWithReason(
          views::Widget::ClosedReason::kUnspecified);
      return true;
    }
  }

  return false;
}

void TabGroupEditorBubbleView::TitleField::ShowContextMenu(
    const gfx::Point& p,
    ui::MenuSourceType source_type) {
  // There is no easy way to stop the propagation of a ShowContextMenu event,
  // which is sometimes used to open the bubble itself. So when the bubble is
  // opened this way, we manually hide the textfield's context menu the first
  // time. Otherwise, the textfield, which is automatically focused, would show
  // an extra context menu when the bubble first opens.
  if (stop_context_menu_propagation_) {
    stop_context_menu_propagation_ = false;
    return;
  }
  views::Textfield::ShowContextMenu(p, source_type);
}

BEGIN_METADATA(TabGroupEditorBubbleView, TitleField, views::Textfield)
END_METADATA

DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(TabGroupEditorBubbleView,
                                      kEditorBubbleIdentifier);

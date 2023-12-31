// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_util.h"
#include "build/build_config.h"
#include "content/browser/accessibility/browser_accessibility_manager.h"
#include "content/browser/accessibility/dump_accessibility_browsertest_base.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "ui/accessibility/platform/inspect/ax_api_type.h"
#include "ui/accessibility/platform/inspect/ax_script_instruction.h"

namespace content {

using ui::AXPropertyFilter;
using ui::AXScriptInstruction;
using ui::AXTreeFormatter;

#if defined(OS_MAC)

constexpr const char kMacAction[]{"mac/action"};
constexpr const char kMacAttributes[]{"mac/attributes"};
constexpr const char kMacSelection[]{"mac/selection"};
constexpr const char kMacTextMarker[]{"mac/textmarker"};
constexpr const char kMacMethods[]{"mac/methods"};

#endif

// See content/test/data/accessibility/readme.md for an overview.
//
// This test loads an HTML file, invokes a script, and then
// compares the script output against an expected baseline.
//
// The flow of the test is as outlined below.
// 1. Load an html file from content/test/data/accessibility.
// 2. Read the expectation.
// 3. Browse to the page, executes scripts and format their output.
// 4. Perform a comparison between actual and expected and fail if they do not
//    exactly match.
class DumpAccessibilityScriptTest : public DumpAccessibilityTestBase {
 public:
  DumpAccessibilityScriptTest() {
    // Drop 'mac' expectations qualifier both from expectation file names and
    // from scenario directives.
    test_helper_.OverrideExpectationType("content");
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    // Enable MathMLCore for some MathML tests.
    base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
        switches::kEnableBlinkFeatures, "MathMLCore");
  }

  std::vector<ui::AXPropertyFilter> DefaultFilters() const override;
  void AddPropertyFilter(
      std::vector<AXPropertyFilter>* property_filters,
      const std::string& filter,
      AXPropertyFilter::Type type = AXPropertyFilter::ALLOW) {
    property_filters->push_back(AXPropertyFilter(filter, type));
  }

  base::Value EvaluateScript(
      AXTreeFormatter* formatter,
      BrowserAccessibility* root,
      const std::vector<AXScriptInstruction>& instructions,
      size_t start_index,
      size_t end_index) {
    return base::Value(
        formatter->EvaluateScript(root, instructions, start_index, end_index));
  }

  std::vector<std::string> Dump() override {
    std::vector<std::string> dump;
    std::unique_ptr<AXTreeFormatter> formatter(CreateFormatter());
    BrowserAccessibility* root = GetManager()->GetRoot();

    size_t start_index = 0;
    size_t length = scenario_.script_instructions.size();
    while (start_index < length) {
      std::string wait_for;
      size_t index = start_index;
      for (; index < length; index++) {
        if (scenario_.script_instructions[index].IsEvent()) {
          wait_for = scenario_.script_instructions[index].AsEvent();
          break;
        }
      }

      std::string actual_contents;
      if (wait_for.empty()) {
        actual_contents = formatter->EvaluateScript(
            root, scenario_.script_instructions, start_index, index);
      } else {
        auto pair = CaptureEvents(
            base::BindOnce(&DumpAccessibilityScriptTest::EvaluateScript,
                           base::Unretained(this), formatter.get(), root,
                           scenario_.script_instructions, start_index, index));
        actual_contents = pair.first.GetString();
        for (auto event : pair.second) {
          if (base::StartsWith(event, wait_for)) {
            actual_contents += event + '\n';
          }
        }
      }

      auto chunk =
          base::SplitString(actual_contents, "\n", base::KEEP_WHITESPACE,
                            base::SPLIT_WANT_NONEMPTY);
      dump.insert(dump.end(), chunk.begin(), chunk.end());

      start_index = index + 1;
    }
    return dump;
  }
};

std::vector<ui::AXPropertyFilter> DumpAccessibilityScriptTest::DefaultFilters()
    const {
  return {};
}

// Parameterize the tests so that each test-pass is run independently.
struct TestPassToString {
  std::string operator()(
      const ::testing::TestParamInfo<ui::AXApiType::Type>& i) const {
    return std::string(i.param);
  }
};

//
// Scripting supported on Mac only.
//

#if defined(OS_MAC)

INSTANTIATE_TEST_SUITE_P(All,
                         DumpAccessibilityScriptTest,
                         ::testing::Values(ui::AXApiType::kMac),
                         TestPassToString());

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXPressButton) {
  RunTypedTest<kMacAction>("ax-press-button.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXAccessKey) {
  RunTypedTest<kMacAttributes>("ax-access-key.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXARIAAtomic) {
  RunTypedTest<kMacAttributes>("ax-aria-atomic.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXARIABusy) {
  RunTypedTest<kMacAttributes>("ax-aria-busy.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXARIACurrent) {
  RunTypedTest<kMacAttributes>("ax-aria-current.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXARIALive) {
  RunTypedTest<kMacAttributes>("ax-aria-live.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXARIARelevant) {
  RunTypedTest<kMacAttributes>("ax-aria-relevant.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXAutocompleteValue) {
  RunTypedTest<kMacAttributes>("ax-autocomplete-value.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXColumnHeaderUIElements) {
  RunTypedTest<kMacAttributes>("ax-column-header-ui-elements.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXDetailsElements) {
  RunTypedTest<kMacAttributes>("ax-details-elements.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXInvalid) {
  RunTypedTest<kMacAttributes>("ax-invalid.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXMathBase) {
  RunTypedTest<kMacAttributes>("ax-math-base.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXMathFractionDenominator) {
  RunTypedTest<kMacAttributes>("ax-math-fraction-denominator.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXMathFractionNumerator) {
  RunTypedTest<kMacAttributes>("ax-math-fraction-numerator.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXMathOver) {
  RunTypedTest<kMacAttributes>("ax-math-over.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXMathPoscripts) {
  RunTypedTest<kMacAttributes>("ax-math-postscripts.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXMathPrescripts) {
  RunTypedTest<kMacAttributes>("ax-math-prescripts.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXMathRootIndex) {
  RunTypedTest<kMacAttributes>("ax-math-root-index.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXMathRootRadicand) {
  RunTypedTest<kMacAttributes>("ax-math-root-radicand.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXMathSubscript) {
  RunTypedTest<kMacAttributes>("ax-math-subscript.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXMathSuperscript) {
  RunTypedTest<kMacAttributes>("ax-math-superscript.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXMathUnder) {
  RunTypedTest<kMacAttributes>("ax-math-under.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, SelectAllTextarea) {
  RunTypedTest<kMacSelection>("selectall-textarea.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest,
                       SetSelectionContenteditable) {
  RunTypedTest<kMacSelection>("set-selection-contenteditable.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, SetSelectionTextarea) {
  RunTypedTest<kMacSelection>("set-selection-textarea.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest,
                       SetSelectedTextRangeContenteditable) {
  RunTypedTest<kMacSelection>("set-selectedtextrange-contenteditable.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest,
                       AXNextWordEndTextMarkerForTextMarker) {
  RunTypedTest<kMacTextMarker>(
      "ax-next-word-end-text-marker-for-text-marker.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest,
                       AXPreviousWordStartTextMarkerForTextMarker) {
  RunTypedTest<kMacTextMarker>(
      "ax-previous-word-start-text-marker-for-text-marker.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AXStartTextMarker) {
  RunTypedTest<kMacTextMarker>("ax-start-text-marker.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest,
                       AXTextMarkerRangeForUIElement) {
  RunTypedTest<kMacTextMarker>("ax-text-marker-range-for-ui-element.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest,
                       AccessibilityColumnHeaderUIElements) {
  RunTypedTest<kMacMethods>("accessibility-column-header-ui-elements.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest,
                       AccessibilityPlaceholderValue) {
  RunTypedTest<kMacMethods>("accessibility-placeholder-value.html");
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityScriptTest, AccessibilityTitle) {
  RunTypedTest<kMacMethods>("accessibility-title.html");
}

#endif

}  // namespace content

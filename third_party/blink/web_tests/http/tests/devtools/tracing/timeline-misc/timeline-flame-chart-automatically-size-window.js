// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(async function() {
  TestRunner.addResult(`Tests the TimelineFlameChart automatically sized window.\n`);
  await TestRunner.loadLegacyModule('console'); await TestRunner.loadTestModule('console_test_runner');
  await TestRunner.loadModule('timeline'); await TestRunner.loadTestModule('performance_test_runner');
  await TestRunner.showPanel('timeline');

  var timeline = UI.panels.timeline;
  timeline.onModeChanged();
  timeline.flameChart._automaticallySizeWindow = true;

  function setWindowTimesHook(startTime, endTime) {
    if (startTime)
      TestRunner.addResult('time delta: ' + (endTime - startTime));
  }

  timeline.overviewPane.setWindowTimes = setWindowTimesHook;
  await PerformanceTestRunner.loadTimeline(PerformanceTestRunner.timelineData());
  TestRunner.completeTest();
})();

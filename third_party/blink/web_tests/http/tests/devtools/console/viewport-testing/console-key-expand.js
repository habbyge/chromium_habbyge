// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(async function() {
  TestRunner.addResult(`Tests that console artifacts can be expanded, collapsed via keyboard.\n`);
  await TestRunner.loadLegacyModule('console'); await TestRunner.loadTestModule('console_test_runner');
  await TestRunner.showPanel('console');
  ConsoleTestRunner.fixConsoleViewportDimensions(600, 200);
  await ConsoleTestRunner.waitUntilConsoleEditorLoaded();

  const consoleView = Console.ConsoleView.instance();
  const viewport = consoleView.viewport;
  const prompt = consoleView.prompt;

  await TestRunner.evaluateInPagePromise(`
    var obj1 = Object.create(null);
    obj1.x = 1;

    var obj2 = Object.create(null);
    obj2.y = 2;
  `);

  TestRunner.runTestSuite([
    async function testExpandingTraces(next) {
      await clearAndLog(`console.warn("warning")`);
      forceSelect(0);

      await dumpFocus();
      press('ArrowRight');
      await dumpFocus();
      press('ArrowLeft');
      await dumpFocus();

      next();
    },

    async function testExpandingGroups(next) {
      await clearAndLog(`console.group("group"); console.log("log child");`, 2);
      forceSelect(0);

      await dumpFocus();
      await ConsoleTestRunner.dumpConsoleMessages();
      press('ArrowLeft');
      await dumpFocus();
      await ConsoleTestRunner.dumpConsoleMessages();
      press('ArrowRight');
      await dumpFocus();
      await ConsoleTestRunner.dumpConsoleMessages();

      next();
    },

    async function testNavigateBetweenObjectsAndLogs(next) {
      await clearAndLog(`console.log("before");console.log("text", obj1, obj2);console.log("after");`, 3);
      forceSelect(1);

      await dumpFocus(true, 1, true /* skipObjectCheck */);
      press('ArrowRight');
      await dumpFocus(true, 1, true /* skipObjectCheck */);
      press('ArrowDown');
      await dumpFocus(true, 1, true /* skipObjectCheck */);
      press('ArrowDown');
      await dumpFocus(true, 1, true /* skipObjectCheck */);
      press('ArrowDown');
      await dumpFocus(true, 1, true /* skipObjectCheck */);
      press('ArrowUp');
      await dumpFocus(true, 1, true /* skipObjectCheck */);
      press('ArrowUp');
      await dumpFocus(true, 1, true /* skipObjectCheck */);
      press('ArrowLeft');
      await dumpFocus(true, 1, true /* skipObjectCheck */);

      next();
    },

    // Note: During this test expanded objects
    // do not include the __proto__ property.
    async function testExpandingObjects(next) {
      await clearAndLog(`console.log("before");console.log("text", obj1, obj2);console.log("after");`, 3);
      forceSelect(1);

      await dumpFocus(true, 1, true /* skipObjectCheck */);
      press('ArrowRight');
      await dumpFocus(true, 1, true /* skipObjectCheck */);

      // Expand obj1.
      press('ArrowRight');
      await dumpFocus(true, 1, true /* skipObjectCheck */);
      await ConsoleTestRunner.waitForRemoteObjectsConsoleMessagesPromise();
      press('ArrowDown');
      await dumpFocus(true, 1, true /* skipObjectCheck */);
      press('ArrowDown');
      await dumpFocus(true, 1, true /* skipObjectCheck */);

      // Expand obj2.
      press('ArrowRight');
      await dumpFocus(true, 1, true /* skipObjectCheck */);
      await ConsoleTestRunner.waitForRemoteObjectsConsoleMessagesPromise();
      press('ArrowDown');
      press('ArrowDown');
      press('ArrowDown');
      await dumpFocus(true, 1, true /* skipObjectCheck */);

      press('ArrowUp');
      press('ArrowUp');
      await dumpFocus(true, 1, true /* skipObjectCheck */);

      // Collapse object.
      press('ArrowLeft');
      await dumpFocus(true, 1, true /* skipObjectCheck */);

      // Select message.
      press('ArrowLeft');
      await dumpFocus(true, 1, true /* skipObjectCheck */);

      next();
    },

    async function testExpandingObjectInTrace(next) {
      await clearAndLog(`console.log("before");console.warn("warning", obj1);console.log("after");`, 3);
      forceSelect(1);

      await dumpFocus(true, 1);
      press('ArrowRight');
      await dumpFocus(true, 1);
      press('ArrowRight');
      await dumpFocus(true, 1);

      // Expand object.
      press('ArrowRight');
      await dumpFocus(true, 1);
      await ConsoleTestRunner.waitForRemoteObjectsConsoleMessagesPromise();
      press('ArrowDown');
      await dumpFocus(true, 1);
      press('ArrowDown');
      press('ArrowDown');
      press('ArrowDown');
      await dumpFocus(true, 1);

      press('ArrowUp');
      press('ArrowUp');
      await dumpFocus(true, 1);
      press('ArrowUp');
      await dumpFocus(true, 1);
      press('ArrowUp');
      await dumpFocus(true, 1);

      // Collapse trace.
      press('ArrowLeft');
      await dumpFocus(true, 1);

      // ArrowLeft on message does not collapse object.
      press('ArrowLeft');
      await dumpFocus(true, 1);

      next();
    },

    async function testExpandingElement(next) {
      await TestRunner.evaluateInPagePromise(`
        var el = document.createElement('div');
        var child = document.createElement('span');
        el.appendChild(child); undefined;
      `);
      const nodePromise = TestRunner.addSnifferPromise(Console.ConsoleViewMessage.prototype, 'formattedParameterAsNodeForTest');
      await clearAndLog(`console.log("before");console.log(el);console.log("after");`, 3);
      await nodePromise;
      forceSelect(1);

      await dumpFocus(true, 1);
      press('ArrowDown');
      press('ArrowDown');
      await dumpFocus(true, 1);

      // Expand object.
      press('ArrowRight');
      await ConsoleTestRunner.waitForRemoteObjectsConsoleMessagesPromise();
      await dumpFocus(true, 1);

      next();
    },

    async function testShiftTabShouldSelectLastObject(next) {
      await clearAndLog(`console.log("before");console.log(obj1);`, 2);
      await ConsoleTestRunner.waitForRemoteObjectsConsoleMessagesPromise();

      TestRunner.addResult(`Setting focus in prompt:`);
      prompt.focus();
      shiftPress('Tab');
      press('ArrowUp');  // Move from source link to object.
      await dumpFocus(true, 1);

      // Expand object.
      press('ArrowRight');
      await ConsoleTestRunner.waitForRemoteObjectsConsoleMessagesPromise();
      await dumpFocus(true, 1);

      next();
    },

    async function testArrowUpToFirstVisibleMessageShouldSelectLastObject(
        next) {
      await clearAndLog(`console.log(obj1);console.log("after");`, 2);
      await ConsoleTestRunner.waitForRemoteObjectsConsoleMessagesPromise();

      TestRunner.addResult(`Setting focus in prompt:`);
      prompt.focus();
      shiftPress('Tab');
      press('ArrowUp');  // Move from source link to "after".
      await dumpFocus(true);

      press('ArrowUp');  // Move from source link to object.
      press('ArrowUp');
      await dumpFocus(true);

      next();
    },

    async function testFocusLastChildInBigObjectShouldScrollIntoView(next) {
      await TestRunner.evaluateInPagePromise(`
        var bigObj = Object.create(null);
          for (var i = 0; i < 100; i++)
            bigObj['a' + i] = i;
      `);
      await clearAndLog(`console.log(bigObj);`, 1);
      await ConsoleTestRunner.waitForRemoteObjectsConsoleMessagesPromise();

      TestRunner.addResult(`Setting focus in prompt:`);
      prompt.focus();
      shiftPress('Tab');
      press('ArrowUp');  // Move from source link to object.
      press('ArrowRight');
      await ConsoleTestRunner.waitForRemoteObjectsConsoleMessagesPromise();
      press('Tab');

      await dumpFocus(true);
      shiftPress('Tab');
      press('ArrowUp');  // Move from source link to object.

      await dumpFocus(true);
      dumpScrollInfo();

      next();
    },
  ]);


  // Utilities.
  async function clearAndLog(expression, expectedCount = 1) {
    consoleView.consoleCleared();
    TestRunner.addResult(`Evaluating: ${expression}`);
    await TestRunner.evaluateInPagePromise(expression);
    await ConsoleTestRunner.waitForConsoleMessagesPromise(expectedCount);
    await ConsoleTestRunner.waitForPendingViewportUpdates();
  }

  function forceSelect(index) {
    TestRunner.addResult(`\nForce selecting index ${index}`);
    viewport.virtualSelectedIndex = index;
    viewport.contentElement().focus();
    viewport.updateFocusedItem();
  }

  function press(key) {
    TestRunner.addResult(`\n${key}:`);
    eventSender.keyDown(key);
  }

  function shiftPress(key) {
    TestRunner.addResult(`\nShift+${key}:`);
    eventSender.keyDown(key, ['shiftKey']);
  }

  function dumpScrollInfo() {
    viewport.refresh();
    let infoText =
      'Is at bottom: ' + TestRunner.isScrolledToBottom(viewport.element) + ', should stick: ' + viewport.stickToBottom();
    TestRunner.addResult(infoText);
  }

  async function dumpFocus(activeElement, messageIndex = 0, skipObjectCheck) {
    const firstMessage = consoleView.visibleViewMessages[messageIndex]
    // Ordering here is important. Retrieving the element triggers the creation of a LiveLocation.
    // Wait for pending updates to settle as updates usually cause more rendering.
    const firstMessageElement = firstMessage.element();
    await TestRunner.waitForPendingLiveLocationUpdates();

    const hasTrace = !!firstMessageElement.querySelector('.console-message-stack-trace-toggle');
    const hasHiddenStackTrace = firstMessageElement.querySelector('.console-message-stack-trace-wrapper > div.hidden');
    const hasCollapsedObject = firstMessageElement.querySelector('.console-view-object-properties-section:not(.expanded)');
    const hasExpandedObject = firstMessageElement.querySelector('.console-view-object-properties-section.expanded');

    TestRunner.addResult(`Viewport virtual selection: ${viewport.virtualSelectedIndex}`);

    if (!skipObjectCheck) {
      if (hasCollapsedObject) {
        TestRunner.addResult(`Has object: collapsed`);
      } else if (hasExpandedObject) {
        TestRunner.addResult(`Has object: expanded`);
      }
    }

    if (hasTrace) {
      TestRunner.addResult(`Is trace expanded: ${!hasHiddenStackTrace ? 'YES' : 'NO'}`);
    }
    if (firstMessage instanceof Console.ConsoleGroupViewMessage) {
      const expanded = !firstMessage.collapsed();
      TestRunner.addResult(`Is group expanded: ${expanded ? 'YES' : 'NO'}`);
    }

    if (!activeElement)
      return;
    var element = document.deepActiveElement();
    if (!element) {
      TestRunner.addResult('null');
      return;
    }
    var name = `activeElement: ${element.tagName}`;
    if (element.id)
      name += '#' + element.id;
    else if (element.className)
      name += '.' + element.className.split(' ').join('.');
    if (element.deepTextContent())
      name += '\nactive text: ' + element.deepTextContent();
    TestRunner.addResult(name);
  }
})();

// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {PromiseResolver} from 'chrome://resources/js/promise_resolver.m.js';
import {fakeRsuChallengeQrCode} from 'chrome://shimless-rma/fake_data.js';
import {FakeShimlessRmaService} from 'chrome://shimless-rma/fake_shimless_rma_service.js';
import {setShimlessRmaServiceForTesting} from 'chrome://shimless-rma/mojo_interface_provider.js';
import {OnboardingEnterRsuWpDisableCodePage} from 'chrome://shimless-rma/onboarding_enter_rsu_wp_disable_code_page.js';
import {assertDeepEquals, assertEquals, assertFalse, assertTrue} from '../../chai_assert.js';
import {flushTasks} from '../../test_util.js';


/**
 * It is not possible to suppress visibility inline so this helper
 * function wraps the access to canvasSize_.
 * @suppress {visibility}
 */
function suppressedComponentCanvasSize_(component) {
  return component.canvasSize_;
}

export function onboardingEnterRsuWpDisableCodePageTest() {
  /** @type {?OnboardingEnterRsuWpDisableCodePage} */
  let component = null;

  /** @type {?FakeShimlessRmaService} */
  let service = null;

  suiteSetup(() => {
    service = new FakeShimlessRmaService();
    setShimlessRmaServiceForTesting(service);
  });

  setup(() => {
    document.body.innerHTML = '';
  });

  teardown(() => {
    component.remove();
    component = null;
    service.reset();
  });

  /**
   * @param {string} challenge
   * @param {string} hwid
   * @return {!Promise}
   */
  function initializeEnterRsuWpDisableCodePage(challenge, hwid) {
    assertFalse(!!component);

    // Initialize the fake data.
    service.setGetRsuDisableWriteProtectChallengeResult(challenge);
    service.setGetRsuDisableWriteProtectHwidResult(hwid);
    service.setGetRsuDisableWriteProtectChallengeQrCodeResponse(
        fakeRsuChallengeQrCode);

    component = /** @type {!OnboardingEnterRsuWpDisableCodePage} */ (
        document.createElement('onboarding-enter-rsu-wp-disable-code-page'));
    assertTrue(!!component);
    document.body.appendChild(component);

    return flushTasks();
  }

  test('EnterRsuWpDisableCodePageInitializes', async () => {
    await initializeEnterRsuWpDisableCodePage('rsu challenge', '');
    const rsuCodeComponent = component.shadowRoot.querySelector('#rsuCode');
    assertFalse(rsuCodeComponent.hidden);
  });

  test('EnterRsuWpDisableCodePageDisplaysChallenge', async () => {
    let expectedParts = ['rsu &nbsp;', 'chal&nbsp;', 'leng&nbsp;', 'e&nbsp;'];
    await initializeEnterRsuWpDisableCodePage('rsu challenge', '');
    const rsuChallengeComponent =
        component.shadowRoot.querySelector('#rsuChallenge');
    assertEquals(7, rsuChallengeComponent.childElementCount);

    // Confirm all the parts match the expected challenge.
    let parts = component.shadowRoot.querySelector('#rsuChallengeParts')
                    .querySelectorAll('span');
    for (let i = 0; i < parts.length; i++) {
      assertEquals(expectedParts[i], parts[i].innerHTML);
    }
  });

  test('EnterRsuWpDisableCodePageDisplaysHwid', async () => {
    await initializeEnterRsuWpDisableCodePage('', 'device hwid');
    const rsuHwidComponent = component.shadowRoot.querySelector('#rsuHwid');
    assertTrue(rsuHwidComponent.innerHTML.includes('device hwid'));
  });

  test('EnterRsuWpDisableCodePageRendersQrCode', async () => {
    await initializeEnterRsuWpDisableCodePage('', '');

    const expectedCanvasSize = 60;


    assertEquals(suppressedComponentCanvasSize_(component), expectedCanvasSize);
    const canvas = component.shadowRoot.querySelector('#qrCodeCanvas');
    assertTrue(!!canvas);
    assertEquals(canvas.width, expectedCanvasSize);
    assertEquals(canvas.height, expectedCanvasSize);

    const context = canvas.getContext('2d');
    assertTrue(!!context);
  });

  test(
      'EnterRsuWpDisableCodePageSetCodeOnNextCallsSetRsuDisableWriteProtectCode',
      async () => {
        const resolver = new PromiseResolver();
        await initializeEnterRsuWpDisableCodePage('', '');
        let expectedCode = 'rsu code';
        let savedCode = '';
        service.setRsuDisableWriteProtectCode = (code) => {
          savedCode = code;
          return resolver.promise;
        };
        const rsuCodeComponent = component.shadowRoot.querySelector('#rsuCode');
        rsuCodeComponent.value = expectedCode;

        let expectedResult = {foo: 'bar'};
        let savedResult;
        component.onNextButtonClick().then((result) => savedResult = result);
        // Resolve to a distinct result to confirm it was not modified.
        resolver.resolve(expectedResult);
        await flushTasks();

        assertDeepEquals(savedCode, expectedCode);
        assertDeepEquals(savedResult, expectedResult);
      });
}

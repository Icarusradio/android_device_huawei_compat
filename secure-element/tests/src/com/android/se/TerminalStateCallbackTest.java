/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.android.se;

import android.content.Intent;
import android.hardware.secure_element.V1_0.ISecureElementHalCallback;
import android.os.UserHandle;
import android.se.omapi.SEService;
import android.test.InstrumentationTestCase;
import android.test.mock.MockContext;

import com.android.se.security.AccessControlEnforcer;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Reproduce synchronous HAL init callbacks without contacting a real SIM or HAL. */
public class TerminalStateCallbackTest extends InstrumentationTestCase {
    public void testNestedInitCallbackIsDeferredAndOrdered() throws Throwable {
        checkCallbacks(false, false);
        checkCallbacks(true, false);
    }

    public void testBinderCallbackReturnsWhileInitHoldsLock() throws Throwable {
        checkCallbacks(false, true);
        checkCallbacks(true, true);
    }

    private void checkCallbacks(boolean version11, boolean binderThread) throws Throwable {
        List<String> accessEvents = new ArrayList<>();
        List<Boolean> broadcasts = new ArrayList<>();
        AtomicReference<Terminal> terminalRef = new AtomicReference<>();
        AtomicReference<Throwable> failure = new AtomicReference<>();
        runTestOnUiThread(() -> {
            Terminal terminal = new Terminal("SIM1", new MockContext() {
                @Override
                public void sendBroadcastAsUser(Intent intent, UserHandle user) {
                    broadcasts.add(intent.getBooleanExtra(SEService.EXTRA_READER_STATE, false));
                }
            });
            terminalRef.set(terminal);
            try {
                field("mAccessControlEnforcer").set(terminal, new AccessControlEnforcer(terminal) {
                    @Override
                    public void initialize() {
                        accessEvents.add("initialize");
                    }

                    @Override
                    public void reset() {
                        accessEvents.add("reset");
                    }
                });
                Object callback = field(version11 ? "mHalCallback11" : "mHalCallback")
                        .get(terminal);
                CountDownLatch returned = new CountDownLatch(1);
                Runnable notifyStates = () -> {
                    try {
                        for (boolean state : new boolean[]{true, false}) {
                            if (version11) {
                                ((android.hardware.secure_element.V1_1.ISecureElementHalCallback)
                                        callback).onStateChange_1_1(state, "test");
                            } else {
                                ((ISecureElementHalCallback) callback).onStateChange(state);
                            }
                        }
                    } catch (Throwable t) {
                        failure.set(t);
                    } finally {
                        returned.countDown();
                    }
                };
                // Model initialize() waiting for the synchronous callback to return.
                synchronized (field("mLock").get(terminal)) {
                    if (binderThread) {
                        new Thread(notifyStates, "fake-hal-callback").start();
                    } else {
                        notifyStates.run();
                    }
                    assertTrue("Callback waited for the initialization lock",
                            returned.await(2, TimeUnit.SECONDS));
                    assertNull(failure.get());
                    assertTrue("Access rules loaded inside the HAL callback", accessEvents.isEmpty());
                    assertTrue("State broadcast before init returned", broadcasts.isEmpty());
                }
            } catch (ReflectiveOperationException | InterruptedException e) {
                throw new AssertionError(e);
            }
        });
        getInstrumentation().waitForIdleSync();
        assertEquals(Arrays.asList("initialize", "reset"), accessEvents);
        assertEquals(Arrays.asList(true, false), broadcasts);
        assertFalse(terminalRef.get().mIsConnected);
    }

    private static Field field(String name) throws NoSuchFieldException {
        Field field = Terminal.class.getDeclaredField(name);
        field.setAccessible(true);
        return field;
    }
}

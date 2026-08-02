const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const SCRIPT_PATH = path.resolve(
    __dirname,
    "../../PrismaUI/views/RleyBodyMorphs/script.js"
);
const SCRIPT_SOURCE = fs.readFileSync(SCRIPT_PATH, "utf8");

function makeEvent(overrides = {}) {
    return {
        pointerId: 1,
        button: 0,
        buttons: 1,
        isPrimary: true,
        clientX: 200,
        clientY: 150,
        defaultPrevented: false,
        propagationStopped: false,
        preventDefault() {
            this.defaultPrevented = true;
        },
        stopPropagation() {
            this.propagationStopped = true;
        },
        ...overrides
    };
}

function makeMouseEvent(overrides = {}) {
    const event = makeEvent(overrides);
    delete event.pointerId;
    delete event.isPrimary;
    return event;
}

function listenerCapture(options) {
    return options === true || !!(options && options.capture);
}

function createHarness({ pointerEvents = true, pointerCapture = "success" } = {}) {
    const canvasListeners = new Map();
    const windowListeners = new Map();
    const captures = new Set();
    const commits = [];
    const previews = [];
    let timerSequence = 0;

    function addListener(registry, type, handler, options) {
        const capture = listenerCapture(options);
        const listeners = registry.get(type) || [];
        if (!listeners.some(item => item.handler === handler && item.capture === capture)) {
            listeners.push({ handler, capture });
            registry.set(type, listeners);
        }
    }

    function removeListener(registry, type, handler, options) {
        const capture = listenerCapture(options);
        const listeners = registry.get(type) || [];
        registry.set(
            type,
            listeners.filter(item => item.handler !== handler || item.capture !== capture)
        );
    }

    function dispatchListeners(registry, type, event, capture) {
        const listeners = [...(registry.get(type) || [])];
        for (const item of listeners) {
            if (item.capture === capture) item.handler(event);
        }
    }

    const context2d = new Proxy({}, {
        get(target, property) {
            if (!(property in target)) target[property] = () => {};
            return target[property];
        },
        set(target, property, value) {
            target[property] = value;
            return true;
        }
    });

    const canvas = {
        width: 400,
        height: 400,
        getContext: () => context2d,
        getBoundingClientRect: () => ({ left: 100, top: 50, width: 200, height: 200 }),
        addEventListener(type, handler, options) {
            addListener(canvasListeners, type, handler, options);
        },
        setPointerCapture(pointerId) {
            if (pointerCapture === "throw") throw new Error("capture rejected");
            if (pointerCapture === "success") captures.add(pointerId);
        },
        hasPointerCapture(pointerId) {
            return captures.has(pointerId);
        },
        releasePointerCapture(pointerId) {
            if (!captures.delete(pointerId)) return;
            this.dispatch("lostpointercapture", makeEvent({ pointerId, buttons: 0 }));
        },
        dispatch(type, event = makeEvent()) {
            event.type = type;
            dispatchListeners(windowListeners, type, event, true);
            if (!event.propagationStopped) {
                dispatchListeners(canvasListeners, type, event, true);
                dispatchListeners(canvasListeners, type, event, false);
            }
            if (!event.propagationStopped) {
                dispatchListeners(windowListeners, type, event, false);
            }
            return event;
        },
        listenerCount(type) {
            return (canvasListeners.get(type) || []).length;
        }
    };

    function control(id) {
        return {
            id,
            checked: true,
            textContent: "",
            addEventListener() {}
        };
    }

    const elements = {
        morphCanvas: canvas,
        thinValue: control("thinValue"),
        muscularValue: control("muscularValue"),
        fatValue: control("fatValue"),
        reset: control("reset"),
        snapToggle: control("snapToggle")
    };

    const sandbox = {
        console,
        document: {
            getElementById(id) {
                return elements[id];
            }
        },
        performance: { now: () => 1000 },
        requestAnimationFrame: () => 1,
        setTimeout: () => ++timerSequence,
        clearTimeout() {},
        onMorphsPreview(payload) {
            previews.push(payload);
        },
        onMorphsCommit(payload) {
            commits.push(payload);
        },
        addEventListener(type, handler, options) {
            addListener(windowListeners, type, handler, options);
        },
        removeEventListener(type, handler, options) {
            removeListener(windowListeners, type, handler, options);
        }
    };

    if (pointerEvents) sandbox.PointerEvent = function PointerEvent() {};
    sandbox.window = sandbox;

    vm.createContext(sandbox);
    vm.runInContext(SCRIPT_SOURCE, sandbox, { filename: SCRIPT_PATH });

    return {
        canvas,
        captures,
        commits,
        previews,
        state(expression) {
            return vm.runInContext(expression, sandbox);
        },
        dispatchWindow(type, event = makeEvent()) {
            event.type = type;
            dispatchListeners(windowListeners, type, event, true);
            if (!event.propagationStopped) {
                dispatchListeners(windowListeners, type, event, false);
            }
            return event;
        },
        windowListenerCount(type, capture) {
            return (windowListeners.get(type) || [])
                .filter(item => capture === undefined || item.capture === capture)
                .length;
        }
    };
}

function payloadValues(payload) {
    return payload.split(",").slice(0, 3).map(Number);
}

test("captured pointer backend maps scaled coordinates and clicks once", () => {
    const harness = createHarness();
    assert.equal(harness.canvas.listenerCount("pointerdown"), 1);
    assert.equal(harness.canvas.listenerCount("mousedown"), 1);

    harness.canvas.dispatch("pointerdown", makeEvent({ clientX: 200, clientY: 150 }));
    const stateAfterDown = harness.state("JSON.stringify(pendingMorphValues)");
    harness.canvas.dispatch(
        "pointerup",
        makeEvent({ buttons: 0, clientX: 200.5, clientY: 150.5 })
    );

    assert.deepEqual(payloadValues(harness.commits[0]), [0, 0, 0]);
    assert.equal(harness.commits.length, 1);
    assert.equal(harness.previews.length, 0);
    assert.equal(harness.state("JSON.stringify(pendingMorphValues)"), stateAfterDown);
    assert.equal(harness.state("activePointerId"), null);
    assert.equal(harness.captures.size, 0);

    harness.canvas.dispatch(
        "pointermove",
        makeEvent({ buttons: 0, clientX: 120, clientY: 200 })
    );
    assert.equal(harness.state("JSON.stringify(pendingMorphValues)"), stateAfterDown);
    assert.equal(harness.commits.length, 1);
});

test("advertised pointer support still accepts mouse-only host injection", () => {
    const harness = createHarness();
    harness.canvas.dispatch("mousedown", makeMouseEvent({ clientX: 200, clientY: 150 }));

    assert.equal(harness.state("activePointerId"), -1);
    assert.equal(harness.windowListenerCount("mousemove", true), 1);
    assert.equal(harness.windowListenerCount("mouseup", true), 1);

    harness.dispatchWindow(
        "mouseup",
        makeMouseEvent({ buttons: 0, clientX: 200, clientY: 150 })
    );

    assert.equal(harness.state("activePointerId"), null);
    assert.equal(harness.commits.length, 1);
});

test("compatibility mousedown cannot replace or double-commit an active pointer gesture", () => {
    const harness = createHarness();
    harness.canvas.dispatch("pointerdown", makeEvent());
    const compatibilityEvent = harness.canvas.dispatch("mousedown", makeMouseEvent());

    assert.equal(compatibilityEvent.defaultPrevented, true);
    assert.equal(harness.state("activePointerId"), 1);
    assert.equal(harness.windowListenerCount("mousemove", true), 0);
    assert.equal(harness.commits.length, 0);

    harness.canvas.dispatch("pointerup", makeEvent({ buttons: 0 }));
    assert.equal(harness.commits.length, 1);
});

test("captured pointer drag previews while held and finalizes idempotently", () => {
    const harness = createHarness();
    harness.canvas.dispatch("pointerdown", makeEvent());
    harness.canvas.dispatch(
        "pointermove",
        makeEvent({ buttons: 1, clientX: 225, clientY: 90 })
    );

    assert.equal(harness.state("activePointerId"), 1);
    assert.ok(harness.previews.length >= 1);

    harness.canvas.dispatch(
        "pointerup",
        makeEvent({ buttons: 0, clientX: 230, clientY: 85 })
    );
    harness.canvas.dispatch("lostpointercapture", makeEvent({ buttons: 0 }));
    harness.canvas.dispatch("pointercancel", makeEvent({ buttons: 0 }));
    harness.dispatchWindow("blur");

    assert.equal(harness.state("activePointerId"), null);
    assert.equal(harness.commits.length, 1);
    assert.equal(harness.captures.size, 0);
});

test("pointer buttons=0 recovers a missing pointerup", () => {
    const harness = createHarness();
    harness.canvas.dispatch("pointerdown", makeEvent({ clientX: 180, clientY: 130 }));
    harness.canvas.dispatch(
        "pointermove",
        makeEvent({ buttons: 0, clientX: 190, clientY: 120 })
    );

    assert.equal(harness.state("activePointerId"), null);
    assert.equal(harness.commits.length, 1);

    harness.canvas.dispatch("pointerup", makeEvent({ buttons: 0 }));
    assert.equal(harness.commits.length, 1);
});

test("rejected pointer capture falls back to temporary window capture listeners", () => {
    const harness = createHarness({ pointerCapture: "throw" });
    harness.canvas.dispatch("pointerdown", makeEvent());

    assert.equal(harness.windowListenerCount("pointermove", true), 1);
    assert.equal(harness.windowListenerCount("pointerup", true), 1);

    harness.dispatchWindow(
        "pointermove",
        makeEvent({ buttons: 1, clientX: 225, clientY: 90 })
    );
    harness.dispatchWindow(
        "pointerup",
        makeEvent({ buttons: 0, clientX: 230, clientY: 85 })
    );

    assert.equal(harness.commits.length, 1);
    assert.equal(harness.state("activePointerId"), null);
    assert.equal(harness.windowListenerCount("pointermove", true), 0);
    assert.equal(harness.windowListenerCount("pointerup", true), 0);
});

test("mouse fallback uses temporary capture-phase listeners without sticky state", () => {
    const harness = createHarness({ pointerEvents: false });
    assert.equal(harness.canvas.listenerCount("pointerdown"), 0);
    assert.equal(harness.canvas.listenerCount("mousedown"), 1);

    harness.canvas.dispatch("mousedown", makeMouseEvent({ clientX: 200, clientY: 150 }));
    assert.equal(harness.state("activePointerId"), -1);
    assert.equal(harness.windowListenerCount("mousemove", true), 1);
    assert.equal(harness.windowListenerCount("mouseup", true), 1);

    harness.dispatchWindow(
        "mousemove",
        makeMouseEvent({ buttons: 1, clientX: 225, clientY: 90 })
    );
    harness.dispatchWindow(
        "mouseup",
        makeMouseEvent({ buttons: 0, clientX: 230, clientY: 85 })
    );

    assert.ok(harness.previews.length >= 1);
    assert.equal(harness.commits.length, 1);
    assert.equal(harness.state("activePointerId"), null);
    assert.equal(harness.windowListenerCount("mousemove", true), 0);
    assert.equal(harness.windowListenerCount("mouseup", true), 0);

    harness.dispatchWindow("mousemove", makeMouseEvent({ buttons: 0 }));
    harness.dispatchWindow("mouseup", makeMouseEvent({ buttons: 0 }));
    assert.equal(harness.commits.length, 1);
});

test("mouse fallback recovers through buttons=0, mouseleave, and blur", async t => {
    for (const ending of ["buttons", "leave", "blur"]) {
        await t.test(ending, () => {
            const harness = createHarness({ pointerEvents: false });
            harness.canvas.dispatch(
                "mousedown",
                makeMouseEvent({ clientX: 180, clientY: 130 })
            );

            if (ending === "buttons") {
                harness.dispatchWindow(
                    "mousemove",
                    makeMouseEvent({ buttons: 0, clientX: 190, clientY: 120 })
                );
            } else if (ending === "leave") {
                harness.canvas.dispatch(
                    "mouseleave",
                    makeMouseEvent({ buttons: 1, clientX: 190, clientY: 120 })
                );
            } else {
                harness.dispatchWindow("blur");
            }

            assert.equal(harness.state("activePointerId"), null);
            assert.equal(harness.commits.length, 1);
            harness.dispatchWindow("blur");
            assert.equal(harness.commits.length, 1);
        });
    }
});

test("secondary and non-primary presses are ignored", () => {
    const pointerHarness = createHarness();
    pointerHarness.canvas.dispatch("pointerdown", makeEvent({ button: 2 }));
    pointerHarness.canvas.dispatch("pointerdown", makeEvent({ isPrimary: false }));
    assert.equal(pointerHarness.state("activePointerId"), null);
    assert.equal(pointerHarness.commits.length, 0);

    const mouseHarness = createHarness({ pointerEvents: false });
    mouseHarness.canvas.dispatch("mousedown", makeMouseEvent({ button: 2 }));
    assert.equal(mouseHarness.state("activePointerId"), null);
    assert.equal(mouseHarness.commits.length, 0);
});

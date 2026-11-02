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
    let clockMs = 1000;

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

    const classes = new Set();

    const canvas = {
        width: 400,
        height: 400,
        classList: {
            add(name) {
                classes.add(name);
            },
            remove(name) {
                classes.delete(name);
            },
            contains(name) {
                return classes.has(name);
            }
        },
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
        performance: { now: () => clockMs },
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
        advanceTime(ms) {
            clockMs += ms;
        },
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

test("preview cadence stays ahead of Mu generation without per-event flooding", () => {
    const harness = createHarness();
    assert.equal(harness.state("MORPH_PREVIEW_INTERVAL"), 32);
});

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

    // Um move SEGURADO primeiro: e ele que prova que este host mantem a mascara
    // de botao nos moves. Sem essa prova, buttons=0 e indistinguivel de um host
    // que simplesmente nao a reporta, e encerrar ali mata todo arrasto.
    harness.canvas.dispatch(
        "pointermove",
        makeEvent({ buttons: 1, clientX: 186, clientY: 126 })
    );
    assert.equal(harness.state("activePointerId"), 1);

    harness.canvas.dispatch(
        "pointermove",
        makeEvent({ buttons: 0, clientX: 190, clientY: 120 })
    );

    assert.equal(harness.state("activePointerId"), null);
    assert.equal(harness.commits.length, 1);

    harness.canvas.dispatch("pointerup", makeEvent({ buttons: 0 }));
    assert.equal(harness.commits.length, 1);
});

// O HOST REAL. PrismaUI injeta todo mouse-move com button=kButton_None, entao
// buttons le 0 durante o arrasto inteiro. Estes dois casos sao o que estava
// quebrado no jogo - e o que a suite antiga afirmava ser o comportamento certo.
test("a host that never reports held buttons still drags", () => {
    const harness = createHarness();
    harness.canvas.dispatch("pointerdown", makeEvent({ clientX: 200, clientY: 150 }));

    harness.canvas.dispatch(
        "pointermove",
        makeEvent({ buttons: 0, clientX: 240, clientY: 150 })
    );

    assert.equal(harness.state("activePointerId"), 1, "o arrasto tem de seguir vivo");
    assert.ok(harness.previews.length >= 1, "o knob tem de seguir o cursor");

    harness.canvas.dispatch(
        "pointerup",
        makeEvent({ buttons: 0, clientX: 240, clientY: 150 })
    );
    assert.equal(harness.state("activePointerId"), null);
    assert.equal(harness.commits.length, 1);
});

test("a host that never reports held buttons survives leaving the canvas", () => {
    const harness = createHarness({ pointerEvents: false });
    harness.canvas.dispatch("mousedown", makeMouseEvent({ clientX: 200, clientY: 150 }));

    harness.dispatchWindow(
        "mousemove",
        makeMouseEvent({ buttons: 0, clientX: 250, clientY: 150 })
    );
    harness.canvas.dispatch(
        "mouseleave",
        makeMouseEvent({ buttons: 0, clientX: 420, clientY: 150 })
    );

    assert.equal(harness.state("activePointerId"), -1, "sair da tela nao encerra o gesto");

    harness.dispatchWindow(
        "mouseup",
        makeMouseEvent({ buttons: 0, clientX: 420, clientY: 150 })
    );
    assert.equal(harness.state("activePointerId"), null);
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

            // As duas recuperacoes por buttons=0 exigem antes um move SEGURADO,
            // que e o que prova que este host mantem a mascara. Num host que
            // nao a mantem, buttons=0 nao autoriza conclusao nenhuma.
            if (ending === "buttons" || ending === "leave") {
                harness.dispatchWindow(
                    "mousemove",
                    makeMouseEvent({ buttons: 1, clientX: 186, clientY: 126 })
                );
            }

            if (ending === "buttons") {
                harness.dispatchWindow(
                    "mousemove",
                    makeMouseEvent({ buttons: 0, clientX: 190, clientY: 120 })
                );
            } else if (ending === "leave") {
                // buttons=0 is the recovery case: the button was released while
                // the cursor was outside the canvas, so no mouseup ever arrived.
                harness.canvas.dispatch(
                    "mouseleave",
                    makeMouseEvent({ buttons: 0, clientX: 190, clientY: 120 })
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

// The harness canvas is 400x400 displayed at 200x200, so client coordinates are
// half of canvas coordinates plus the rect origin.
function atCanvas(x, y, overrides = {}) {
    return makeEvent({ clientX: x / 2 + 100, clientY: y / 2 + 50, ...overrides });
}

test("leaving the canvas with the button held keeps the drag alive", () => {
    const harness = createHarness({ pointerEvents: false });
    harness.canvas.dispatch("mousedown", makeMouseEvent(atCanvas(200, 200)));

    // Overshooting the rim is the common case, because the pole anchors sit on
    // it. The gesture must survive.
    harness.canvas.dispatch(
        "mouseleave",
        makeMouseEvent(atCanvas(200, 40, { buttons: 1 }))
    );

    assert.equal(harness.state("activePointerId"), -1);
    assert.equal(harness.commits.length, 0);

    harness.dispatchWindow("mouseup", makeMouseEvent(atCanvas(200, 40, { buttons: 0 })));
    assert.equal(harness.state("activePointerId"), null);
    assert.equal(harness.commits.length, 1);
});

test("a quick click places the knob and does not drag with it", () => {
    const harness = createHarness();
    harness.canvas.dispatch("pointerdown", atCanvas(200, 120));

    const placed = harness.state("JSON.stringify(pendingMorphValues)");

    // Three canvas pixels of tremor, inside the same instant: below the travel
    // threshold and below the hold time, so the knob must not follow.
    harness.canvas.dispatch("pointermove", atCanvas(203, 120, { buttons: 1 }));

    assert.equal(harness.state("pointerMoved"), false);
    assert.equal(harness.state("JSON.stringify(pendingMorphValues)"), placed);

    harness.canvas.dispatch("pointerup", atCanvas(203, 120, { buttons: 0 }));
    assert.equal(harness.commits.length, 1);
    assert.equal(harness.state("JSON.stringify(pendingMorphValues)"), placed);
});

test("travelling far enough turns a press into a drag", () => {
    const harness = createHarness();
    harness.canvas.dispatch("pointerdown", atCanvas(200, 200));

    // Well clear of the centre's own snap zone, so the move is observable.
    harness.canvas.dispatch("pointermove", atCanvas(200, 150, { buttons: 1 }));

    assert.equal(harness.state("pointerMoved"), true);
    assert.equal(harness.state("Math.round(knob.y)"), 150);
});

test("holding still long enough also turns a press into a drag", () => {
    const harness = createHarness();
    harness.canvas.dispatch("pointerdown", atCanvas(200, 200));

    harness.advanceTime(200);

    // Two canvas pixels is under the travel threshold, but the press has now
    // been held past the hold threshold, so it counts as a drag.
    harness.canvas.dispatch("pointermove", atCanvas(202, 200, { buttons: 1 }));

    assert.equal(harness.state("pointerMoved"), true);
});

test("poles snap exactly, so the readout reaches 1.00", () => {
    const harness = createHarness();

    // 8 canvas px below the muscular anchor: inside the 14 px pole radius.
    harness.canvas.dispatch("pointerdown", atCanvas(200, 70));
    harness.canvas.dispatch("pointerup", atCanvas(200, 70, { buttons: 0 }));

    assert.deepEqual(payloadValues(harness.commits[0]), [0, 1, 0]);
});

test("sex transition invalidation allows the restored 100 percent state to commit again", () => {
    const harness = createHarness();

    harness.state('window.restoreMorphs("0,1,0")');
    harness.state('sendCommitMorphs({ thin: 0, muscular: 1, fat: 0 })');
    assert.equal(harness.commits.length, 0);

    harness.state("window.invalidateMorphDispatchState()");
    harness.state('sendCommitMorphs({ thin: 0, muscular: 1, fat: 0 })');

    assert.equal(harness.commits.length, 1);
    assert.deepEqual(payloadValues(harness.commits[0]), [0, 1, 0]);
});

test("an axis rail holds through hysteresis and releases past it", () => {
    const harness = createHarness();

    // Enter the vertical muscular rail from 5 px away.
    harness.canvas.dispatch("pointerdown", atCanvas(205, 130));
    assert.equal(harness.state("Math.round(knob.x)"), 200);
    assert.equal(harness.state("snapLineState.activeName"), "muscular");

    // 15 px away is past the 10 px entry threshold but inside the 22 px release
    // threshold, so an established rail must keep it.
    harness.canvas.dispatch("pointermove", atCanvas(215, 130, { buttons: 1 }));
    assert.equal(harness.state("Math.round(knob.x)"), 200);

    // 28 px away is past the release threshold.
    harness.canvas.dispatch("pointermove", atCanvas(228, 130, { buttons: 1 }));
    assert.equal(harness.state("Math.round(knob.x)"), 228);
    assert.equal(harness.state("snapLineState.isSnapped"), false);
});

test("shift suspends snapping without touching the snap toggle", () => {
    const harness = createHarness();

    harness.canvas.dispatch("pointerdown", atCanvas(205, 130, { shiftKey: true }));

    assert.equal(harness.state("Math.round(knob.x)"), 205);
    assert.equal(harness.state("snapLineState.isSnapped"), false);
    assert.equal(harness.state("snapEnabled"), true);
});

test("the dragging class tracks the gesture for the cursor style", () => {
    const harness = createHarness();
    assert.equal(harness.canvas.classList.contains("dragging"), false);

    harness.canvas.dispatch("pointerdown", atCanvas(200, 200));
    assert.equal(harness.canvas.classList.contains("dragging"), true);

    harness.canvas.dispatch("pointerup", atCanvas(200, 200, { buttons: 0 }));
    assert.equal(harness.canvas.classList.contains("dragging"), false);
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

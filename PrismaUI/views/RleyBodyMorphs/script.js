const canvas = document.getElementById("morphCanvas");
const ctx = canvas.getContext("2d");

const resetButton = document.getElementById("reset");
const snapToggleEl = document.getElementById("snapToggle");
const faceSliderNames = ["removeFaceMorph", "doubleChin", "fullerChin", "standaloneThin", "standaloneMuscular", "standaloneHeavy"];
const faceSliders = Object.fromEntries(faceSliderNames.map(name => [name, document.getElementById(name)]));

const center = { x: canvas.width / 2, y: canvas.height / 2 };
const radius = 138;
const anchorRadius = 6;
const MOUSE_POINTER_ID = -1;
const supportsCapturedPointerEvents =
    typeof window.PointerEvent !== "undefined" &&
    typeof canvas.setPointerCapture === "function" &&
    typeof canvas.hasPointerCapture === "function" &&
    typeof canvas.releasePointerCapture === "function";

let activePointerId = null;
let pointerStartPos = null;
let pointerStartTime = 0;
let pointerMoved = false;
let knob = { x: center.x, y: center.y };
let renderedKnob = { x: center.x, y: center.y };
let lastRenderTime = 0;

let morphPreviewTimeout = null;
let morphPreviewFrame = null;
let morphCommitTimeout = null;
let lastPreviewSendTime = 0;

// Body/face application is substantially more expensive than painting the UI.
// Keep previews frequent enough to feel live, but do not let native calls
// monopolize every other frame on a 60 Hz display.
const MORPH_PREVIEW_INTERVAL = 64;
const MORPH_COMMIT_DELAY = 120;
const MORPH_DELTA_THRESHOLD = 0.01;
const KNOB_RESPONSE_MS = 12;
const KNOB_SETTLE_EPSILON = 0.08;

// A press always jumps the knob to the cursor. It only starts *following* the
// cursor once the gesture proves itself a drag: either the cursor travelled far
// enough, or the button was held long enough. A quick click therefore places
// the knob and stops there, immune to the 1-3 px of hand tremor that a click
// normally carries.
const DRAG_START_DISTANCE = 4;
const DRAG_START_HOLD_MS = 140;

// Snap geometry, in canvas pixels against a 138 px wheel radius.
// Poles and the centre snap EXACTLY, so the model lands on 1.00 / 0.00
// rather than 0.997. Axis rails use hysteresis: easy to enter, harder to leave,
// which is what stops the knob chattering on and off a line.
const POLE_SNAP_RADIUS = 14;
const LINE_SNAP_ENTER = 10;
const LINE_SNAP_RELEASE = 22;

// Poles own the ends of each rail, so the rail yields there and the exact pole
// snap wins instead of the two fighting over the same pixels.
const RAIL_END_MARGIN = POLE_SNAP_RADIUS / radius;

let pendingMorphValues = { thin: 0, muscular: 0, fat: 0, removeFaceMorph: 0, doubleChin: 0, fullerChin: 0, standaloneThin: 0, standaloneMuscular: 0, standaloneHeavy: 0 };
let lastPreviewSentValues = null;
let lastCommitSentValues = null;
let morphTraceSequence = 0;
let snapEnabled = true;
let drawFramePending = false;

let snapLineState = {
    activeName: null,
    alpha: 0,
    targetAlpha: 0,
    isSnapped: false,
    isBlinking: false,
    slowFade: false
};

let animationFramePending = false;
let stopSlidingTimeout = null;
const STOP_SLIDING_DELAY = 120;

const anchors = {
    muscular: { x: center.x, y: center.y - radius },
    thin: {
        x: center.x - Math.cos(Math.PI / 6) * radius,
        y: center.y + Math.sin(Math.PI / 6) * radius
    },
    fat: {
        x: center.x + Math.cos(Math.PI / 6) * radius,
        y: center.y + Math.sin(Math.PI / 6) * radius
    }
};

const GUIDE_RING_COUNT = 5;
const guideRingRadii = Array.from(
    { length: GUIDE_RING_COUNT },
    (_, i) => radius * ((i + 1) / GUIDE_RING_COUNT)
);

const slotGrid = [
    { r: (guideRingRadii[0] + guideRingRadii[1]) * 0.5, segments: 18 },
    { r: (guideRingRadii[1] + guideRingRadii[2]) * 0.5, segments: 18 },
    { r: (guideRingRadii[2] + guideRingRadii[3]) * 0.5, segments: 18 },
    { r: (guideRingRadii[3] + guideRingRadii[4]) * 0.5, segments: 18 }
];

const TAU = Math.PI * 2;
const EPS = 0.00001;

// Every ring uses the same segment count, so the per-slot geometry is fixed for
// the life of the page. It used to be recomputed on every one of the ~144 slot
// fills per frame.
const SLOT_SEGMENTS = slotGrid[0].segments;
const SLOT_ARC = (TAU / SLOT_SEGMENTS) * 1.001;
const SLOT_HALF_THICKNESS = (guideRingRadii[1] - guideRingRadii[0]) * 0.499;
const SLOT_ANGLES = Array.from(
    { length: SLOT_SEGMENTS },
    (_, i) => (-Math.PI / 2) + (i / SLOT_SEGMENTS) * TAU
);

const ANGLES = {
    heavy: Math.PI / 6,
    thin: (5 * Math.PI) / 6,
    muscular: (3 * Math.PI) / 2
};

function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
}

function length(x, y) {
    return Math.sqrt(x * x + y * y);
}

function dot(ax, ay, bx, by) {
    return ax * bx + ay * by;
}

function angularDifference(a, b) {
    let d = a - b;
    while (d > Math.PI) d -= Math.PI * 2;
    while (d < -Math.PI) d += Math.PI * 2;
    return Math.abs(d);
}

function wrapAngle(angle) {
    while (angle < 0) angle += TAU;
    while (angle >= TAU) angle -= TAU;
    return angle;
}

function isNearPoint(x1, y1, x2, y2, epsilon = 0.5) {
    return length(x1 - x2, y1 - y2) <= epsilon;
}

function projectPointOntoSegment(px, py, ax, ay, bx, by) {
    const abx = bx - ax;
    const aby = by - ay;
    const apx = px - ax;
    const apy = py - ay;

    const abLenSq = abx * abx + aby * aby;
    if (abLenSq <= EPS) {
        return { x: ax, y: ay, t: 0, distance: length(px - ax, py - ay) };
    }

    let t = dot(apx, apy, abx, aby) / abLenSq;
    t = clamp(t, 0, 1);

    const x = ax + abx * t;
    const y = ay + aby * t;
    const distance = length(px - x, py - y);

    return { x, y, t, distance };
}

function stepSnapLineAnimation() {
    const speed = (snapLineState.targetAlpha === 0 && snapLineState.slowFade)
        ? 0.025
        : 0.065;

    snapLineState.alpha += (snapLineState.targetAlpha - snapLineState.alpha) * speed;

    if (Math.abs(snapLineState.targetAlpha - snapLineState.alpha) < 0.01) {
        snapLineState.alpha = snapLineState.targetAlpha;

        if (snapLineState.alpha <= 0.001) {
            snapLineState.alpha = 0;
            snapLineState.slowFade = false;

            // Only forget which rail was active once we are no longer
            // snapped to it; an idle fade must preserve the hysteresis.
            if (!snapLineState.isSnapped) {
                snapLineState.activeName = null;
            }
        }
    }
}

function stepKnobAnimation(timestamp) {
    const elapsed = lastRenderTime > 0
        ? clamp(timestamp - lastRenderTime, 0, 50)
        : 16.67;
    lastRenderTime = timestamp;

    const response = 1 - Math.exp(-elapsed / KNOB_RESPONSE_MS);
    renderedKnob.x += (knob.x - renderedKnob.x) * response;
    renderedKnob.y += (knob.y - renderedKnob.y) * response;

    const remaining = length(knob.x - renderedKnob.x, knob.y - renderedKnob.y);
    if (remaining <= KNOB_SETTLE_EPSILON) {
        renderedKnob.x = knob.x;
        renderedKnob.y = knob.y;
        return false;
    }
    return true;
}

// One scheduler, one repaint per frame. These were two independent
// requestAnimationFrame paths, so dragging while the snap line animated queued
// two full canvas repaints in the same frame.
function scheduleFrame(animate) {
    if (animate) animationFramePending = true;

    if (drawFramePending) return;

    drawFramePending = true;
    requestAnimationFrame((timestamp) => {
        drawFramePending = false;

        const animating = animationFramePending;
        animationFramePending = false;

        if (animating) stepSnapLineAnimation();
        const knobAnimating = stepKnobAnimation(timestamp);

        draw();

        const snapAnimating = animating &&
            Math.abs(snapLineState.targetAlpha - snapLineState.alpha) > 0.001;
        if (snapAnimating || knobAnimating) {
            scheduleFrame(true);
        }
    });
}

function requestAnimatedRedraw() {
    scheduleFrame(true);
}

function requestDraw() {
    scheduleFrame(false);
}

function setSnapLineHighlight(name, mode) {
    if (mode === "blink") {
        snapLineState.activeName = name;
        snapLineState.isSnapped = true;
        snapLineState.isBlinking = true;
        snapLineState.slowFade = true;

        snapLineState.alpha = 1;
        snapLineState.targetAlpha = 0;

        requestAnimatedRedraw();

        setTimeout(() => {
            if (snapLineState.isSnapped) {
                snapLineState.targetAlpha = 1;
                snapLineState.isBlinking = false;
                requestAnimatedRedraw();
            }
        }, 120);
    } else if (mode === "hold") {
        snapLineState.activeName = name;
        snapLineState.isSnapped = true;
        snapLineState.isBlinking = false;
        snapLineState.slowFade = false;
        snapLineState.targetAlpha = 1;
        requestAnimatedRedraw();
    } else if (mode === "idle") {
        // Fade the rail out while the knob rests, but stay logically snapped.
        // Clearing isSnapped here would reset the hysteresis, so pausing on a
        // rail and then nudging would pop the knob off it.
        snapLineState.isBlinking = false;
        snapLineState.slowFade = true;
        snapLineState.targetAlpha = 0;
        requestAnimatedRedraw();
    } else {
        snapLineState.isSnapped = false;
        snapLineState.isBlinking = false;
        snapLineState.targetAlpha = 0;
        requestAnimatedRedraw();
    }
}

function markSlidingActivity() {
    if (stopSlidingTimeout !== null) {
        clearTimeout(stopSlidingTimeout);
    }

    stopSlidingTimeout = setTimeout(() => {
        stopSlidingTimeout = null;

        if (activePointerId !== null) {
            setSnapLineHighlight(snapLineState.activeName, "idle");
        }
    }, STOP_SLIDING_DELAY);
}

function getKnobPolar() {
    const px = knob.x - center.x;
    const py = knob.y - center.y;

    return {
        px,
        py,
        dist: length(px, py),
        radial: clamp(length(px, py) / radius, 0, 1),
        angle: wrapAngle(Math.atan2(py, px))
    };
}

function getSectorForAngle(angle) {
    if (angle >= ANGLES.thin && angle <= ANGLES.muscular) {
        return "thin-muscular";
    }

    if (angle >= ANGLES.muscular || angle <= ANGLES.heavy) {
        return "muscular-heavy";
    }

    return "heavy-thin";
}

function computeMorphsFromSector(angle, radial) {
    if (radial <= EPS) {
        return { thin: 0, muscular: 0, fat: 0 };
    }

    const sector = getSectorForAngle(angle);

    if (sector === "thin-muscular") {
        let t = (angle - ANGLES.thin) / (ANGLES.muscular - ANGLES.thin);
        t = clamp(t, 0, 1);

        return {
            thin: radial * (1 - t),
            muscular: radial * t,
            fat: 0
        };
    }

    if (sector === "muscular-heavy") {
        let unwrappedAngle = angle <= ANGLES.heavy ? angle + TAU : angle;

        let t = (unwrappedAngle - ANGLES.muscular) /
                ((ANGLES.heavy + TAU) - ANGLES.muscular);

        t = clamp(t, 0, 1);

        return {
            thin: 0,
            muscular: radial * (1 - t),
            fat: radial * t
        };
    }

    let t = (angle - ANGLES.heavy) / (ANGLES.thin - ANGLES.heavy);
    t = clamp(t, 0, 1);

    return {
        thin: radial * t,
        muscular: 0,
        fat: radial * (1 - t)
    };
}

function getExactSnapState() {
    if (isNearPoint(knob.x, knob.y, center.x, center.y)) {
        return { thin: 0, muscular: 0, fat: 0 };
    }

    if (isNearPoint(knob.x, knob.y, anchors.thin.x, anchors.thin.y)) {
        return { thin: 1, muscular: 0, fat: 0 };
    }

    if (isNearPoint(knob.x, knob.y, anchors.muscular.x, anchors.muscular.y)) {
        return { thin: 0, muscular: 1, fat: 0 };
    }

    if (isNearPoint(knob.x, knob.y, anchors.fat.x, anchors.fat.y)) {
        return { thin: 0, muscular: 0, fat: 1 };
    }

    return null;
}

function computeMorphsFromKnob() {
    const snapState = getExactSnapState();
    if (snapState) {
        return snapState;
    }

    const polar = getKnobPolar();
    return computeMorphsFromSector(polar.angle, polar.radial);
}

function updateKnobFromMorphs(thin, muscular, fat) {
    const total = thin + muscular + fat;

    if (total <= EPS) {
        knob.x = center.x;
        knob.y = center.y;
        renderedKnob.x = knob.x;
        renderedKnob.y = knob.y;
        requestDraw();
        return;
    }

    if (thin >= 0.999 && muscular <= 0.001 && fat <= 0.001) {
        knob.x = anchors.thin.x;
        knob.y = anchors.thin.y;
        renderedKnob.x = knob.x;
        renderedKnob.y = knob.y;
        requestDraw();
        return;
    }

    if (muscular >= 0.999 && thin <= 0.001 && fat <= 0.001) {
        knob.x = anchors.muscular.x;
        knob.y = anchors.muscular.y;
        renderedKnob.x = knob.x;
        renderedKnob.y = knob.y;
        requestDraw();
        return;
    }

    if (fat >= 0.999 && thin <= 0.001 && muscular <= 0.001) {
        knob.x = anchors.fat.x;
        knob.y = anchors.fat.y;
        renderedKnob.x = knob.x;
        renderedKnob.y = knob.y;
        requestDraw();
        return;
    }

    let angle = null;
    let radial = clamp(total, 0, 1);

    if (fat <= EPS && (thin > EPS || muscular > EPS)) {
        const sectorTotal = thin + muscular;
        const t = sectorTotal > EPS ? muscular / sectorTotal : 0;
        angle = ANGLES.thin + (ANGLES.muscular - ANGLES.thin) * t;
        radial = clamp(sectorTotal, 0, 1);
    } else if (thin <= EPS && (muscular > EPS || fat > EPS)) {
        const sectorTotal = muscular + fat;
        const t = sectorTotal > EPS ? fat / sectorTotal : 0;
        angle = ANGLES.muscular + ((ANGLES.heavy + TAU) - ANGLES.muscular) * t;
        angle = wrapAngle(angle);
        radial = clamp(sectorTotal, 0, 1);
    } else if (muscular <= EPS && (thin > EPS || fat > EPS)) {
        const sectorTotal = thin + fat;
        const t = sectorTotal > EPS ? thin / sectorTotal : 0;
        angle = ANGLES.heavy + (ANGLES.thin - ANGLES.heavy) * t;
        radial = clamp(sectorTotal, 0, 1);
    } else {
        const axisScale = total > 1 ? 1 / total : 1;
        const centerWeight = total < 1 ? 1 - total : 0;
        const nx =
            center.x * centerWeight +
            anchors.thin.x * thin * axisScale +
            anchors.muscular.x * muscular * axisScale +
            anchors.fat.x * fat * axisScale;

        const ny =
            center.y * centerWeight +
            anchors.thin.y * thin * axisScale +
            anchors.muscular.y * muscular * axisScale +
            anchors.fat.y * fat * axisScale;

        knob.x = nx;
        knob.y = ny;
        renderedKnob.x = knob.x;
        renderedKnob.y = knob.y;
        requestDraw();
        return;
    }

    knob.x = center.x + Math.cos(angle) * (radial * radius);
    knob.y = center.y + Math.sin(angle) * (radial * radius);
    renderedKnob.x = knob.x;
    renderedKnob.y = knob.y;
    requestDraw();
}

function approxEqual(a, b, epsilon = 0.02) {
    return Math.abs(a - b) <= epsilon;
}

function runBlendModelSelfTests() {
    const tests = [];

    tests.push(() => {
        const result = computeMorphsFromSector(ANGLES.muscular, 1);
        return approxEqual(result.thin, 0) &&
            approxEqual(result.muscular, 1) &&
            approxEqual(result.fat, 0);
    });

    tests.push(() => {
        const result = computeMorphsFromSector(ANGLES.muscular, 0);
        return approxEqual(result.thin, 0) &&
            approxEqual(result.muscular, 0) &&
            approxEqual(result.fat, 0);
    });

    tests.push(() => {
        const midAngle = (ANGLES.thin + ANGLES.muscular) * 0.5;
        const result = computeMorphsFromSector(midAngle, 1);
        return approxEqual(result.thin, 0.5) &&
            approxEqual(result.muscular, 0.5) &&
            approxEqual(result.fat, 0);
    });

    tests.push(() => {
        const midAngle = (ANGLES.thin + ANGLES.muscular) * 0.5;
        const result = computeMorphsFromSector(midAngle, 0.5);
        return approxEqual(result.thin, 0.25) &&
            approxEqual(result.muscular, 0.25) &&
            approxEqual(result.fat, 0);
    });

    const passed = tests.every(fn => fn());
    if (!passed) {
        console.warn("Blend model self-tests failed");
    }
}

function sanitize(value) {
    if (isNaN(value)) return 0;
    return clamp(value, 0, 1);
}

function cloneMorphValues(values) {
    return {
        thin: sanitize(values.thin),
        muscular: sanitize(values.muscular),
        fat: sanitize(values.fat),
        removeFaceMorph: sanitize(values.removeFaceMorph),
        doubleChin: sanitize(values.doubleChin),
        fullerChin: sanitize(values.fullerChin),
        standaloneThin: sanitize(values.standaloneThin),
        standaloneMuscular: sanitize(values.standaloneMuscular),
        standaloneHeavy: sanitize(values.standaloneHeavy)
    };
}

function hasMeaningfulDelta(a, b) {
    if (!a || !b) return true;

    return (
        Math.abs(a.thin - b.thin) > MORPH_DELTA_THRESHOLD ||
        Math.abs(a.muscular - b.muscular) > MORPH_DELTA_THRESHOLD ||
        Math.abs(a.fat - b.fat) > MORPH_DELTA_THRESHOLD ||
        faceSliderNames.some(name => Math.abs(a[name] - b[name]) > MORPH_DELTA_THRESHOLD)
    );
}

function nextMorphTraceId(source) {
    morphTraceSequence += 1;
    return `${source}-${Date.now()}-${morphTraceSequence}`;
}

function sendPreviewMorphs(values) {
    const safe = cloneMorphValues(values);

    if (!hasMeaningfulDelta(safe, lastPreviewSentValues)) {
        return;
    }

    const traceId = nextMorphTraceId("preview");
    window.onMorphsPreview(`${safe.thin},${safe.muscular},${safe.fat},${safe.removeFaceMorph},${safe.doubleChin},${safe.fullerChin},${safe.standaloneThin},${safe.standaloneMuscular},${safe.standaloneHeavy},${traceId}`);
    lastPreviewSentValues = safe;
}

function sendCommitMorphs(values) {
    const safe = cloneMorphValues(values);

    if (!hasMeaningfulDelta(safe, lastCommitSentValues)) {
        return;
    }

    const traceId = nextMorphTraceId("commit");
    window.onMorphsCommit(`${safe.thin},${safe.muscular},${safe.fat},${safe.removeFaceMorph},${safe.doubleChin},${safe.fullerChin},${safe.standaloneThin},${safe.standaloneMuscular},${safe.standaloneHeavy},${traceId}`);
    lastCommitSentValues = safe;
    lastPreviewSentValues = cloneMorphValues(safe);
}

function updateWheelReadout(values) {
    // The wheel itself communicates the blend; keeping this hook avoids
    // coupling morph dispatch to optional numeric UI.
}

function updateFaceReadout(values) {
    faceSliderNames.forEach(name => {
        faceSliders[name].value = Math.round(values[name] * 100);
        document.getElementById(`${name}Value`).textContent = `${Math.round(values[name] * 100)}%`;
    });
}

function updateReadout(values) {
    updateWheelReadout(values);
    updateFaceReadout(values);
}

window.restoreMorphs = function(payload) {
    const parts = payload.split(",");

    const thin = parseFloat(parts[0]) || 0;
    const muscular = parseFloat(parts[1]) || 0;
    const fat = parseFloat(parts[2]) || 0;

    const restored = { thin, muscular, fat };
    faceSliderNames.forEach((name, index) => restored[name] = parseFloat(parts[index + 3]) || 0);

    pendingMorphValues = cloneMorphValues(restored);
    lastPreviewSentValues = cloneMorphValues(restored);
    lastCommitSentValues = cloneMorphValues(restored);

    updateKnobFromMorphs(thin, muscular, fat);
    updateReadout(restored);
};

window.invalidateMorphDispatchState = function() {
    lastPreviewSentValues = null;
    lastCommitSentValues = null;
};


function scheduleMorphCommit() {
    if (morphCommitTimeout !== null) {
        clearTimeout(morphCommitTimeout);
    }

    morphCommitTimeout = setTimeout(() => {
        sendCommitMorphs(pendingMorphValues);
        morphCommitTimeout = null;
    }, MORPH_COMMIT_DELAY);
}

function flushCommitNow() {
    if (morphCommitTimeout !== null) {
        clearTimeout(morphCommitTimeout);
        morphCommitTimeout = null;
    }

    sendCommitMorphs(pendingMorphValues);
}

function cancelPendingPreview() {
    if (morphPreviewTimeout !== null) {
        clearTimeout(morphPreviewTimeout);
        morphPreviewTimeout = null;
    }

    if (morphPreviewFrame !== null) {
        cancelAnimationFrame(morphPreviewFrame);
        morphPreviewFrame = null;
    }
}

// Native morph application blocks Prisma's browser thread. Queue it after a
// paint opportunity so pointer input and the puck are visually processed
// first. Repeated moves only replace pending values; they never build a queue.
function schedulePreviewAfterPaint(delay) {
    if (morphPreviewTimeout !== null || morphPreviewFrame !== null) return;

    morphPreviewTimeout = setTimeout(() => {
        morphPreviewTimeout = null;
        morphPreviewFrame = requestAnimationFrame(() => {
            morphPreviewFrame = null;
            morphPreviewTimeout = setTimeout(() => {
                morphPreviewTimeout = null;
                sendPreviewMorphs(pendingMorphValues);
                lastPreviewSendTime = performance.now();
            }, 0);
        });
    }, Math.max(0, delay));
}

function scheduleMorphUpdate(options = {}) {
    const commit = !!options.commit;
    const preview = options.preview !== false;

    const faceValues = {};
    faceSliderNames.forEach(name => { faceValues[name] = pendingMorphValues[name] || 0; });
    pendingMorphValues = { ...computeMorphsFromKnob(), ...faceValues };
    updateWheelReadout(pendingMorphValues);

    if (!preview) {
        cancelPendingPreview();
    } else {
        const now = performance.now();
        const elapsed = now - lastPreviewSendTime;
        schedulePreviewAfterPaint(MORPH_PREVIEW_INTERVAL - elapsed);
    }

    if (commit) {
        scheduleMorphCommit();
    }
}

function drawLabel(text, x, y) {
    ctx.save();
    ctx.font = "500 15px Segoe UI, Arial, sans-serif";
    ctx.fillStyle = "rgba(210,210,210,0.88)";
    ctx.textAlign = "center";
    ctx.fillText(text, x, y);
    ctx.restore();
}

function drawAnchor(x, y) {
    ctx.beginPath();
    ctx.arc(x, y, anchorRadius, 0, Math.PI * 2);
    ctx.fillStyle = "rgba(205,205,205,0.82)";
    ctx.fill();
}

function drawCorePuck() {
    ctx.save();

    ctx.beginPath();
    ctx.arc(center.x, center.y, 15, 0, Math.PI * 2);
    ctx.fillStyle = "rgba(220,220,220,0.96)";
    ctx.fill();

    ctx.beginPath();
    ctx.arc(center.x, center.y, 15, 0, Math.PI * 2);
    ctx.strokeStyle = "rgba(205,205,205,0.55)";
    ctx.lineWidth = 1.2;
    ctx.stroke();

    ctx.beginPath();
    ctx.arc(center.x, center.y, 4.2, 0, Math.PI * 2);
    ctx.fillStyle = "rgba(55,55,55,0.95)";
    ctx.fill();

    ctx.restore();
}

function drawSelectionDot() {
    const dragging = activePointerId !== null;

    ctx.save();

    // A halo while snapped tells you the value is locked to a rail or a pole,
    // which is otherwise only inferable from the wheel position.
    if (snapLineState.isSnapped) {
        ctx.beginPath();
        ctx.arc(renderedKnob.x, renderedKnob.y, 9.5, 0, Math.PI * 2);
        ctx.strokeStyle = "rgba(240,240,240,0.55)";
        ctx.lineWidth = 1.4;
        ctx.stroke();
    }

    ctx.beginPath();
    ctx.arc(renderedKnob.x, renderedKnob.y, dragging ? 6.4 : 5.2, 0, Math.PI * 2);
    ctx.fillStyle = "rgba(35,35,35,0.98)";
    ctx.fill();

    ctx.restore();
}

function drawSnapLineHighlight() {
    if (!snapLineState.activeName || snapLineState.alpha <= 0.001) return;

    const anchor = anchors[snapLineState.activeName];
    if (!anchor) return;

    ctx.save();
    ctx.beginPath();
    ctx.moveTo(center.x, center.y);
    ctx.lineTo(anchor.x, anchor.y);
    ctx.strokeStyle = `rgba(235,235,235,${(0.20 * snapLineState.alpha).toFixed(3)})`;
    ctx.lineWidth = 2.4;
    ctx.stroke();
    ctx.restore();
}

function drawSlotShape(ringRadius, segmentIndex, alpha) {
    const a = SLOT_ANGLES[segmentIndex];
    const segArc = SLOT_ARC;

    let inner = ringRadius - SLOT_HALF_THICKNESS;
    let outer = ringRadius + SLOT_HALF_THICKNESS;

    outer = Math.min(outer, radius - 1);
    if (inner >= outer) return;

    ctx.beginPath();
    ctx.arc(center.x, center.y, outer, a - segArc / 2, a + segArc / 2);
    ctx.arc(center.x, center.y, inner, a + segArc / 2, a - segArc / 2, true);
    ctx.closePath();
    ctx.fillStyle = `rgba(225,225,225,${alpha.toFixed(3)})`;
    ctx.fill();
}

const SLOT_BASE_ALPHA = 0.015;
const SLOT_ANGULAR_SPREAD = 0.20;

// The dim base grid and the bright halo around the knob used to be two passes,
// so every lit slot was filled twice. One pass fills each slot exactly once at
// whichever alpha wins, which halves the path count in the worst case.
function drawSlotGrid() {
    const dx = renderedKnob.x - center.x;
    const dy = renderedKnob.y - center.y;
    const knobDist = length(dx, dy);
    const lit = knobDist >= 6;

    const knobAngle = lit ? Math.atan2(dy, dx) : 0;
    const knobRingPos = lit ? slotRingPosition(knobDist) : 0;

    const spreadDenom = 2 * SLOT_ANGULAR_SPREAD * SLOT_ANGULAR_SPREAD;

    for (let r = 0; r < slotGrid.length; r++) {
        const ring = slotGrid[r];
        const radialWeight = lit ? Math.exp(-Math.abs(r - knobRingPos) * 0.95) : 0;

        for (let i = 0; i < SLOT_SEGMENTS; i++) {
            let alpha = SLOT_BASE_ALPHA;

            if (lit) {
                const angDelta = angularDifference(SLOT_ANGLES[i], knobAngle);
                const brightness =
                    radialWeight * Math.exp(-(angDelta * angDelta) / spreadDenom);

                if (brightness >= 0.05) {
                    alpha = clamp(
                        0.14 + Math.pow(brightness, 0.72) * 0.82,
                        SLOT_BASE_ALPHA,
                        0.96
                    );
                }
            }

            drawSlotShape(ring.r, i, alpha);
        }
    }
}

// Where the knob sits on the ring stack, as a fractional ring index.
function slotRingPosition(knobDist) {
    const last = slotGrid.length - 1;

    if (knobDist <= slotGrid[0].r) return 0;
    if (knobDist >= slotGrid[last].r) return last;

    for (let i = 0; i < last; i++) {
        const a = slotGrid[i].r;
        const b = slotGrid[i + 1].r;

        if (knobDist >= a && knobDist <= b) {
            return i + (knobDist - a) / (b - a);
        }
    }

    return 0;
}

function draw() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    for (let i = 0; i < guideRingRadii.length; i++) {
        const r = guideRingRadii[i];
        ctx.beginPath();
        ctx.arc(center.x, center.y, r, 0, Math.PI * 2);
        ctx.strokeStyle = i === guideRingRadii.length - 1
            ? "rgba(205,205,205,0.52)"
            : "rgba(205,205,205,0.08)";
        ctx.lineWidth = i === guideRingRadii.length - 1 ? 1.6 : 1;
        ctx.stroke();
    }

    ctx.beginPath();
    ctx.arc(center.x, center.y, 2, 0, Math.PI * 2);
    ctx.fillStyle = "rgba(210,210,210,0.55)";
    ctx.fill();

    drawSnapLineHighlight();
    drawSlotGrid();

    drawAnchor(anchors.muscular.x, anchors.muscular.y);
    drawAnchor(anchors.thin.x, anchors.thin.y);
    drawAnchor(anchors.fat.x, anchors.fat.y);

    drawLabel("MUSCULAR", anchors.muscular.x, anchors.muscular.y - 22);
    drawLabel("THIN", anchors.thin.x - 38, anchors.thin.y + 24);
    drawLabel("HEAVY", anchors.fat.x + 38, anchors.fat.y + 24);

    drawCorePuck();
    drawSelectionDot();
}

function getCanvasPos(event) {
    const rect = canvas.getBoundingClientRect();
    const scaleX = rect.width > 0 ? canvas.width / rect.width : 1;
    const scaleY = rect.height > 0 ? canvas.height / rect.height : 1;

    return {
        x: (event.clientX - rect.left) * scaleX,
        y: (event.clientY - rect.top) * scaleY
    };
}

function setKnobFromPoint(x, y, options = {}) {
    let dx = x - center.x;
    let dy = y - center.y;
    const dist = length(dx, dy);

    if (dist > radius) {
        const scale = radius / dist;
        dx *= scale;
        dy *= scale;
    }

    let targetX = center.x + dx;
    let targetY = center.y + dy;

    // Shift suspends snapping for as long as it is held, so a value between two
    // rails stays reachable without toggling the Snap checkbox.
    if (!snapEnabled || options.freeMove) {
        setSnapLineHighlight(null, "off");
        knob.x = targetX;
        knob.y = targetY;
        // Pointer motion already arrives as a stream. Filtering that stream a
        // second time makes the puck visibly trail the cursor, especially in
        // Prisma's embedded browser. Interpolation remains useful for restores
        // and other non-pointer changes, but direct manipulation must be 1:1.
        if (activePointerId !== null) {
            renderedKnob.x = knob.x;
            renderedKnob.y = knob.y;
        }
        markSlidingActivity();
        requestAnimatedRedraw();
        scheduleMorphUpdate(options);
        return;
    }

    const snapped = resolveSnap(targetX, targetY);

    knob.x = snapped.x;
    knob.y = snapped.y;

    if (activePointerId !== null) {
        renderedKnob.x = knob.x;
        renderedKnob.y = knob.y;
    }

    if (snapped.line) {
        setSnapLineHighlight(snapped.line, snapped.entering ? "blink" : "hold");
    } else {
        setSnapLineHighlight(null, "off");
    }

    markSlidingActivity();

    requestAnimatedRedraw();
    scheduleMorphUpdate(options);
}

// Pure geometry: given a candidate position, return where the knob should
// actually sit. Poles and the centre win outright and land exactly; otherwise
// the point is projected exactly onto the nearest axis rail if it is within the
// hysteresis threshold for that rail.
function resolveSnap(targetX, targetY) {
    const snapPoints = [
        { name: "thin", x: anchors.thin.x, y: anchors.thin.y },
        { name: "muscular", x: anchors.muscular.x, y: anchors.muscular.y },
        { name: "fat", x: anchors.fat.x, y: anchors.fat.y },
        { name: "center", x: center.x, y: center.y }
    ];

    let closestPoint = null;
    let closestPointDist = Infinity;

    for (const p of snapPoints) {
        const d = length(targetX - p.x, targetY - p.y);

        if (d < closestPointDist) {
            closestPointDist = d;
            closestPoint = p;
        }
    }

    if (closestPointDist < POLE_SNAP_RADIUS) {
        const onPole = closestPoint.name !== "center";

        return {
            x: closestPoint.x,
            y: closestPoint.y,
            line: onPole ? closestPoint.name : null,
            entering: onPole &&
                !(snapLineState.isSnapped && snapLineState.activeName === closestPoint.name)
        };
    }

    const lineCandidates = [
        { name: "thin", anchor: anchors.thin },
        { name: "muscular", anchor: anchors.muscular },
        { name: "fat", anchor: anchors.fat }
    ];

    let bestLine = null;
    let bestProjection = null;

    for (const candidate of lineCandidates) {
        const proj = projectPointOntoSegment(
            targetX, targetY,
            center.x, center.y,
            candidate.anchor.x, candidate.anchor.y
        );

        if (proj.t <= RAIL_END_MARGIN || proj.t >= 1 - RAIL_END_MARGIN) continue;

        if (!bestProjection || proj.distance < bestProjection.distance) {
            bestProjection = proj;
            bestLine = candidate;
        }
    }

    if (!bestProjection) {
        return { x: targetX, y: targetY, line: null, entering: false };
    }

    const alreadyOnSameLine =
        snapLineState.isSnapped && snapLineState.activeName === bestLine.name;

    const threshold = alreadyOnSameLine ? LINE_SNAP_RELEASE : LINE_SNAP_ENTER;

    if (bestProjection.distance >= threshold) {
        return { x: targetX, y: targetY, line: null, entering: false };
    }

    return {
        x: bestProjection.x,
        y: bestProjection.y,
        line: bestLine.name,
        entering: !alreadyOnSameLine
    };
}

function finishPointerInteraction(event) {
    if (activePointerId === null) return;
    if (event && typeof event.pointerId === "number" && event.pointerId !== activePointerId) return;

    if (event) {
        event.preventDefault();
        event.stopPropagation();
    }

    const pointerId = activePointerId;
    // Clear first: releasePointerCapture dispatches lostpointercapture, so the
    // finish path must already be inert when that event arrives.
    activePointerId = null;
    pointerStartPos = null;
    pointerStartTime = 0;
    pointerMoved = false;

    canvas.classList.remove("dragging");

    detachPointerFallbackListeners();
    detachMouseFallbackListeners();

    if (stopSlidingTimeout !== null) {
        clearTimeout(stopSlidingTimeout);
        stopSlidingTimeout = null;
    }

    if (supportsCapturedPointerEvents &&
        pointerId !== MOUSE_POINTER_ID &&
        canvas.hasPointerCapture(pointerId)) {
        try {
            canvas.releasePointerCapture(pointerId);
        } catch (_) {
            // Capture can already be gone after a native cancel or focus loss.
        }
    }

    // The final commit contains the same current values as a final preview.
    // Sending both back-to-back stalls two frames and applies no extra state.
    cancelPendingPreview();
    flushCommitNow();

    setSnapLineHighlight(null, "off");
}

// Um move que reporta buttons=0 so significa "soltou" se ESTE host mantiver a
// mascara de botao nos moves. O PrismaUI injeta todo mouse-move com
// button=kButton_None (InputHandler.cpp, os dois unicos kType_MouseMoved), sem
// consultar o g_mouseButtonStates que ele mesmo mantem para down/up - entao
// buttons le 0 durante o arrasto inteiro, e tratar isso como release encerrava
// o gesto no primeiro movimento. Clique funcionava, arrasto nao.
//
// A capacidade se aprende SO DOS MOVES. Aprender do press seria aprender a
// resposta errada: o press carrega um botao de verdade mesmo nos hosts que o
// descartam depois.
let hostReportsHeldButtons = false;

function movingWithButtonReleased(event) {
    if (typeof event.buttons !== "number") return false;

    if ((event.buttons & 1) !== 0) {
        hostReportsHeldButtons = true;
        return false;
    }

    return hostReportsHeldButtons;
}

function updatePointerPosition(event, preview) {
    const pos = getCanvasPos(event);

    // A press becomes a drag once it has travelled far enough or been held long
    // enough. Until then the knob stays where the press put it, so a quick
    // click cannot be nudged off target by tremor.
    if (!pointerMoved && pointerStartPos) {
        const travelled =
            length(pos.x - pointerStartPos.x, pos.y - pointerStartPos.y) >= DRAG_START_DISTANCE;
        const held = performance.now() - pointerStartTime >= DRAG_START_HOLD_MS;

        if (travelled || held) {
            pointerMoved = true;
        }
    }

    if (pointerMoved) {
        setKnobFromPoint(pos.x, pos.y, { preview, freeMove: !!event.shiftKey });
    }
}

function beginPointerInteraction(event, pointerId) {
    if (activePointerId !== null) {
        finishPointerInteraction();
    }

    event.preventDefault();
    event.stopPropagation();

    activePointerId = pointerId;
    pointerStartPos = getCanvasPos(event);
    pointerStartTime = performance.now();
    pointerMoved = false;

    canvas.classList.add("dragging");

    setKnobFromPoint(pointerStartPos.x, pointerStartPos.y, {
        preview: false,
        freeMove: !!event.shiftKey
    });
}

function handlePointerDown(event) {
    if (event.button !== 0) return;
    if (typeof event.isPrimary === "boolean" && !event.isPrimary) return;

    beginPointerInteraction(event, event.pointerId);

    try {
        canvas.setPointerCapture(event.pointerId);
        if (!canvas.hasPointerCapture(event.pointerId)) {
            attachPointerFallbackListeners();
        }
    } catch (_) {
        attachPointerFallbackListeners();
    }
}

function handlePointerMove(event) {
    if (event.pointerId !== activePointerId) return;

    event.preventDefault();
    event.stopPropagation();

    if (movingWithButtonReleased(event)) {
        updatePointerPosition(event, false);
        finishPointerInteraction(event);
        return;
    }

    updatePointerPosition(event, true);
}

function handlePointerUp(event) {
    if (event.pointerId !== activePointerId) return;

    updatePointerPosition(event, false);
    finishPointerInteraction(event);
}

function attachPointerFallbackListeners() {
    window.addEventListener("pointermove", handlePointerMove, true);
    window.addEventListener("pointerup", handlePointerUp, true);
    window.addEventListener("pointercancel", finishPointerInteraction, true);
}

function detachPointerFallbackListeners() {
    window.removeEventListener("pointermove", handlePointerMove, true);
    window.removeEventListener("pointerup", handlePointerUp, true);
    window.removeEventListener("pointercancel", finishPointerInteraction, true);
}

function attachMouseFallbackListeners() {
    window.addEventListener("mousemove", handleMouseMove, true);
    window.addEventListener("mouseup", handleMouseUp, true);
}

function detachMouseFallbackListeners() {
    window.removeEventListener("mousemove", handleMouseMove, true);
    window.removeEventListener("mouseup", handleMouseUp, true);
}

function handleMouseDown(event) {
    if (event.button !== 0) return;

    // Some embedded hosts expose PointerEvent but inject only MouseEvent. If a
    // real pointerdown already started the gesture, consume its compatibility
    // mousedown; otherwise this becomes the runtime mouse fallback.
    if (activePointerId !== null && activePointerId !== MOUSE_POINTER_ID) {
        event.preventDefault();
        event.stopPropagation();
        return;
    }

    beginPointerInteraction(event, MOUSE_POINTER_ID);
    attachMouseFallbackListeners();
}

function handleMouseMove(event) {
    if (activePointerId !== MOUSE_POINTER_ID) return;

    event.preventDefault();
    event.stopPropagation();

    if (movingWithButtonReleased(event)) {
        updatePointerPosition(event, false);
        finishPointerInteraction(event);
        return;
    }

    updatePointerPosition(event, true);
}

function handleMouseUp(event) {
    if (activePointerId !== MOUSE_POINTER_ID) return;

    if (event.button !== 0 &&
        (typeof event.buttons !== "number" || (event.buttons & 1) !== 0)) {
        return;
    }

    updatePointerPosition(event, false);
    finishPointerInteraction(event);
}

function handleMouseLeave(event) {
    if (activePointerId !== MOUSE_POINTER_ID) return;

    // Leaving the canvas mid-drag is normal: the pole anchors sit near the rim,
    // so overshooting them is the common case. The window-level fallback
    // listeners keep feeding moves, so only treat this as the end of the
    // gesture when the button is genuinely no longer held - e num host que nao
    // reporta a mascara, "genuinamente" nunca pode ser deduzido de buttons=0.
    if (!movingWithButtonReleased(event)) {
        updatePointerPosition(event, true);
        return;
    }

    updatePointerPosition(event, false);
    finishPointerInteraction(event);
}

if (supportsCapturedPointerEvents) {
    canvas.addEventListener("pointerdown", handlePointerDown);
    canvas.addEventListener("pointermove", handlePointerMove);
    canvas.addEventListener("pointerup", handlePointerUp);
    canvas.addEventListener("pointercancel", finishPointerInteraction);
    canvas.addEventListener("lostpointercapture", finishPointerInteraction);
}

canvas.addEventListener("mousedown", handleMouseDown);
canvas.addEventListener("mouseleave", handleMouseLeave);

canvas.addEventListener("click", (event) => {
    event.preventDefault();
    event.stopPropagation();
});

canvas.addEventListener("contextmenu", (event) => {
    event.preventDefault();
    event.stopPropagation();
});

window.addEventListener("blur", () => {
    finishPointerInteraction();
});

resetButton.addEventListener("click", () => {
    const values = { thin: 0, muscular: 0, fat: 0 };
    pendingMorphValues = cloneMorphValues(values);
    updateKnobFromMorphs(values.thin, values.muscular, values.fat);
    updateReadout(values);

    cancelPendingPreview();
    flushCommitNow();
});

snapToggleEl.addEventListener("change", () => {
    snapEnabled = !!snapToggleEl.checked;
    if (!snapEnabled) {
        setSnapLineHighlight(null, "off");
        draw();
    }
});

faceSliderNames.forEach(name => {
    faceSliders[name].addEventListener("input", () => {
        pendingMorphValues[name] = clamp(parseFloat(faceSliders[name].value) / 100, 0, 1);
        document.getElementById(`${name}Value`).textContent = `${faceSliders[name].value}%`;
        scheduleMorphUpdate({ preview: true, commit: true });
    });
});

runBlendModelSelfTests();

updateKnobFromMorphs(0, 0, 0);
updateReadout({ thin: 0, muscular: 0, fat: 0 });

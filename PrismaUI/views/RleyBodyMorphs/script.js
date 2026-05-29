const canvas = document.getElementById("morphCanvas");
const ctx = canvas.getContext("2d");

const thinValueEl = document.getElementById("thinValue");
const muscularValueEl = document.getElementById("muscularValue");
const fatValueEl = document.getElementById("fatValue");
const resetButton = document.getElementById("reset");
const snapToggleEl = document.getElementById("snapToggle");

const center = { x: canvas.width / 2, y: canvas.height / 2 };
const radius = 138;
const anchorRadius = 6;

let dragging = false;
let knob = { x: center.x, y: center.y };

let morphPreviewTimeout = null;
let morphCommitTimeout = null;
let lastPreviewSendTime = 0;

const MORPH_PREVIEW_INTERVAL = 48;
const MORPH_COMMIT_DELAY = 120;
const MORPH_DELTA_THRESHOLD = 0.01;

let pendingMorphValues = { thin: 0, muscular: 0, fat: 0 };
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

function requestAnimatedRedraw() {
    if (animationFramePending) return;

    animationFramePending = true;
    requestAnimationFrame(() => {
        animationFramePending = false;

        const speed = (snapLineState.targetAlpha === 0 && snapLineState.slowFade)
            ? 0.025
            : 0.065;

        snapLineState.alpha += (snapLineState.targetAlpha - snapLineState.alpha) * speed;

        if (Math.abs(snapLineState.targetAlpha - snapLineState.alpha) < 0.01) {
            snapLineState.alpha = snapLineState.targetAlpha;

            if (snapLineState.alpha <= 0.001) {
                snapLineState.alpha = 0;
                snapLineState.activeName = null;
                snapLineState.slowFade = false;
            }
        }

        draw();

        if (Math.abs(snapLineState.targetAlpha - snapLineState.alpha) > 0.001) {
            requestAnimatedRedraw();
        }
    });
}

function requestDraw() {
    if (drawFramePending) return;

    drawFramePending = true;
    requestAnimationFrame(() => {
        drawFramePending = false;
        draw();
    });
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
        if (dragging) {
            setSnapLineHighlight(null, "off");
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
        draw();
        return;
    }

    if (thin >= 0.999 && muscular <= 0.001 && fat <= 0.001) {
        knob.x = anchors.thin.x;
        knob.y = anchors.thin.y;
        draw();
        return;
    }

    if (muscular >= 0.999 && thin <= 0.001 && fat <= 0.001) {
        knob.x = anchors.muscular.x;
        knob.y = anchors.muscular.y;
        draw();
        return;
    }

    if (fat >= 0.999 && thin <= 0.001 && muscular <= 0.001) {
        knob.x = anchors.fat.x;
        knob.y = anchors.fat.y;
        draw();
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
        const nx =
            anchors.thin.x * thin +
            anchors.muscular.x * muscular +
            anchors.fat.x * fat;

        const ny =
            anchors.thin.y * thin +
            anchors.muscular.y * muscular +
            anchors.fat.y * fat;

        knob.x = nx;
        knob.y = ny;
        draw();
        return;
    }

    knob.x = center.x + Math.cos(angle) * (radial * radius);
    knob.y = center.y + Math.sin(angle) * (radial * radius);
    draw();
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
        fat: sanitize(values.fat)
    };
}

function hasMeaningfulDelta(a, b) {
    if (!a || !b) return true;

    return (
        Math.abs(a.thin - b.thin) > MORPH_DELTA_THRESHOLD ||
        Math.abs(a.muscular - b.muscular) > MORPH_DELTA_THRESHOLD ||
        Math.abs(a.fat - b.fat) > MORPH_DELTA_THRESHOLD
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
    window.onMorphsPreview(`${safe.thin},${safe.muscular},${safe.fat},${traceId}`);
    lastPreviewSentValues = safe;
}

function sendCommitMorphs(values) {
    const safe = cloneMorphValues(values);

    if (!hasMeaningfulDelta(safe, lastCommitSentValues)) {
        return;
    }

    const traceId = nextMorphTraceId("commit");
    window.onMorphsCommit(`${safe.thin},${safe.muscular},${safe.fat},${traceId}`);
    lastCommitSentValues = safe;
}

function updateReadout(values) {
    thinValueEl.textContent = values.thin.toFixed(2);
    muscularValueEl.textContent = values.muscular.toFixed(2);
    fatValueEl.textContent = values.fat.toFixed(2);
}

window.restoreMorphs = function(payload) {
    const parts = payload.split(",");

    const thin = parseFloat(parts[0]) || 0;
    const muscular = parseFloat(parts[1]) || 0;
    const fat = parseFloat(parts[2]) || 0;

    const restored = { thin, muscular, fat };

    pendingMorphValues = cloneMorphValues(restored);
    lastPreviewSentValues = cloneMorphValues(restored);
    lastCommitSentValues = cloneMorphValues(restored);

    updateKnobFromMorphs(thin, muscular, fat);
    updateReadout(restored);
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

function flushPreviewNow() {
    if (morphPreviewTimeout !== null) {
        clearTimeout(morphPreviewTimeout);
        morphPreviewTimeout = null;
    }

    sendPreviewMorphs(pendingMorphValues);
    lastPreviewSendTime = performance.now();
}

function flushCommitNow() {
    if (morphCommitTimeout !== null) {
        clearTimeout(morphCommitTimeout);
        morphCommitTimeout = null;
    }

    sendCommitMorphs(pendingMorphValues);
}

function scheduleMorphUpdate(options = {}) {
    const commit = !!options.commit;

    pendingMorphValues = computeMorphsFromKnob();
    updateReadout(pendingMorphValues);

    const now = performance.now();
    const elapsed = now - lastPreviewSendTime;

    if (elapsed >= MORPH_PREVIEW_INTERVAL) {
        sendPreviewMorphs(pendingMorphValues);
        lastPreviewSendTime = now;

        if (morphPreviewTimeout !== null) {
            clearTimeout(morphPreviewTimeout);
            morphPreviewTimeout = null;
        }
    } else {
        if (morphPreviewTimeout === null) {
            morphPreviewTimeout = setTimeout(() => {
                sendPreviewMorphs(pendingMorphValues);
                lastPreviewSendTime = performance.now();
                morphPreviewTimeout = null;
            }, MORPH_PREVIEW_INTERVAL - elapsed);
        }
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
    ctx.save();
    ctx.beginPath();
    ctx.arc(knob.x, knob.y, 5.2, 0, Math.PI * 2);
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

function drawSlotShape(ringRadius, segmentCount, segmentIndex, alpha) {
    const a = (-Math.PI / 2) + (segmentIndex / segmentCount) * Math.PI * 2;

    const cellArc = (Math.PI * 2 / segmentCount);
    const segArc = cellArc * 1.001;

    const gap = guideRingRadii[1] - guideRingRadii[0];
    const halfThickness = gap * 0.499;

    let inner = ringRadius - halfThickness;
    let outer = ringRadius + halfThickness;

    outer = Math.min(outer, radius - 1);
    if (inner >= outer) return;

    ctx.beginPath();
    ctx.arc(center.x, center.y, outer, a - segArc / 2, a + segArc / 2);
    ctx.arc(center.x, center.y, inner, a + segArc / 2, a - segArc / 2, true);
    ctx.closePath();
    ctx.fillStyle = `rgba(225,225,225,${alpha.toFixed(3)})`;
    ctx.fill();
}

function drawBaseSlotGrid() {
    for (const ring of slotGrid) {
        for (let i = 0; i < ring.segments; i++) {
            drawSlotShape(ring.r, ring.segments, i, 0.015);
        }
    }
}

function drawActiveSlotHighlights() {
    const dx = knob.x - center.x;
    const dy = knob.y - center.y;
    const knobDist = length(dx, dy);

    if (knobDist < 6) return;

    const knobAngle = Math.atan2(dy, dx);
    const slotRadii = slotGrid.map(r => r.r);

    let knobRingPos = 0;
    if (knobDist <= slotRadii[0]) {
        knobRingPos = 0;
    } else if (knobDist >= slotRadii[slotRadii.length - 1]) {
        knobRingPos = slotRadii.length - 1;
    } else {
        for (let i = 0; i < slotRadii.length - 1; i++) {
            const a = slotRadii[i];
            const b = slotRadii[i + 1];
            if (knobDist >= a && knobDist <= b) {
                const t = (knobDist - a) / (b - a);
                knobRingPos = i + t;
                break;
            }
        }
    }

    for (let r = 0; r < slotGrid.length; r++) {
        const ring = slotGrid[r];
        const radialDelta = Math.abs(r - knobRingPos);

        const radialWeight = Math.exp(-radialDelta * 0.95);
        const baseSpread = 0.20;

        for (let i = 0; i < ring.segments; i++) {
            const a = (-Math.PI / 2) + (i / ring.segments) * Math.PI * 2;
            const angDelta = angularDifference(a, knobAngle);

            const angularWeight = Math.exp(
                -(angDelta * angDelta) / (2 * baseSpread * baseSpread)
            );

            let brightness = radialWeight * angularWeight;
            if (brightness < 0.05) continue;

            brightness = Math.pow(brightness, 0.72);

            const alpha = clamp(0.14 + brightness * 0.82, 0, 0.96);
            drawSlotShape(ring.r, ring.segments, i, alpha);
        }
    }
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
    drawBaseSlotGrid();
    drawActiveSlotHighlights();

    drawAnchor(anchors.muscular.x, anchors.muscular.y);
    drawAnchor(anchors.thin.x, anchors.thin.y);
    drawAnchor(anchors.fat.x, anchors.fat.y);

    drawLabel("MUSCULAR", anchors.muscular.x, anchors.muscular.y - 22);
    drawLabel("THIN", anchors.thin.x - 38, anchors.thin.y + 24);
    drawLabel("HEAVY", anchors.fat.x + 38, anchors.fat.y + 24);

    drawCorePuck();
    drawSelectionDot();
}

function getMousePos(event) {
    const rect = canvas.getBoundingClientRect();
    return {
        x: event.clientX - rect.left,
        y: event.clientY - rect.top
    };
}

function setKnobFromPoint(x, y) {
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

    if (!snapEnabled) {
        setSnapLineHighlight(null, "off");
        knob.x = targetX;
        knob.y = targetY;
        markSlidingActivity();
        requestDraw();
        scheduleMorphUpdate();
        return;
    }

    const SNAP_DISTANCE = 5;
    const LINE_SNAP_ENTER = 6;
    const LINE_SNAP_STICK = 13;
    const LINE_SNAP_STRONG = 24;

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

    if (closestPointDist < SNAP_DISTANCE) {
        targetX = closestPoint.x;
        targetY = closestPoint.y;

        if (closestPoint.name === "thin" || closestPoint.name === "muscular" || closestPoint.name === "fat") {
            setSnapLineHighlight(closestPoint.name, "hold");
        } else {
            setSnapLineHighlight(null, "off");
        }

        knob.x = targetX;
        knob.y = targetY;

        markSlidingActivity();

        requestDraw();
        scheduleMorphUpdate();
        return;
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

        if (proj.t <= 0.06 || proj.t >= 0.94) continue;

        if (!bestProjection || proj.distance < bestProjection.distance) {
            bestProjection = proj;
            bestLine = candidate;
        }
    }

    if (bestProjection) {
        const alreadyOnSameLine =
            snapLineState.isSnapped &&
            snapLineState.activeName === bestLine.name;

        let threshold;

        if (alreadyOnSameLine) {
            threshold = LINE_SNAP_STRONG;
        } else if (snapLineState.isSnapped) {
            threshold = LINE_SNAP_STICK;
        } else {
            threshold = LINE_SNAP_ENTER;
        }

        if (bestProjection.distance < threshold) {
            const strength = alreadyOnSameLine ? 0.985 : 0.42;

            targetX = targetX + (bestProjection.x - targetX) * strength;
            targetY = targetY + (bestProjection.y - targetY) * strength;

            if (!alreadyOnSameLine) {
                setSnapLineHighlight(bestLine.name, "blink");
            } else {
                setSnapLineHighlight(bestLine.name, "hold");
            }
        } else {
            setSnapLineHighlight(null, "off");
        }
    } else {
        setSnapLineHighlight(null, "off");
    }

    knob.x = targetX;
    knob.y = targetY;

    markSlidingActivity();

    requestDraw();
    scheduleMorphUpdate();
}

function finishDrag() {
    dragging = false;

    if (stopSlidingTimeout !== null) {
        clearTimeout(stopSlidingTimeout);
        stopSlidingTimeout = null;
    }

    flushPreviewNow();
    flushCommitNow();

    setSnapLineHighlight(null, "off");
}

function handleDragMove(event) {
    const rect = canvas.getBoundingClientRect();
    setKnobFromPoint(event.clientX - rect.left, event.clientY - rect.top);
}

function handleDragEnd() {
    document.removeEventListener("mousemove", handleDragMove);
    document.removeEventListener("mouseup", handleDragEnd);
    finishDrag();
}

canvas.addEventListener("mousedown", (event) => {
    if (event.button !== 0) return;

    if (dragging) {
        document.removeEventListener("mousemove", handleDragMove);
        document.removeEventListener("mouseup", handleDragEnd);
    }

    dragging = true;

    const pos = getMousePos(event);
    setKnobFromPoint(pos.x, pos.y);

    document.addEventListener("mousemove", handleDragMove);
    document.addEventListener("mouseup", handleDragEnd);
});

canvas.addEventListener("contextmenu", (event) => {
    event.preventDefault();
    event.stopPropagation();
});

window.addEventListener("blur", () => {
    document.removeEventListener("mousemove", handleDragMove);
    document.removeEventListener("mouseup", handleDragEnd);
    finishDrag();
});

resetButton.addEventListener("click", () => {
    const values = { thin: 0, muscular: 0, fat: 0 };
    pendingMorphValues = cloneMorphValues(values);
    updateKnobFromMorphs(values.thin, values.muscular, values.fat);
    updateReadout(values);

    flushPreviewNow();
    flushCommitNow();
});

snapToggleEl.addEventListener("change", () => {
    snapEnabled = !!snapToggleEl.checked;
    if (!snapEnabled) {
        setSnapLineHighlight(null, "off");
        draw();
    }
});

runBlendModelSelfTests();

updateKnobFromMorphs(0, 0, 0);
updateReadout({ thin: 0, muscular: 0, fat: 0 });

// VORTEX - an Arcade Phantasm game by Cursed Entertainment.
// You're falling through a collapsing dimension. Run along the inside of the tunnel: steering runs you round the wall,
// so "down" keeps changing, and the tunnel turns by itself too. Dodge the blocks, jump the gaps, time the spinning bars
// and grab the energy fragments. The glitchy, flickering blocks turn your left and right around for a few seconds.
// Faster every level; corkscrews from level 3, the tunnel breaks apart from level 4, and from level 5 you jump between
// floating pieces of it.
(function () {
  "use strict";
  var canvas = document.getElementById("game");
  var ctx = canvas.getContext("2d");
  var splash = document.getElementById("splash");
  var hud = {
    score: document.getElementById("hud-score"), level: document.getElementById("hud-level"),
    lives: document.getElementById("hud-lives"), best: document.getElementById("hud-best")
  };
  var hash = location.hash;
  var shotMode = hash.indexOf("#shot") === 0;

  // the Cursed colours
  var C = { navy: "#001725", deep: "#005184", blue: "#007ab7", sky: "#34a9e3", pink: "#f088e9", green: "#39ff6a", black: "#020611" };

  // the tunnel: N panels round, rows ROW long, VIS rows ahead in view
  var N = 14, LA = Math.PI * 2 / N, TAU = Math.PI * 2, R = 1, ROW = 0.6, VIS = 60, CAM_BACK = 1.15, CAM_Y = 0.4, NEAR = 0.14;
  var JUMP_V = 3.6, GRAV = 11.5, STEER = 2.8, BLOCK_H = 0.6, FRAG_H = 0.3, RUN_HW = 0.07;
  var LEVEL_ROWS = 260, REVERSE_TIME = 4.5, CRUMBLE_D = 11, INVULN = 2.2;
  var EMPTY = 0, HOLE = 1, BLOCK = 2, GLITCH = 3, CRUMBLE = 4;
  var LEVEL_NOTES = ["", "Run, jump, collect", "Glitches and spinning bars", "Corkscrews", "The tunnel is breaking up",
                     "Jump between the pieces", "Crossbars", "Faster", "Faster still"];

  // a seeded random for the track, so #shot always looks the same
  var seed = 1;
  function rnd() {
    seed = seed + 0x6D2B79F5 | 0;
    var t = Math.imul(seed ^ seed >>> 15, 1 | seed);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  }
  function ri(n) { return Math.floor(rnd() * n); }
  function wrap(i) { return ((i % N) + N) % N; }
  function mod(a, m) { return ((a % m) + m) % m; }
  function ld(a, b) { var d = Math.abs(wrap(a) - wrap(b)); return Math.min(d, N - d); }
  function clamp(v, a, b) { return v < a ? a : v > b ? b : v; }

  var W = 0, H = 0, F = 0, CX = 0, CY = 0, DPR = 1;
  var rows, gen, pz, phi, camRoll, h, vh, grounded, spin, level, lives, score, frags, combo, comboT;
  var reverseT, invulnT, fallT, state, time, bannerT, bannerText, bannerSub, flashT, flashCol, shakeT, overT, jumpBuf;
  var keys = { left: false, right: false };
  var particles = [], stars = [], shards = [];
  var best = parseInt(localStorage.getItem("vortexBest") || "0", 10);
  var started = false, paused = false, autopilot = false;

  // ---- the track ------------------------------------------------------------------------------------------------
  function newRow() {
    gen.twistRate += (gen.twistTarget - gen.twistRate) * 0.05;
    gen.cx += (gen.tcx - gen.cx) * 0.03;
    gen.cy += (gen.tcy - gen.cy) * 0.03;
    var row = { c: [], f: [], twist: gen.twist, cx: gen.cx, cy: gen.cy, bar: null, gate: 0, island: false, wob: 0 };
    gen.twist += gen.twistRate;
    for (var i = 0; i < N; i++) { row.c.push(EMPTY); row.f.push(0); }
    return row;
  }
  function pushRow(fn) {
    var row = newRow();
    if (fn) fn(row);
    rows[gen.r++] = row;
    gen.inLvl++;
    return row;
  }
  function fragAtSafe(r) { r.f[gen.safe] = 1; }
  function moveSafe(by) {
    gen.oldSafe = gen.safe;
    gen.safe = wrap(gen.safe + (by || (rnd() < 0.5 ? -1 : 1)));
    gen.moveAt = gen.r;
  }
  function isSafe(l) { return l === gen.safe || (l === gen.oldSafe && gen.r - gen.moveAt < 5); }

  function setTargets() {
    var L = gen.lvl;
    var cork = L >= 3 && rnd() < 0.45;
    gen.twistTarget = cork ? (rnd() < 0.5 ? -1 : 1) * Math.min(0.1, 0.045 + 0.012 * (L - 3)) : (rnd() - 0.5) * 0.02;
    var bend = Math.min(1, 0.4 + L * 0.15) * 0.007;
    var a = rnd() * TAU, m = rnd() < 0.2 ? 0 : bend * (0.4 + 0.6 * rnd());
    gen.tcx = Math.sin(a) * m;
    gen.tcy = Math.cos(a) * m;
  }

  var PATTERNS = {
    run: function () {
      var k = 12 + ri(6);
      for (var i = 0; i < k; i++) { if (i % 5 === 4) moveSafe(); pushRow(i % 2 === 0 ? fragAtSafe : null); }
    },
    blocks: function () {
      var p = Math.min(0.34, 0.12 + 0.03 * gen.lvl), k = 14 + ri(8);
      for (var i = 0; i < k; i++) {
        if (i % 4 === 3 && rnd() < 0.7) moveSafe();
        pushRow(function (r) {
          if (i % 2) return;
          for (var l = 0; l < N; l++) if (!isSafe(l) && rnd() < p) r.c[l] = BLOCK;
          if (rnd() < 0.35) r.f[gen.safe] = 1;
        });
      }
    },
    wall: function () {
      var n = gen.lvl >= 3 ? 2 : 1;
      for (var w = 0; w < n; w++) {
        for (var i = 0; i < 6; i++) { if (i === 1 || (i === 3 && gen.lvl >= 2)) moveSafe(); pushRow(i === 4 ? fragAtSafe : null); }
        pushRow(function (r) { for (var l = 0; l < N; l++) if (ld(l, gen.safe) > 1) r.c[l] = BLOCK; });
      }
      for (var j = 0; j < 4; j++) pushRow(null);
    },
    gap: function () {
      for (var i = 0; i < 5; i++) pushRow(null);
      var g = 1 + ri(Math.min(3, gen.lvl));
      for (var j = 0; j < g; j++) pushRow(function (r) { for (var l = 0; l < N; l++) r.c[l] = HOLE; if (j === (g >> 1)) r.f[gen.safe] = 1; });
      for (i = 0; i < 6; i++) pushRow(null);
    },
    glitch: function () {
      var k = 12 + ri(6), hitRow = 4 + 2 * ri(3);
      for (var i = 0; i < k; i++) {
        if (i % 5 === 4) moveSafe();
        pushRow(function (r) {
          if (i % 2) return;
          for (var l = 0; l < N; l++) {
            if (isSafe(l)) continue;
            var q = rnd();
            if (q < 0.17) r.c[l] = GLITCH; else if (q < 0.23) r.c[l] = BLOCK;
          }
          if (i === hitRow) r.c[gen.safe] = GLITCH;
        });
      }
    },
    bars: function () {
      var n = gen.lvl >= 4 ? 2 : 1;
      for (var b = 0; b < n; b++) {
        for (var i = 0; i < 6; i++) pushRow(i === 2 ? fragAtSafe : null);
        pushRow(function (r) {
          r.bar = { ph: rnd() * Math.PI, sp: (rnd() < 0.5 ? -1 : 1) * Math.min(3.2, 1.3 + 0.25 * gen.lvl), cross: gen.lvl >= 6 && rnd() < 0.6 };
        });
      }
      for (i = 0; i < 6; i++) pushRow(null);
    },
    broken: function () {
      var p = Math.min(0.5, 0.24 + 0.04 * (gen.lvl - 4)), k = 16 + ri(8);
      for (var i = 0; i < k; i++) {
        if (i % 4 === 3) moveSafe();
        pushRow(function (r) {
          for (var l = 0; l < N; l++) {
            if (isSafe(l)) continue;
            if (rnd() < p) r.c[l] = rnd() < 0.5 ? CRUMBLE : HOLE;
            else if (i % 3 === 0 && rnd() < 0.08) r.c[l] = BLOCK;
          }
          if (i % 3 === 0) r.f[gen.safe] = 1;
        });
      }
    },
    islands: function () {
      var segs = 3 + ri(3), w = gen.lvl >= 7 ? 1 : 2;
      for (var s = 0; s < segs; s++) {
        var len = 5 + ri(4), wob = rnd() * TAU;
        for (var i = 0; i < len; i++) pushRow(function (r) {
          r.island = true; r.wob = wob;
          for (var l = 0; l < N; l++) if (ld(l, gen.safe) > w) r.c[l] = HOLE;
          if (i === 2) r.f[gen.safe] = 1;
        });
        var g = 2 + ri(Math.min(4, gen.lvl - 3));
        for (var j = 0; j < g; j++) pushRow(function (r) {
          for (var l = 0; l < N; l++) r.c[l] = HOLE;
          if (j === (g >> 1)) r.f[gen.safe] = 1;
        });
        gen.safe = wrap(gen.safe + ri(3) - 1);       // the next piece has floated a little way round
      }
      for (i = 0; i < 4; i++) pushRow(null);
    },
    slalom: function () {
      var k = 3 + ri(3), dirn = rnd() < 0.5 ? -1 : 1;
      for (var s = 0; s < k; s++) {
        for (var i = 0; i < 7; i++) { if (i === 1 || i === 3) moveSafe(dirn); pushRow(i === 5 ? fragAtSafe : null); }
        pushRow(function (r) { for (var l = 0; l < N; l++) if (ld(l, gen.safe) > 1) r.c[l] = BLOCK; });
        if (rnd() < 0.4) dirn = -dirn;
      }
      for (var j = 0; j < 4; j++) pushRow(null);
    }
  };

  function genChunk() {
    if (gen.inLvl >= LEVEL_ROWS) {
      gen.lvl++;
      gen.inLvl = 0;
      pushRow(function (r) { r.gate = gen.lvl; });
      setTargets();
      for (var i = 0; i < 14; i++) pushRow(i % 2 ? null : fragAtSafe);
      return;
    }
    if (rnd() < 0.35) setTargets();
    var L = gen.lvl;
    var list = [["run", 2], ["blocks", 3], ["wall", L >= 2 ? 2 : 1], ["gap", 2], ["glitch", L >= 2 ? 2.5 : 1.2],
                ["bars", L >= 2 ? 2 : 0], ["broken", L >= 4 ? 3 : 0], ["islands", L >= 5 ? 3.5 : 0], ["slalom", L >= 3 ? 2 : 0]];
    var total = 0, k;
    for (k = 0; k < list.length; k++) total += list[k][1];
    var pick = rnd() * total;
    for (k = 0; k < list.length; k++) { pick -= list[k][1]; if (pick <= 0) break; }
    PATTERNS[list[Math.min(k, list.length - 1)][0]]();
  }

  function ensureRows(upTo) { while (gen.r <= upTo + 2) genChunk(); }

  function resetTrack(startLevel) {
    rows = {};
    gen = { r: 0, safe: ri(N), oldSafe: -1, moveAt: -99, lvl: startLevel, inLvl: 0, twist: 0, twistRate: 0, twistTarget: 0,
            cx: 0, cy: 0, tcx: 0, tcy: 0 };
    if (startLevel > 1) setTargets();
    for (var i = 0; i < 30; i++) pushRow(i > 12 && i % 2 === 0 ? fragAtSafe : null);
  }

  function rowAt(i) { return rows[i] || null; }
  function twistAt(z) {
    var e = z / ROW, i = Math.floor(e), a = rows[i], b = rows[i + 1];
    if (!a) return 0;
    if (!b) return a.twist;
    return a.twist + (b.twist - a.twist) * (e - i);
  }
  // which panel the runner is over, at depth z, at angle offset off
  function laneAt(z, off) { return Math.floor(mod(phi + (off || 0) - twistAt(z) - spin, TAU) / LA) % N; }

  // ---- the game -------------------------------------------------------------------------------------------------
  function levelSpeed(L) { return Math.min(22, 9 + 1.4 * (L - 1)); }

  function newGame(startLevel) {
    resetTrack(startLevel);
    pz = 2 * ROW; spin = 0; h = 0; vh = 0; grounded = true;
    phi = (gen.safe + 0.5) * LA;
    // start on the lane the first fragments sit on
    for (var i = 0; i < 30; i++) if (rows[i].f.indexOf(1) >= 0) { phi = (rows[i].f.indexOf(1) + 0.5) * LA + rows[i].twist; break; }
    camRoll = phi;
    level = startLevel; lives = 3; score = 0; frags = 0; combo = 0; comboT = 0;
    reverseT = 0; invulnT = 0; fallT = 0; flashT = 0; shakeT = 0; jumpBuf = 0;
    particles = [];
    state = "play";
    banner("LEVEL " + level, LEVEL_NOTES[Math.min(level, LEVEL_NOTES.length - 1)]);
    if (!shotMode) { Sfx.start(); Sfx.music(true, level); }
    updateHud();
  }

  function banner(t, sub) { bannerText = t; bannerSub = sub || ""; bannerT = 2.6; }

  function updateHud() {
    hud.score.textContent = "Score " + Math.floor(score);
    hud.level.textContent = "Level " + level;
    hud.lives.textContent = "Lives " + Math.max(0, lives);
    hud.best.textContent = "Best " + best + (Sfx.isMuted() ? "  (muted)" : "");
  }

  function runnerScreen(radiusOff) {
    if (!P(phi, R - h + (radiusOff || 0), pz)) return [W / 2, H * 0.8];
    return [PX, PY];
  }

  function crash() {
    var p = runnerScreen(-0.15);
    burst(p[0], p[1], 70, 520, [C.pink, "#ffffff", C.sky, C.green], 4);
    burst(p[0], p[1], 30, 220, ["#ffffff"], 6);
    Sfx.hit();
    shakeT = 0.5; flash("#ffffff", 0.35);
    loseLife();
  }

  function startFall() {
    state = "fall"; fallT = 0;
    Sfx.fall();
  }

  function loseLife() {
    lives--;
    combo = 0;
    if (lives <= 0) return gameOver();
    state = "play";
    invulnT = INVULN; h = 0; vh = 0; grounded = true; reverseT = 0;
    updateHud();
  }

  function gameOver() {
    state = "over"; overT = time;
    Sfx.music(false); Sfx.over();
    if (score > best && !shotMode) { best = Math.floor(score); localStorage.setItem("vortexBest", String(best)); }
    updateHud();
  }

  function glitchHit(lane, r) {
    rows[r].c[lane] = EMPTY;
    reverseT = REVERSE_TIME;
    var p = runnerScreen(-0.2);
    burst(p[0], p[1], 40, 380, [C.green, C.pink, "#ffffff"], 3);
    Sfx.glitch(); flash(C.green, 0.25); shakeT = 0.25;
  }

  function collect(lane, r) {
    rows[r].f[lane] = 0;
    frags++;
    combo = comboT > 0 ? combo + 1 : 0; comboT = 1.2;
    score += 50 * level * (1 + Math.min(combo, 10) * 0.1);
    var p = runnerScreen(-FRAG_H);
    burst(p[0], p[1], 22, 300, [C.green, "#ffffff"], 2.5);
    Sfx.frag(combo);
  }

  function flash(col, t) { flashCol = col; flashT = t; }

  // the autopilot drives the tunnel behind the splash (and the #shot mode): head for the clearest lane ahead
  function autoSteer() {
    var r0 = Math.floor(pz / ROW), cur = laneAt(pz), bestK = 0, bestCost = 1e9;
    for (var k = -3; k <= 3; k++) {
      var cost = Math.abs(k) * 0.6;
      for (var j = 0; j < 9; j++) {
        var row = rows[r0 + j]; if (!row) continue;
        var c = row.c[wrap(cur + k)];
        if (c === BLOCK) cost += 12 - j; else if (c === GLITCH) cost += 4; else if ((c === HOLE || c === CRUMBLE) && j < 2) cost += 1;
        if (row.f[wrap(cur + k)]) cost -= 1;
      }
      if (cost < bestCost) { bestCost = cost; bestK = k; }
    }
    var target = (wrap(cur + bestK) + 0.5) * LA + twistAt(pz) + spin;
    var d = mod(target - phi + Math.PI, TAU) - Math.PI;
    var ahead = rows[r0 + 1];
    if (ahead && grounded && (ahead.c[cur] === HOLE || ahead.c[cur] === CRUMBLE)) jumpBuf = 0.1;
    return Math.abs(d) < 0.06 ? 0 : (d > 0 ? 1 : -1);
  }

  function update(dt) {
    time += dt;
    bannerT = Math.max(0, bannerT - dt);
    flashT = Math.max(0, flashT - dt);
    shakeT = Math.max(0, shakeT - dt);
    if (state === "over") { pz += 3 * dt; return; }
    var attract = state === "attract";
    var speed = attract ? 9 : levelSpeed(level);
    if (state === "fall") speed *= 0.5;

    // the tunnel turns by itself, back and forth, harder every level
    spin += Math.min(0.75, 0.14 + 0.07 * level) * Math.sin(time * 0.17 + 1.3) * dt;

    var dir;
    if (attract || autopilot) dir = autoSteer();
    else {
      dir = (keys.right ? 1 : 0) - (keys.left ? 1 : 0);
      if (reverseT > 0) dir = -dir;
    }
    if (state === "play" || attract) phi += dir * STEER * dt;

    jumpBuf = Math.max(0, jumpBuf - dt);
    if (jumpBuf > 0 && grounded && (state === "play" || attract)) {
      vh = JUMP_V; grounded = false; jumpBuf = 0;
      if (!attract && !shotMode) Sfx.jump();
    }
    if (!grounded && state !== "fall") {
      h += vh * dt; vh -= GRAV * dt;
      if (h <= 0) { h = 0; vh = 0; grounded = true; }
    }

    var oz = pz, tw0 = twistAt(pz);
    pz += speed * dt;
    ensureRows(Math.floor(pz / ROW) + VIS + 4);
    phi += twistAt(pz) - tw0;               // the corkscrew carries you round with it
    var rb = Math.floor(pz / ROW) - 6;      // forget the rows far behind
    for (var del = rb - 10; del < rb; del++) delete rows[del];

    if (state === "fall") {
      fallT += dt;
      h -= (1.5 + fallT * 6) * dt;
      if (fallT > 0.95) {
        var p = runnerScreen();
        burst(p[0], p[1], 30, 200, [C.pink, C.sky], 3);
        loseLife();
      }
      return;
    }
    if (attract) return;

    score += speed * dt * 2 * (1 + 0.25 * (level - 1));
    reverseT = Math.max(0, reverseT - dt);
    if (reverseT === 0 && wasReversed) Sfx.unglitch();
    wasReversed = reverseT > 0;
    invulnT = Math.max(0, invulnT - dt);
    comboT = Math.max(0, comboT - dt);

    // everything between the last frame and this one
    for (var r = Math.floor(oz / ROW); r <= Math.floor(pz / ROW) && state === "play"; r++) {
      var row = rows[r]; if (!row) continue;
      var zc = Math.min(Math.max(pz, r * ROW + 0.01), (r + 1) * ROW - 0.01);
      if (row.gate > level) {
        level = row.gate;
        score += 500 * level;
        if (level % 3 === 0 && lives < 5) lives++;
        banner("LEVEL " + level, LEVEL_NOTES[Math.min(level, LEVEL_NOTES.length - 1)]);
        Sfx.level(); Sfx.music(false); Sfx.music(true, level);
        flash("#ffffff", 0.5);
      }
      var lc = laneAt(zc), ll = laneAt(zc, -RUN_HW), lr = laneAt(zc, RUN_HW);
      var lanes = ll === lr ? [lc] : [ll, lr];
      for (var q = 0; q < lanes.length; q++) {
        var L = lanes[q], c = row.c[L];
        if (c === GLITCH && h < BLOCK_H) { glitchHit(L, r); }
        else if (c === BLOCK && h < BLOCK_H && invulnT <= 0) { crash(); break; }
        if (row.f[L] && Math.abs(h - FRAG_H) < 0.45) collect(L, r);
      }
      if (state !== "play") break;
      if (grounded && invulnT <= 0 && (row.c[lc] === HOLE || row.c[lc] === CRUMBLE)) { startFall(); break; }
    }
    // the spinning bars
    for (r = Math.floor((pz - 0.3) / ROW); r <= Math.floor((pz + 0.3) / ROW) && state === "play"; r++) {
      var br = rows[r];
      if (!br || !br.bar || invulnT > 0) continue;
      var bzc = (r + 0.5) * ROW;
      if (Math.abs(pz - bzc) > 0.22) continue;
      var b = barAngle(br, r), per = br.bar.cross ? Math.PI / 2 : Math.PI;
      var m = mod(phi - b, per);
      if (Math.min(m, per - m) < 0.16 + RUN_HW) crash();
    }
    if (Math.floor(score) !== lastHudScore) { lastHudScore = Math.floor(score); updateHud(); }
  }
  var wasReversed = false, lastHudScore = -1;

  function barAngle(row, r) { return row.bar.ph + row.bar.sp * time + twistAt((r + 0.5) * ROW) + spin; }

  // ---- particles and the void -----------------------------------------------------------------------------------
  function burst(x, y, n, speed, cols, size) {
    for (var i = 0; i < n; i++) {
      var a = Math.random() * TAU, v = speed * (0.2 + Math.random() * 0.8), life = 0.4 + Math.random() * 0.6;
      particles.push({ x: x, y: y, vx: Math.cos(a) * v, vy: Math.sin(a) * v, life: life, max: life,
                       c: cols[Math.floor(Math.random() * cols.length)], s: size * (0.5 + Math.random()) });
    }
  }

  function makeStars() {
    stars = [];
    for (var i = 0; i < 220; i++) stars.push({ a: Math.random() * TAU, r: Math.random(), sp: 0.3 + Math.random() * 0.9 });
    shards = [];
    for (i = 0; i < 26; i++) shards.push({ a: Math.random() * TAU, r: 0.15 + Math.random() * 0.9, rot: Math.random() * TAU,
                                           vr: (Math.random() - 0.5) * 2, s: 0.4 + Math.random(), c: i % 2 ? PINK : SKY });
  }

  // ---- projection -----------------------------------------------------------------------------------------------
  // edge rings: the screen positions of the panel corners at every row edge in view, worked out once a frame
  var MAXE = VIS + 12, EX = new Float32Array(MAXE * N), EY = new Float32Array(MAXE * N), EOX = new Float32Array(MAXE),
      EOY = new Float32Array(MAXE), ES = new Float32Array(MAXE), firstE = 0, lastE = 0;
  var PX = 0, PY = 0, PS = 0;

  function computeRings() {
    var p0 = Math.floor(pz / ROW);
    firstE = Math.floor((pz - CAM_BACK) / ROW);
    lastE = Math.min(p0 + VIS, firstE + MAXE - 1);
    var cr = Math.cos(camRoll), sr = Math.sin(camRoll);
    var ox = 0, oy = 0, vx = 0, vy = 0, prevZ = pz;
    for (var e = firstE; e <= lastE; e++) {
      var k = e - firstE, z = e * ROW;
      if (e > p0) {                                  // bends: the curve of the tunnel, built up from the runner forwards
        var row = rows[e - 1], len = z - prevZ;
        if (row) { vx += row.cx * len; vy += row.cy * len; }
        ox += vx * len; oy += vy * len; prevZ = z;
      }
      var cx = ox * cr - oy * sr, cy = oy * cr + ox * sr;
      EOX[k] = cx; EOY[k] = cy;
      var dz = Math.max(NEAR, z - pz + CAM_BACK), s = F / dz;
      ES[k] = s;
      var rw = rows[e], tw = (rw ? rw.twist : 0) + spin - camRoll;
      for (var l = 0; l < N; l++) {
        var a = l * LA + tw;
        EX[k * N + l] = CX + (cx + R * Math.sin(a)) * s;
        EY[k * N + l] = CY + (cy + R * Math.cos(a) - CAM_Y) * s;
      }
    }
  }

  // any point: world angle th, radius r (1 = the wall), depth z; sets PX, PY and PS (the scale there)
  function P(th, r, z) {
    var dz = z - pz + CAM_BACK;
    if (dz < NEAR) return false;
    var f = z / ROW - firstE, k = Math.floor(f), t = f - k;
    if (k < 0) { k = 0; t = 0; }
    if (k >= lastE - firstE) { k = lastE - firstE - 1; t = 1; }
    var ox = EOX[k] + (EOX[k + 1] - EOX[k]) * t, oy = EOY[k] + (EOY[k + 1] - EOY[k]) * t;
    var a = th - camRoll, s = F / dz;
    PX = CX + (ox + r * Math.sin(a)) * s;
    PY = CY + (oy + r * Math.cos(a) - CAM_Y) * s;
    PS = s;
    return true;
  }

  // ---- drawing --------------------------------------------------------------------------------------------------
  var PINK = [240, 136, 233], SKY = [52, 169, 227], BLUE = [0, 122, 183];
  var PAL_FILL = [], PAL_LINE = [];
  (function () {
    for (var i = 0; i <= 32; i++) {
      var t = i / 32, r = SKY[0] + (PINK[0] - SKY[0]) * t, g = SKY[1] + (PINK[1] - SKY[1]) * t, b = SKY[2] + (PINK[2] - SKY[2]) * t;
      PAL_LINE.push("rgb(" + (r | 0) + "," + (g | 0) + "," + (b | 0) + ")");
      PAL_FILL.push("rgb(" + ((r * 0.2) | 0) + "," + ((g * 0.18 + 10) | 0) + "," + ((b * 0.22 + 22) | 0) + ")");
    }
  })();

  function quad(x0, y0, x1, y1, x2, y2, x3, y3, inset) {
    var mx = (x0 + x1 + x2 + x3) / 4, my = (y0 + y1 + y2 + y3) / 4, k = 1 - inset;
    ctx.beginPath();
    ctx.moveTo(mx + (x0 - mx) * k, my + (y0 - my) * k);
    ctx.lineTo(mx + (x1 - mx) * k, my + (y1 - my) * k);
    ctx.lineTo(mx + (x2 - mx) * k, my + (y2 - my) * k);
    ctx.lineTo(mx + (x3 - mx) * k, my + (y3 - my) * k);
    ctx.closePath();
  }

  var tmp = new Float32Array(16);
  function pquad(th0, th1, r0, r1, z0, z1, alongZ) {   // a quad through P(): returns false if behind the camera
    var pts = alongZ ? [[th0, r0, z0], [th1, r0, z0], [th1, r1, z1], [th0, r1, z1]]
                     : [[th0, r0, z0], [th1, r1, z0], [th1, r1, z1], [th0, r0, z1]];
    for (var i = 0; i < 4; i++) {
      if (!P(pts[i][0], pts[i][1], pts[i][2])) return false;
      tmp[i * 2] = PX; tmp[i * 2 + 1] = PY;
    }
    quad(tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5], tmp[6], tmp[7], 0);
    return true;
  }

  function drawBackground(dt, speed) {
    ctx.fillStyle = C.black;
    ctx.fillRect(0, 0, W, H);
    var k = lastE - firstE, vx = CX + EOX[k] * ES[k], vy = CY + (EOY[k] - CAM_Y) * ES[k], big = Math.max(W, H) * 0.9;
    // stars streaming out of the vortex
    ctx.strokeStyle = "#ffffff";
    for (var i = 0; i < stars.length; i++) {
      var s = stars[i];
      s.r += s.r * dt * s.sp * speed * 0.12 + dt * 0.01;
      if (s.r > 1.3) { s.r = 0.02 + Math.random() * 0.05; s.a = Math.random() * TAU; }
      var a = s.a - camRoll * 0.6, r1 = s.r * big, r0 = r1 * (1 - 0.06 * speed / 9);
      ctx.globalAlpha = Math.min(1, s.r * 1.5);
      ctx.lineWidth = 1 + s.r * 1.5;
      ctx.beginPath();
      ctx.moveTo(vx + Math.cos(a) * r0, vy + Math.sin(a) * r0);
      ctx.lineTo(vx + Math.cos(a) * r1, vy + Math.sin(a) * r1);
      ctx.stroke();
    }
    // from level 4 on, broken bits of the tunnel tumble in the void
    if (level >= 4) {
      for (i = 0; i < shards.length; i++) {
        var sh = shards[i];
        sh.rot += sh.vr * dt; sh.a += dt * 0.05 * sh.vr;
        var sa = sh.a - camRoll * 0.3, sr = sh.r * big * 0.8, x = vx + Math.cos(sa) * sr, y = vy + Math.sin(sa) * sr, z = 14 * sh.s;
        ctx.save(); ctx.translate(x, y); ctx.rotate(sh.rot);
        ctx.globalAlpha = 0.4;
        ctx.fillStyle = C.navy; ctx.strokeStyle = "rgb(" + sh.c.join(",") + ")"; ctx.lineWidth = 1.5;
        ctx.fillRect(-z, -z * 0.6, z * 2, z * 1.2); ctx.strokeRect(-z, -z * 0.6, z * 2, z * 1.2);
        ctx.restore();
      }
    }
    // the eye of the vortex
    ctx.globalAlpha = 1;
    for (i = 6; i >= 1; i--) {
      var rr = (i * 9 + (time * 40) % 9) * (H / 720);
      ctx.strokeStyle = i % 2 ? C.pink : C.sky;
      ctx.globalAlpha = 0.5 * (1 - i / 7);
      ctx.lineWidth = 2;
      ctx.beginPath(); ctx.arc(vx, vy, rr, 0, TAU); ctx.stroke();
    }
    ctx.globalAlpha = 1;
    ctx.shadowColor = C.pink; ctx.shadowBlur = 30; ctx.fillStyle = "#ffffff";
    ctx.beginPath(); ctx.arc(vx, vy, 4 + 2 * Math.sin(time * 6), 0, TAU); ctx.fill();
    ctx.shadowBlur = 0;
  }

  function drawTunnel() {
    var far = (lastE - firstE) * ROW;
    for (var e = lastE - 1; e >= firstE; e--) {
      var row = rows[e]; if (!row) continue;
      var k = e - firstE, d = e * ROW - pz;
      var fog = clamp(1 - (d - 2) / (far - 2), 0, 1);
      fog = Math.pow(fog, 0.8);
      if (fog <= 0.01) continue;
      var lw = clamp(ES[k] * 0.006, 0.6, 4);
      var wave = 0.5 + 0.5 * Math.sin(time * 5 - e * 0.45);
      var nearer = rows[e - 1];
      var wob = row.island ? 0.03 * Math.sin(time * 1.7 + row.wob) : 0;
      for (var l = 0; l < N; l++) {
        var c = row.c[l];
        var i0 = k * N + l, i1 = k * N + (l + 1) % N, j0 = (k + 1) * N + l, j1 = (k + 1) * N + (l + 1) % N;
        var pal = Math.round((l % 2 ? 1 - wave : wave) * 32);
        if (c === HOLE) continue;
        if (c === CRUMBLE && d < CRUMBLE_D) {     // the panel breaks off and falls away into the void
          var q = 1 - Math.max(0, d) / CRUMBLE_D, tw = row.twist + spin + q * 0.5 * (l % 2 ? 1 : -1);
          ctx.globalAlpha = fog * (1 - q);
          if (pquad(l * LA + tw, (l + 1) * LA + tw, R + q * q * 3, R + q * q * 3, e * ROW, (e + 1) * ROW, false)) {
            ctx.fillStyle = PAL_FILL[pal]; ctx.fill();
            ctx.strokeStyle = PAL_LINE[pal]; ctx.lineWidth = lw; ctx.stroke();
          }
          continue;
        }
        // the cliff edge, where the panel nearer to you is missing
        if (nearer && (nearer.c[l] === HOLE || nearer.c[l] === CRUMBLE) && d > -0.5) {
          var tw2 = row.twist + spin;
          ctx.globalAlpha = fog;
          if (pquad(l * LA + tw2, (l + 1) * LA + tw2, R, R + 0.25, e * ROW, e * ROW, true)) {
            ctx.fillStyle = C.navy; ctx.fill();
            ctx.strokeStyle = C.deep; ctx.lineWidth = lw; ctx.stroke();
          }
        }
        ctx.globalAlpha = fog;
        if (wob) {
          var tw3 = row.twist + spin;
          if (!pquad(l * LA + tw3, (l + 1) * LA + tw3, R + wob, R + wob, e * ROW, (e + 1) * ROW, false)) continue;
        } else {
          quad(EX[i0], EY[i0], EX[i1], EY[i1], EX[j1], EY[j1], EX[j0], EY[j0], 0.1);
        }
        ctx.fillStyle = c === CRUMBLE ? C.navy : PAL_FILL[pal];
        ctx.fill();
        ctx.strokeStyle = c === CRUMBLE ? (Math.random() < 0.5 ? "#ffffff" : PAL_LINE[pal]) : PAL_LINE[pal];
        ctx.lineWidth = lw;
        ctx.stroke();
      }
      if (d > -0.3) drawRowThings(row, e, d, fog);
    }
    ctx.globalAlpha = 1;
  }

  function drawBox(e, l, row, fog, glitch) {
    var tw = row.twist + spin, a0 = (l + 0.12) * LA + tw, a1 = (l + 0.88) * LA + tw;
    var z0 = e * ROW + 0.08, z1 = (e + 1) * ROW - 0.08, r0 = R, r1 = R - BLOCK_H;
    var jit = 0, fill = C.navy, line = "#ffffff", topLine = C.sky;
    if (glitch) {
      if (Math.random() < 0.18) return;                    // it flickers out of being
      jit = 0.05;
      var cs = [C.green, C.pink, "#ffffff"];
      line = cs[Math.floor(Math.random() * 3)]; topLine = cs[Math.floor(Math.random() * 3)];
      fill = Math.random() < 0.5 ? "#06331a" : "#2a0a2a";
      a0 += (Math.random() - 0.5) * jit; a1 += (Math.random() - 0.5) * jit;
    }
    ctx.globalAlpha = fog;
    ctx.lineWidth = clamp(PS * 0.008, 0.8, 3);
    var faces = [[a0, a0, r0, r1, z0, z1, false], [a1, a1, r0, r1, z0, z1, false], [a0, a1, r1, r1, z0, z1, false], [a0, a1, r0, r1, z0, z0, true]];
    // the sides are walls going in from the panel: (angle, radius) along the edge
    for (var i = 0; i < 4; i++) {
      var f = faces[i], ok;
      if (i < 2) {
        ok = P(f[0], r0, z0); if (!ok) return; tmp[0] = PX; tmp[1] = PY;
        P(f[0], r1, z0); tmp[2] = PX; tmp[3] = PY;
        P(f[0], r1, z1); tmp[4] = PX; tmp[5] = PY;
        P(f[0], r0, z1); tmp[6] = PX; tmp[7] = PY;
        quad(tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5], tmp[6], tmp[7], 0);
      } else if (i === 2) {
        if (!pquad(a0, a1, r1, r1, z0, z1, false)) return;
      } else {
        if (!pquad(a0, a1, r0, r1, z0, z0, true)) return;
      }
      ctx.fillStyle = i === 3 ? fill : (glitch ? fill : "#000c16");
      ctx.fill();
      ctx.strokeStyle = i === 2 ? topLine : line;
      ctx.stroke();
    }
    if (glitch && Math.random() < 0.5) {                  // a ghost copy, torn off sideways
      ctx.globalAlpha = fog * 0.5;
      if (pquad(a0 + 0.08, a1 + 0.08, r0, r1, z0, z0, true)) { ctx.strokeStyle = C.green; ctx.stroke(); }
    }
  }

  function drawRowThings(row, e, d, fog) {
    var l;
    for (l = 0; l < N; l++) {
      if (row.c[l] === BLOCK) drawBox(e, l, row, fog, false);
      else if (row.c[l] === GLITCH) drawBox(e, l, row, fog, true);
    }
    var zc = (e + 0.5) * ROW;
    for (l = 0; l < N; l++) {
      if (!row.f[l]) continue;
      if (!P((l + 0.5) * LA + row.twist + spin, R - FRAG_H + 0.04 * Math.sin(time * 4 + l), zc)) continue;
      var s = PS * 0.08, rot = time * 3 + l;
      ctx.globalAlpha = fog;
      ctx.save(); ctx.translate(PX, PY); ctx.rotate(rot);
      ctx.shadowColor = C.green; ctx.shadowBlur = 14; ctx.fillStyle = C.green;
      ctx.beginPath(); ctx.moveTo(0, -s); ctx.lineTo(s * 0.6, 0); ctx.lineTo(0, s); ctx.lineTo(-s * 0.6, 0); ctx.closePath(); ctx.fill();
      ctx.shadowBlur = 0; ctx.fillStyle = "#ffffff";
      ctx.beginPath(); ctx.moveTo(0, -s * 0.4); ctx.lineTo(s * 0.25, 0); ctx.lineTo(0, s * 0.4); ctx.lineTo(-s * 0.25, 0); ctx.closePath(); ctx.fill();
      ctx.restore();
    }
    if (row.bar) {
      var b = barAngle(row, e), arms = row.bar.cross ? 2 : 1;
      ctx.globalAlpha = fog;
      ctx.shadowColor = C.pink; ctx.shadowBlur = 18; ctx.lineCap = "round";
      for (var a = 0; a < arms; a++) {
        var ang = b + a * Math.PI / 2;
        if (!P(ang, R, zc)) break;
        var x0 = PX, y0 = PY, s0 = PS;
        P(ang + Math.PI, R, zc);
        ctx.strokeStyle = C.pink; ctx.lineWidth = Math.max(2, s0 * 0.09);
        ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(PX, PY); ctx.stroke();
        ctx.strokeStyle = "#ffffff"; ctx.lineWidth = Math.max(1, s0 * 0.03);
        ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(PX, PY); ctx.stroke();
      }
      if (P(0, 0, zc)) { ctx.fillStyle = "#ffffff"; ctx.beginPath(); ctx.arc(PX, PY, Math.max(2, PS * 0.07), 0, TAU); ctx.fill(); }
      ctx.shadowBlur = 0; ctx.lineCap = "butt";
    }
    if (row.gate) {                                        // the level gate: a bright ring
      ctx.globalAlpha = fog;
      ctx.shadowColor = C.green; ctx.shadowBlur = 24; ctx.strokeStyle = C.green; ctx.lineWidth = clamp(ES[e - firstE] * 0.03, 1, 12);
      ctx.beginPath();
      for (l = 0; l <= N * 3; l++) { P(l * LA / 3 + row.twist + spin, R * 0.96, e * ROW); if (l) ctx.lineTo(PX, PY); else ctx.moveTo(PX, PY); }
      ctx.stroke(); ctx.shadowBlur = 0;
    }
  }

  function drawRunner() {
    if (state === "attract" || state === "over") return;
    if (invulnT > 0 && Math.floor(time * 14) % 2) return;
    var a = phi - camRoll, glowC = reverseT > 0 ? C.green : C.pink;
    if (h > 0.02 && P(phi, R, pz)) {                       // the shadow on the wall
      ctx.fillStyle = "rgba(0,0,0,0.45)";
      ctx.beginPath(); ctx.ellipse(PX, PY, PS * 0.09, PS * 0.03, -a, 0, TAU); ctx.fill();
    }
    if (!P(phi, R - h, pz)) return;
    var u = PS * 0.075, run = time * 16, air = !grounded || state === "fall";
    ctx.save();
    ctx.translate(PX, PY); ctx.rotate(-a);
    if (state === "fall") { var k = Math.max(0.1, 1 - fallT); ctx.scale(k, k); ctx.rotate(fallT * 6); }
    ctx.strokeStyle = "#ffffff"; ctx.lineCap = "round"; ctx.lineJoin = "round";
    ctx.shadowColor = glowC; ctx.shadowBlur = 20;
    ctx.lineWidth = u * 0.45;
    var s1 = Math.sin(run), s2 = Math.sin(run + Math.PI);
    ctx.beginPath();                                       // legs
    if (air) { ctx.moveTo(-0.3 * u, -0.5 * u); ctx.lineTo(-0.35 * u, -1.2 * u); ctx.moveTo(0.3 * u, -0.6 * u); ctx.lineTo(0.35 * u, -1.2 * u); }
    else {
      ctx.moveTo(-0.35 * u, -Math.max(0, s1) * 0.7 * u); ctx.lineTo(-0.3 * u, -1.2 * u);
      ctx.moveTo(0.35 * u, -Math.max(0, s2) * 0.7 * u); ctx.lineTo(0.3 * u, -1.2 * u);
    }
    ctx.stroke();
    ctx.lineWidth = u * 0.7;                               // body
    ctx.beginPath(); ctx.moveTo(0, -1.25 * u); ctx.lineTo(0, -2.4 * u); ctx.stroke();
    ctx.lineWidth = u * 0.32;                              // arms
    ctx.beginPath();
    var sw = air ? -0.8 : 0.35;
    ctx.moveTo(-0.3 * u, -2.3 * u); ctx.lineTo(-0.75 * u, (-1.7 + (air ? -1 : s2 * sw)) * u);
    ctx.moveTo(0.3 * u, -2.3 * u); ctx.lineTo(0.75 * u, (-1.7 + (air ? -1 : s1 * sw)) * u);
    ctx.stroke();
    ctx.fillStyle = "#ffffff";                             // head
    ctx.beginPath(); ctx.arc(0, -2.95 * u, 0.42 * u, 0, TAU); ctx.fill();
    ctx.shadowBlur = 0;
    ctx.fillStyle = glowC;                                 // a little glowing core
    ctx.beginPath(); ctx.arc(0, -1.9 * u, 0.15 * u, 0, TAU); ctx.fill();
    ctx.restore();
  }

  function drawParticles(dt) {
    var drag = Math.exp(-2.5 * dt);
    for (var i = particles.length - 1; i >= 0; i--) {
      var p = particles[i];
      p.life -= dt;
      if (p.life <= 0) { particles.splice(i, 1); continue; }
      p.x += p.vx * dt; p.y += p.vy * dt; p.vx *= drag; p.vy *= drag;
      var k = p.life / p.max;
      ctx.globalAlpha = k;
      ctx.fillStyle = p.c; ctx.shadowColor = p.c; ctx.shadowBlur = 10;
      ctx.beginPath(); ctx.arc(p.x, p.y, 0.5 + p.s * k, 0, TAU); ctx.fill();
    }
    ctx.globalAlpha = 1; ctx.shadowBlur = 0;
  }

  function centreText(t, y, size, col, glow) {
    ctx.textAlign = "center"; ctx.textBaseline = "middle";
    ctx.font = "bold " + size + "px 'Arial Black', Arial, sans-serif";
    ctx.shadowColor = glow || col; ctx.shadowBlur = glow ? 22 : 0;
    ctx.fillStyle = col; ctx.fillText(t, W / 2, y);
    ctx.shadowBlur = 0;
  }

  function drawOverlays() {
    if (reverseT > 0) {
      // the screen goes green and tears while your controls are the wrong way round
      ctx.fillStyle = "rgba(57,255,106,0.13)";
      ctx.fillRect(0, 0, W, H);
      ctx.save(); ctx.setTransform(1, 0, 0, 1, 0, 0);
      for (var i = 0; i < 5; i++) {
        if (Math.random() < 0.55) continue;
        var y = Math.random() * canvas.height, hh = 4 + Math.random() * 40 * DPR, dx = (Math.random() - 0.5) * 50 * DPR;
        ctx.drawImage(canvas, 0, y, canvas.width, hh, dx, y, canvas.width, hh);
      }
      ctx.restore();
      ctx.strokeStyle = C.green; ctx.lineWidth = 6;
      ctx.strokeRect(3, 3, W - 6, H - 6);
      var sz = Math.min(44, W / 14);
      centreText(Math.floor(time * 10) % 7 === 0 ? "C0NTR0LS R3V3RS3D" : "CONTROLS REVERSED", H * 0.14, sz, C.green, C.green);
      var bw = Math.min(360, W * 0.6);
      ctx.fillStyle = C.navy; ctx.fillRect(W / 2 - bw / 2, H * 0.14 + sz * 0.7, bw, 8);
      ctx.fillStyle = C.green; ctx.fillRect(W / 2 - bw / 2, H * 0.14 + sz * 0.7, bw * reverseT / REVERSE_TIME, 8);
    }
    if (flashT > 0) {
      ctx.globalAlpha = Math.min(0.6, flashT * 1.6);
      ctx.fillStyle = flashCol; ctx.fillRect(0, 0, W, H);
      ctx.globalAlpha = 1;
    }
    if (bannerT > 0 && state !== "attract") {
      ctx.globalAlpha = Math.min(1, bannerT * 1.5);
      centreText(bannerText, H * 0.32, Math.min(84, W / 8), C.pink, C.pink);
      centreText(bannerSub, H * 0.32 + Math.min(60, W / 12), Math.min(26, W / 22), C.sky);
      ctx.globalAlpha = 1;
    }
    if (paused || state === "over") {
      ctx.fillStyle = "rgba(2, 6, 17, 0.65)";
      ctx.fillRect(0, 0, W, H);
      centreText(paused ? "PAUSED" : "GAME OVER", H / 2 - 30, Math.min(84, W / 8), C.pink, C.pink);
      ctx.font = "20px Arial, sans-serif"; ctx.fillStyle = "#ffffff";
      ctx.fillText(paused ? "P to carry on" : "Score " + Math.floor(score) + "  ·  Level " + level + "  ·  Fragments " + frags,
                   W / 2, H / 2 + 28);
      if (!paused) {
        ctx.fillStyle = Math.floor(score) >= best && score > 0 ? C.green : C.sky;
        ctx.fillText(Math.floor(score) >= best && score > 0 ? "A new best!" : "Best " + best, W / 2, H / 2 + 60);
        ctx.fillStyle = "#c2c2c2";
        ctx.fillText("Space, Enter or tap to run again", W / 2, H / 2 + 96);
      }
    }
  }

  function draw(dt) {
    var shake = shakeT > 0 ? shakeT * 18 : 0;
    camRoll += (phi - camRoll) * Math.min(1, dt * 7);
    CX = W / 2 + (Math.random() - 0.5) * shake;
    CY = H * 0.47 + (Math.random() - 0.5) * shake;
    computeRings();
    drawBackground(dt, state === "attract" ? 9 : levelSpeed(level));
    drawTunnel();
    drawRunner();
    drawParticles(dt);
    drawOverlays();
  }

  // ---- the loop and the controls --------------------------------------------------------------------------------
  var last = 0;
  function loop(now) {
    var dt = last ? Math.min(0.05, (now - last) / 1000) : 1 / 60;
    last = now;
    if (!paused) update(dt);
    draw(paused ? 0 : dt);
    requestAnimationFrame(loop);
  }

  function resize() {
    DPR = Math.min(window.devicePixelRatio || 1, 1.5);
    W = window.innerWidth; H = window.innerHeight;
    canvas.width = Math.round(W * DPR); canvas.height = Math.round(H * DPR);
    ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
    F = Math.min(W * 0.75, H) * 0.62;
  }

  function start() {
    if (started) return;
    started = true;
    splash.classList.add("gone");
    document.body.classList.add("playing");
    newGame(1);
  }

  function togglePause() {
    if (state === "over" || !started) return;
    paused = !paused;
    Sfx.music(!paused, level);
  }

  function restart() { if (state === "over" && time - overT > 0.8) newGame(1); }

  function keyName(e) { return e.key.length === 1 ? e.key.toLowerCase() : e.key; }
  document.addEventListener("keydown", function (e) {
    var k = keyName(e);
    if (!started) { start(); e.preventDefault(); return; }
    if (k === "m") { Sfx.toggleMute(); updateHud(); }
    else if (k === "p" || k === "Escape") togglePause();
    else if (state === "over") { if (k === " " || k === "Enter" || k === "r") restart(); }
    else if (k === "ArrowLeft" || k === "a") keys.left = true;
    else if (k === "ArrowRight" || k === "d") keys.right = true;
    else if ((k === " " || k === "ArrowUp" || k === "w") && !e.repeat) jumpBuf = 0.15;
    else return;
    e.preventDefault();
  });
  document.addEventListener("keyup", function (e) {
    var k = keyName(e);
    if (k === "ArrowLeft" || k === "a") keys.left = false;
    if (k === "ArrowRight" || k === "d") keys.right = false;
  });
  window.addEventListener("blur", function () { keys.left = keys.right = false; if (state === "play" && !paused) togglePause(); });

  splash.addEventListener("pointerdown", function (e) { e.preventDefault(); start(); });
  canvas.addEventListener("pointerdown", function () { if (state === "over") restart(); });

  function hold(id, on, off) {
    var b = document.getElementById(id);
    b.addEventListener("pointerdown", function (e) { e.preventDefault(); b.classList.add("down"); on(); });
    ["pointerup", "pointercancel", "pointerleave"].forEach(function (ev) {
      b.addEventListener(ev, function () { b.classList.remove("down"); if (off) off(); });
    });
  }
  hold("t-left", function () { keys.left = true; }, function () { keys.left = false; });
  hold("t-right", function () { keys.right = true; }, function () { keys.right = false; });
  hold("t-jump", function () { if (state === "over") restart(); else jumpBuf = 0.15; });
  hold("t-pause", togglePause);

  window.addEventListener("resize", resize);

  // a peek for the tests: read the game's state, or let the autopilot run
  window.vortexDebug = {
    get: function () { return { state: state, level: level, lives: lives, score: Math.floor(score), frags: frags, reversed: reverseT > 0, paused: paused, pz: pz }; },
    autopilot: function (on) { autopilot = !!on; },
    reverse: function () { reverseT = REVERSE_TIME; }
  };

  resize();
  makeStars();
  time = 0;
  seed = 20261109;
  newGame(1);
  state = "attract";
  bannerT = 0;

  if (shotMode) {
    // for README screenshots: a good-looking moment mid-game, simulated and drawn once
    var shotLevel = hash === "#shot-broken" ? 5 : hash === "#shot-reversed" ? 2 : 3;
    seed = hash === "#shot-broken" ? 77 : hash === "#shot-reversed" ? 12 : 5;
    splash.style.display = "none";
    started = true;
    document.body.classList.add("playing");
    newGame(shotLevel);
    autopilot = true;
    var simT = hash === "#shot-broken" ? 9.1 : hash === "#shot-reversed" ? 6.3 : 7.4;
    for (var t = 0; t < simT; t += 1 / 60) {
      invulnT = 10;
      update(1 / 60);
      if (state !== "play") state = "play";
      draw(1 / 60);
    }
    invulnT = 0; bannerT = 0; flashT = 0; shakeT = 0;
    if (hash === "#shot-reversed") reverseT = 3.1;
    if (grounded) { h = 0.32; grounded = false; vh = 0.4; }
    var rp = runnerScreen(-0.3);
    burst(rp[0] + 30, rp[1] - 40, 26, 260, [C.green, "#ffffff"], 2.5);
    draw(0.12);
    draw(0.06);
    updateHud();
    return;
  }
  requestAnimationFrame(loop);
})();

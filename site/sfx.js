// VORTEX's sound, all made with the Web Audio API (no files): jumps, fragment chimes, crashes, the glitch, the fall,
// the level gate, and a low pulsing bass while you run. Browsers only allow sound after a key or tap, so the audio
// starts on the first one. M mutes (remembered between visits).
var Sfx = (function () {
  "use strict";
  var ctx = null, out = null, musicBus = null;
  var muted = localStorage.getItem("vortexMute") === "1";

  function wake() {
    if (!ctx) {
      var AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) return;
      ctx = new AC();
      out = ctx.createGain();
      out.gain.value = muted ? 0 : 0.35;
      out.connect(ctx.destination);
      musicBus = ctx.createGain();
      musicBus.gain.value = 0.55;
      musicBus.connect(out);
    }
    if (ctx.state === "suspended") ctx.resume();
  }
  ["keydown", "pointerdown", "touchstart"].forEach(function (e) { document.addEventListener(e, wake, true); });

  // one note: a waveform gliding from f0 to f1, fading out over len seconds
  function tone(type, f0, f1, at, len, vol, dest) {
    var o = ctx.createOscillator(), g = ctx.createGain();
    o.type = type;
    o.frequency.setValueAtTime(f0, at);
    o.frequency.exponentialRampToValueAtTime(Math.max(1, f1), at + len);
    g.gain.setValueAtTime(vol, at);
    g.gain.exponentialRampToValueAtTime(0.001, at + len);
    o.connect(g); g.connect(dest || out);
    o.start(at); o.stop(at + len + 0.02);
  }

  // white noise through a filter, fading out
  function noise(at, len, vol, freq, type, dest) {
    var n = Math.floor(ctx.sampleRate * len), buf = ctx.createBuffer(1, n, ctx.sampleRate), d = buf.getChannelData(0);
    for (var i = 0; i < n; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / n, 2);
    var s = ctx.createBufferSource(), f = ctx.createBiquadFilter(), g = ctx.createGain();
    s.buffer = buf;
    f.type = type || "lowpass"; f.frequency.value = freq || 2000;
    g.gain.value = vol;
    s.connect(f); f.connect(g); g.connect(dest || out);
    s.start(at);
  }

  // the music: a sixteen-step bass line in A minor with a ticking hat, faster every level
  var BASS = [0, 0, 12, 0, 3, 0, 15, 3, 0, 0, 12, 0, 7, 5, 3, -2];
  var playing = false, nextT = 0, step = 0, bpm = 120, timer = null;
  function schedule() {
    if (!ctx || !playing) return;
    while (nextT < ctx.currentTime + 0.15) {
      var n = BASS[step % 16], f = 55 * Math.pow(2, n / 12);
      var o = ctx.createOscillator(), flt = ctx.createBiquadFilter(), g = ctx.createGain();
      o.type = "sawtooth"; o.frequency.value = f;
      flt.type = "lowpass"; flt.frequency.setValueAtTime(900, nextT); flt.frequency.exponentialRampToValueAtTime(160, nextT + 0.16);
      g.gain.setValueAtTime(0.16, nextT); g.gain.exponentialRampToValueAtTime(0.001, nextT + 0.2);
      o.connect(flt); flt.connect(g); g.connect(musicBus);
      o.start(nextT); o.stop(nextT + 0.22);
      if (step % 2 === 1) noise(nextT, 0.03, 0.05, 7000, "highpass", musicBus);
      if (step % 8 === 4) noise(nextT, 0.12, 0.12, 1800, "bandpass", musicBus);
      if (step % 32 === 0) tone("triangle", 440, 440, nextT, 0.6, 0.04, musicBus);
      nextT += 60 / bpm / 4;
      step++;
    }
  }

  return {
    toggleMute: function () {
      muted = !muted;
      localStorage.setItem("vortexMute", muted ? "1" : "0");
      if (out) out.gain.setTargetAtTime(muted ? 0 : 0.35, ctx.currentTime, 0.02);
      return muted;
    },
    isMuted: function () { return muted; },
    music: function (on, level) {
      bpm = Math.min(180, 116 + (level || 1) * 6);
      if (on && !playing) {
        if (!ctx) return;
        playing = true; nextT = ctx.currentTime + 0.05; step = 0;
        timer = setInterval(schedule, 40);
      } else if (!on && playing) {
        playing = false; clearInterval(timer);
      }
    },
    jump: function () {
      if (!ctx) return;
      var t = ctx.currentTime;
      tone("square", 220, 660, t, 0.14, 0.12);
      tone("sine", 330, 990, t, 0.16, 0.25);
    },
    frag: function (combo) {           // a chime, climbing with each fragment in a row
      if (!ctx) return;
      var t = ctx.currentTime, up = Math.pow(2, Math.min(combo, 12) / 12);
      tone("triangle", 880 * up, 880 * up, t, 0.08, 0.4);
      tone("sine", 1320 * up, 1760 * up, t + 0.05, 0.16, 0.3);
    },
    hit: function () {                 // a crash: a boom and a falling zap
      if (!ctx) return;
      var t = ctx.currentTime;
      noise(t, 0.6, 0.7, 900);
      tone("sawtooth", 300, 40, t, 0.5, 0.3);
      tone("square", 1200, 90, t, 0.3, 0.12);
    },
    fall: function () {                // a long whistle down into the void
      if (!ctx) return;
      var t = ctx.currentTime;
      tone("sine", 1400, 60, t, 1.1, 0.3);
      tone("triangle", 700, 30, t + 0.1, 1.0, 0.2);
    },
    glitch: function () {              // the glitch: broken bleeps jumping about
      if (!ctx) return;
      var t = ctx.currentTime;
      for (var i = 0; i < 9; i++) {
        var f = 200 + Math.random() * 2400;
        tone(i % 2 ? "square" : "sawtooth", f, f * (0.5 + Math.random()), t + i * 0.035, 0.05, 0.12);
      }
      noise(t, 0.3, 0.25, 4000, "bandpass");
    },
    unglitch: function () {
      if (!ctx) return;
      var t = ctx.currentTime;
      tone("triangle", 660, 990, t, 0.12, 0.2);
    },
    level: function () {               // a rising arpeggio through the gate
      if (!ctx) return;
      var t = ctx.currentTime, notes = [0, 3, 7, 12, 15, 19, 24];
      for (var i = 0; i < notes.length; i++) tone("square", 330 * Math.pow(2, notes[i] / 12), 330 * Math.pow(2, notes[i] / 12), t + i * 0.06, 0.18, 0.1);
      noise(t, 0.8, 0.15, 6000, "highpass");
    },
    start: function () {
      if (!ctx) return;
      var t = ctx.currentTime;
      tone("sawtooth", 80, 880, t, 0.5, 0.12);
      tone("sine", 160, 1760, t, 0.5, 0.15);
    },
    over: function () {
      if (!ctx) return;
      var t = ctx.currentTime;
      tone("square", 440, 110, t, 1.0, 0.2);
      tone("triangle", 330, 55, t + 0.15, 1.2, 0.35);
      noise(t, 1.2, 0.2, 500);
    }
  };
})();

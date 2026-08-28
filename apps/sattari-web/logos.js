/* =============================================================================
   Sattari — product marks
   -----------------------------------------------------------------------------
   One SVG per plugin, drawn to a shared grammar so the set reads as a family
   rather than 46 unrelated drawings:

     · 32×32 box, 1.75 stroke, round caps and joins, no fill
     · the body of the mark uses currentColor, so it inherits whatever the
       surrounding text colour is (works on light and dark, and in :hover)
     · exactly ONE element per mark is painted in the accent, and it is always
       the part that says what the plugin DOES — the snapped note in Auto Pitch,
       the ceiling in Peak, the ducked envelope in Side Chain Master.

   Each mark abstracts function, not appearance: Comp is a transfer curve, Gate
   is a square pulse, Echo is decaying repeats, Multiband is a stepped response.

   THE CONSTRAINT THAT SHAPED THIS SET: at 24px, a mark is its silhouette. Forty-
   six plugins that mostly process waveforms will all collapse into "a squiggle"
   unless they are deliberately spread across silhouette classes — ring, grid,
   bars, faders, curve-in-axes, dots, geometric. So related processors are pushed
   apart on purpose: Mix is faders, Multiband is a staircase, StemDeck is offset
   bars, where all three would otherwise have been stacked lines. Same for Pulse
   (smooth scallops) against Side Chain Master (a trigger and one sharp duck),
   and Comp (a transfer curve in its axes) against Riser (a filled sweep).
   Keep that rule when adding a mark: check the silhouette against the sheet,
   not the idea.

   Usage:  el.innerHTML = SATTARI_LOGO('auto-pitch')
   ========================================================================== */
(function (global) {
  'use strict';

  var A = 'var(--mint,#00a37a)';   // the accent, resolved from the page tokens

  function svg(inner) {
    return '<svg viewBox="0 0 32 32" fill="none" stroke="currentColor" ' +
           'stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" ' +
           'width="100%" height="100%" aria-hidden="true">' + inner + '</svg>';
  }
  var acc  = ' stroke="' + A + '"';
  var accf = ' fill="' + A + '" stroke="none"';
  var cf   = ' fill="currentColor" stroke="none"';

  var M = {
    /* ── key / analysis ──────────────────────── silhouette: rings, frames */
    // pitch stepping up and snapping onto a grid line
    'auto-pitch': '<path d="M4 24h6v-6h6v-6h6V6"/><path d="M4 12h24" opacity=".28"/>' +
                  '<circle cx="26" cy="6" r="2.6"' + accf + '/>',
    // circle of fifths: twelve divisions — a dashed ring, so the ticks cost
    // nothing — with one tonic lit
    'song-key':   '<circle cx="16" cy="16" r="12" opacity=".45" stroke-dasharray="2.2 4.08"/>' +
                  '<circle cx="16" cy="16" r="7.5"/>' +
                  '<circle cx="16" cy="4" r="2.4"' + accf + '/>',
    // a waveform resolving into a notehead
    'voxkey':     '<path d="M3 16c2-5 4-5 6 0s4 5 6 0"/><path d="M21 20V9l6-2v11"/>' +
                  '<ellipse cx="19" cy="21" rx="2.4" ry="2"' + accf + '/>',
    // reference needle centred on an arc
    'tuner':      '<path d="M6 22a12 12 0 0 1 20 0"/><path d="M16 22V10"' + acc + '/>' +
                  '<circle cx="16" cy="23.5" r="1.8"' + accf + '/>',
    // hub broadcasting to the session
    'brain':      '<circle cx="16" cy="16" r="4.5"' + accf + '/><circle cx="16" cy="16" r="10"/>' +
                  '<path d="M16 6v2.5M26 16h-2.5M16 26v-2.5M6 16h2.5M23 9l-1.8 1.8M23 23l-1.8-1.8M9 23l1.8-1.8M9 9l1.8 1.8"/>',
    // oscilloscope trace inside a frame
    'scope':      '<rect x="4" y="7" width="24" height="18" rx="3"/>' +
                  '<path d="M8 16c2-6 4-6 6 0s4 6 6 0 3-3 4 0"' + acc + '/>',

    /* ── vocal ──────────────────────────── silhouette: notes, mic, ladder */
    // a chord: three voices stacked on one stem. Stacked rather than stepped,
    // so it does not read as Arp's ascending run at 24px.
    'harmony':    '<path d="M4 23h24M4 12h24" opacity=".22"/>' +
                  '<path d="M16.4 23V7"/>' +
                  '<ellipse cx="12.8" cy="23" rx="3.6" ry="2.5"/>' +
                  '<ellipse cx="12.8" cy="17.5" rx="3.6" ry="2.5"/>' +
                  '<ellipse cx="12.8" cy="12" rx="3.6" ry="2.5"' + accf + '/>',
    // a ladder of scale degrees with one rung BETWEEN the others — the
    // microtone is the whole point, so it takes the accent. The spine is what
    // keeps this from reading as StemDeck's stack of lines.
    'maqam':      '<path d="M16 4v24"/>' +
                  '<path d="M11 8h10M11 14h10M11 20h10M11 26h10" opacity=".55"/>' +
                  '<path d="M12.5 17h7"' + acc + '/>',
    // the same take, printed twice with an offset
    'double':     '<path d="M4 12c2-5 4-5 6 0s4 5 6 0 3-3 4-1"/>' +
                  '<path d="M8 19c2-5 4-5 6 0s4 5 6 0 3-3 4-1"' + acc + '/>',
    // a spectrum with the sibilant band notched out
    'deess':      '<path d="M3 22c6 0 7-10 11-10 2 0 3 2 4 4"/>' +
                  '<path d="M19 17l3 8 3-8h4"' + acc + '/><path d="M3 27h26" opacity=".26"/>',
    // formant resonance, moved
    'face':       '<path d="M3 25c4 0 4-7 7-7s4 7 7 7" opacity=".38"/>' +
                  '<path d="M11 25c4 0 4-12 7-12s4 12 7 12"' + acc + '/>' +
                  '<path d="M3 27h26" opacity=".26"/>',
    // one voice blooming symmetrically into an ensemble
    'glass-choir':'<circle cx="16" cy="16" r="2.6"' + accf + '/>' +
                  '<path d="M11 10a9 9 0 0 0 0 12M21 10a9 9 0 0 1 0 12"/>' +
                  '<path d="M7 6a15 15 0 0 0 0 20M25 6a15 15 0 0 1 0 20" opacity=".4"/>',
    // a fixed chain of three
    'royal-chain':'<rect x="3" y="12" width="9" height="8" rx="4"/>' +
                  '<rect x="11.5" y="12" width="9" height="8" rx="4"' + acc + '/>' +
                  '<rect x="20" y="12" width="9" height="8" rx="4"/>',
    'vocal-master':'<path d="M16 20a4 4 0 0 0 4-4V9a4 4 0 0 0-8 0v7a4 4 0 0 0 4 4Z"/>' +
                  '<path d="M9 15a7 7 0 0 0 14 0" opacity=".5"/><path d="M16 24v4"/>' +
                  '<path d="M11 28h10"' + acc + '/>',
    // voice driving a synth
    'voxsynth':   '<path d="M4 16c2-6 4-6 6 0s4 6 6 0"/>' +
                  '<path d="M20 22V10h3v12M25 22V10h3v12"' + acc + '/>',
    // a wave bent out of shape
    'warp':       '<path d="M4 16c3-9 6 9 9 0s6 9 9 0 4-5 6-3"' + acc + '/>' +
                  '<path d="M4 24c4-4 8 4 12 0s8-4 12 0" opacity=".33"/>',
    // one shape becoming another
    'morph':      '<circle cx="10" cy="16" r="6"/><rect x="16" y="10" width="12" height="12" rx="5"' + acc + '/>',

    /* ── dynamics / mix ───────────── silhouette: axes, faders, bars, steps */
    // a transfer curve in its axes — knee, then ratio
    'comp':       '<path d="M5 27V5M5 27h22" opacity=".33"/>' +
                  '<path d="M5 27 26 6" opacity=".28" stroke-dasharray="2 3"/>' +
                  '<path d="M5 27l9-9c3-3 5-1 13-5"' + acc + '/>',
    // square gate pulse
    'gate':       '<path d="M3 24h6V10h6v14h6V10h6"' + acc + '/><path d="M3 28h26" opacity=".26"/>',
    // the ceiling, and a peak flattened against it
    'peak':       '<path d="M3 9h26"' + acc + '/>' +
                  '<path d="M4 26l3-9 2 6 3-14h3l3 16 3-8 3 9"/>',
    // stepped band response, split at two crossovers
    'multiband':  '<path d="M4 12h7v6h10v-8h7"' + acc + '/>' +
                  '<path d="M11 7v18M21 7v18" opacity=".28" stroke-dasharray="2 3"/>',
    // attack spike and its sustain
    'transient':  '<path d="M4 24h4l2-16 2 16h4"' + acc + '/>' +
                  '<path d="M16 24c4 0 8-3 12-3" opacity=".45"/><path d="M4 28h24" opacity=".26"/>',
    // the trigger, and the one sharp duck it causes
    'side-chain-master':'<circle cx="6" cy="16" r="3.4"' + accf + '/>' +
                  '<path d="M11 10h3l1 12c1-8 2-12 4-12h9"' + acc + '/>' +
                  '<path d="M11 27h18" opacity=".26"/>',
    // EQ curve with a node
    'eq':         '<path d="M3 20c5 0 6-9 11-9s6 12 11 12 4-5 4-5"/>' +
                  '<circle cx="14" cy="11" r="2.6"' + accf + '/>',
    // loudness bars under a target line
    'meter':      '<path d="M6 26V14M12 26V8M18 26V17M24 26V11"/><path d="M3 10h26"' + acc + '/>',
    // stereo width
    'imager':     '<path d="M16 5v22" opacity=".33"/>' +
                  '<path d="M12 12 5 16l7 4"' + acc + '/><path d="M20 12l7 4-7 4"' + acc + '/>',
    // noise in, signal out
    'clean':      '<path d="M3 16c2-7 3 7 5 0s3 7 5 0" opacity=".45" stroke-dasharray="2 2"/>' +
                  '<path d="M14 6v20" opacity=".22" stroke-dasharray="2 3"/>' +
                  '<path d="M17 16c4-6 8 6 11 0"' + acc + '/>',
    // two takes, and the shift that lines them up
    'align':      '<path d="M3 8c3-5 6 5 9 0s6 5 9 0"/><path d="M8 26c3-5 6 5 9 0s6 5 9 0"/>' +
                  '<path d="M10 17h12M12.5 14.5 10 17l2.5 2.5M19.5 14.5 22 17l-2.5 2.5"' + acc + '/>',
    // a mixer: three faders, set differently
    'mix':        '<path d="M8 5v22M16 5v22M24 5v22" opacity=".33"/>' +
                  '<rect x="5" y="18" width="6" height="3.6" rx="1.8"' + cf + '/>' +
                  '<rect x="13" y="8" width="6" height="3.6" rx="1.8"' + accf + '/>' +
                  '<rect x="21" y="14" width="6" height="3.6" rx="1.8"' + cf + '/>',
    // many sources, one destination
    'bus':        '<path d="M4 8h9c4 0 4 8 8 8h7M4 16h9M4 24h9c4 0 4-8 8-8" opacity=".55"/>' +
                  '<circle cx="28" cy="16" r="2.8"' + accf + '/>',
    // a deep sine, an octave down
    'sub-conjurer':'<path d="M3 15c4-9 8-9 12 0s8 9 12 0"' + acc + '/>' +
                  '<path d="M16 20v6M13 23l3 3 3-3" opacity=".55"/>',

    /* ── time / space ─────────────── silhouette: arcs, reels, sweeps, bars */
    // expanding reflections
    'space':      '<path d="M11 10a9 9 0 0 1 0 12" opacity=".35"/>' +
                  '<path d="M16 7a15 15 0 0 1 0 18" opacity=".55"/>' +
                  '<path d="M21 4a21 21 0 0 1 0 24"' + acc + '/><path d="M5 13v6"' + acc + '/>',
    // repeats, decaying in time
    'echo':       '<path d="M5 8v16"' + acc + '/><path d="M12 11v10" opacity=".65"/>' +
                  '<path d="M19 14v4" opacity=".42"/><path d="M25 15.5v1" opacity=".25"/>' +
                  '<path d="M4 28h22M23.5 25.5 26 28l-2.5 2.5" opacity=".28"/>',
    // reflections, pitched up
    'shimmer':    '<path d="M14 8a13 13 0 0 1 0 16" opacity=".4"/>' +
                  '<path d="M20 5a19 19 0 0 1 0 22" opacity=".55"/>' +
                  '<path d="M6 16l3-4 3 4-3 4z"' + accf + '/>',
    // tape: two reels and the wow in between
    'warble':     '<circle cx="8" cy="21" r="4.5"/><circle cx="24" cy="21" r="4.5"/>' +
                  '<circle cx="8" cy="21" r="1"' + cf + '/><circle cx="24" cy="21" r="1"' + cf + '/>' +
                  '<path d="M4 10c3-4 5 4 8 0s5 4 8 0 3-2 6-1"' + acc + '/>',
    // tempo pumping, on the grid
    'pulse':      '<path d="M3 12c3 0 3 10 6 10s3-10 6-10 3 10 6 10 3-10 6-10"' + acc + '/>' +
                  '<path d="M3 27h26" opacity=".26"/><path d="M9 24v3M15 24v3M21 24v3" opacity=".3"/>',
    // a sweep, building
    'riser':      '<path d="M4 26C13 26 17 7 27 7v19z" fill="' + A + '" opacity=".2" stroke="none"/>' +
                  '<path d="M4 26C13 26 17 7 27 7"' + acc + '/><path d="M4 26h24" opacity=".28"/>',
    // saturation, pushed
    'heat':       '<path d="M4 26c8 0 4-20 12-20 6 0 5 12 12 12" opacity=".33"/>' +
                  '<path d="M4 24c7 0 5-16 12-16 6 0 5 12 12 12"' + acc + '/>',
    // a bank of bands, one driven
    'vocoder':    '<path d="M5 25v-6M9.5 25v-11M18.5 25v-8M23 25v-13M27.5 25v-5"/>' +
                  '<path d="M14 25V8"' + acc + '/><path d="M3 28h26" opacity=".26"/>',

    /* ── instruments ──────────────── silhouette: grid, dots, slices, drum */
    // a waveform, sliced
    'chop':       '<path d="M3 16c2-7 3 7 5 0s3 7 5 0 3 7 5 0 3 7 5 0 3 4 4 2" opacity=".45"/>' +
                  '<path d="M10 6v20M20 6v20"' + acc + '/>',
    // a step grid, with hits
    'rhythm-genie':'<rect x="4" y="6" width="24" height="20" rx="3" opacity=".4"/>' +
                  '<path d="M4 13h24M4 19h24M10 6v20M16 6v20M22 6v20" opacity=".3"/>' +
                  '<rect x="5" y="7" width="4" height="5" rx="1"' + accf + '/>' +
                  '<rect x="17" y="14" width="4" height="4" rx="1"' + accf + '/>',
    // a cloud of grains
    'granulator': '<circle cx="7" cy="12" r="1.8"' + accf + '/><circle cx="13" cy="20" r="1.8"' + accf + '/>' +
                  '<circle cx="19" cy="9" r="1.8"' + accf + '/><circle cx="25" cy="18" r="1.8"' + accf + '/>' +
                  '<circle cx="10" cy="25" r="1.4" opacity=".5"' + cf + '/>' +
                  '<circle cx="22" cy="25" r="1.4" opacity=".5"' + cf + '/>' +
                  '<path d="M3 16h26" opacity=".2"/>',
    // the drum, and its long tail
    'eightoheight':'<circle cx="7" cy="16" r="4"' + accf + '/>' +
                  '<path d="M12 16c1.5-8 3 8 4.5 0s2.5 6 4 0 2 4 3 0h2"/>',
    // an arpeggio climbing
    'sattari-arp':'<path d="M6 24 27 9" opacity=".28"/>' +
                  '<circle cx="6" cy="24" r="2.2"' + cf + '/><circle cx="13" cy="19" r="2.2"' + cf + '/>' +
                  '<circle cx="20" cy="14" r="2.2"' + cf + '/><circle cx="27" cy="9" r="2.2"' + accf + '/>',
    // a chord stacked on a scale
    'scaler':     '<path d="M4 24h24M4 18h24M4 12h24" opacity=".26"/>' +
                  '<rect x="8" y="9" width="5" height="6" rx="1.5"' + accf + '/>' +
                  '<rect x="8" y="15" width="5" height="6" rx="1.5" opacity=".55"' + cf + '/>' +
                  '<rect x="19" y="15" width="5" height="6" rx="1.5" opacity=".55"' + cf + '/>',

    /* ── stems ──────────────────────────────────────────────────────────── */
    // one mix, pulled apart into offset layers
    'stemdeck':   '<path d="M3 8h18"/><path d="M11 14h18"' + acc + '/>' +
                  '<path d="M3 20h18" opacity=".7"/><path d="M11 26h18" opacity=".45"/>'
  };

  // Fallback: a neutral module glyph, so a new plugin never renders blank.
  var FALLBACK = '<rect x="5" y="5" width="22" height="22" rx="6"/>' +
                 '<circle cx="16" cy="16" r="3"' + accf + '/>';

  global.SATTARI_LOGO = function (id) {
    return svg(M[id] || FALLBACK);
  };
  global.SATTARI_LOGO.has = function (id) { return Object.prototype.hasOwnProperty.call(M, id); };
  global.SATTARI_LOGO.ids = function () { return Object.keys(M); };
})(window);

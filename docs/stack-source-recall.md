# Stack presets and source-file recall

Stack has two different save paths:

- **Settings presets** (`.sattaripreset`) store layer types, placement and parameter values. They do not contain imported audio or source-file references.
- **DAW project state** also stores the absolute source-file references for each layer's Chop source or Granulator A/B sources. The audio files themselves are not embedded.

Keep imported audio in a stable location. When moving a session to another computer or moving its media, reopen Stack, select each affected layer, and use **Load source A/B** to relink the files. Then save the DAW project again. Saving a settings preset alone does not make a portable, sample-inclusive instrument.

During project source restoration, Stack holds its output silent until the queued file reload finishes. Missing or unreadable sources are cleared and marked in the editor; Stack must not substitute a previously loaded sample. Source references remain in project state so missing media can be identified and recovered. Restoring a source-free state clears previous source assignments. Ordinary source-free synth state does not wait for file-loading work.

Preset save/load failures display an error and leave current settings active. Rapid saves receive distinct names instead of overwriting a preset saved in the same second.

Replacing Chop or Granulator sources also reclaims sample buffers that no current source, engine or voice still references. Reclamation happens on the message thread; the audio callback retains protection against freeing a sample buffer mid-block. Source handoff uses a non-blocking try-lock on the callback; if publication overlaps it, the engine retains its previous source for that block. This bounds unused import history, not the size of an individual imported file or the memory required by active voices.

The automated source-recall contract uses generated audio and real Chop/Granulator modules. Full supported-DAW project migration still requires the release acceptance matrix; a component test alone is not a host-compatibility approval.

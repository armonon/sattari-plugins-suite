# Auto Pitch selective port — 11 October 2026

Two useful older fixes were integrated into current native source without
replacing its newer live pitch engine, harmony workflows or current UI contracts.
Hosts can tear down an instance that was never prepared without resetting an
unconfigured optional Signalsmith object. Host bypass now delays dry audio by
the reported latency, and the delay receives dry input during normal processing,
so switching back to bypass cannot replay audio from an earlier bypass period.

The existing full AutoPitchCoreContract passed twice on arm64 with ONNX disabled:
first with optional Signalsmith absent, then with its pinned submodule linked.
Rubber Band remains the selected print engine in both runs. Each run constructs
and releases eight unprepared instances and checks bypass sample-for-sample at
64- and 512-sample block sizes. Existing workflow, harmony, pitch, noise and
song-key checks also pass. The regression additions came from the original
fixes, adapted to preserve current contracts and initialise JUCE before teardown.

The attached JSON records final binary and native source hashes. This does not
qualify Windows, Intel Auto Pitch, public installers, optional Signalsmith print
output, named DAWs or listening acceptance. Other commits in the older Auto Pitch
branch still require selective review; this is not a wholesale branch merge.

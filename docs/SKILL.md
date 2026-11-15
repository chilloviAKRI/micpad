---
name: micpad
description: Manage the local Micpad soundboard without changing PipeWire devices or defaults.
---

Use `micpad check --json` for read-only diagnostics, `micpad list --json`
for the library, `micpad add /absolute/audio/file.wav --json` to reference
a sound, `micpad play 1 --json` to send it, and `micpad stop --json` to stop.
Use `micpad pause --json` and `micpad resume --json` to preserve the current
position; `micpad seek 45 --json` seeks to 45 seconds without restarting the clip.
Open the app with `micpad open` before control commands. Local listening
works without a call. To send sounds to other apps, they must be recording
the selected physical microphone through PipeWire. Muting that microphone
blocks the capture feed while local listening continues.
Playback adds temporary client-owned links; never alter device defaults,
restart audio services, or create virtual devices to work around a failure.

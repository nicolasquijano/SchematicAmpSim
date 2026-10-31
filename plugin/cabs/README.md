# Built-in cabinet impulses

The WAV files placed in this folder are embedded in the plugin when it is built, and appear in the Load menus of the Cabinet page
(the display name comes from the file name: `cab_4x12_os_mix1.wav` becomes "4x12 OS Mix 1"; the list is in alphabetical order).

The files are **not in the repository** (`.gitignore` leaves `plugin/cabs/*.wav` out): they are third-party impulse responses and
their licences do not allow redistributing the files with the source. A clone of the repository builds without them, with an empty
built-in list; the Cabinet page still loads any file with "From file...".

To build a plugin that carries impulses, copy mono or dual-mono WAV files (any rate; the first channel is used) here before configuring.

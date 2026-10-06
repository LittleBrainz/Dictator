Dictator - Phase 4 live transcription

Download Dictator v0.4.0.zip and extract the complete artifact once, then double-click Dictator.exe.
Windows 11 x64 is required. No separate .NET, Windows App SDK, or development
tools are needed. Dictator.exe is a small launcher; the application and runtimes
are unpacked under lib/WinUI. Startup does not extract them into Temp.
Keep the complete lib folder next to Dictator.exe. The application and tray
use the same stylized microphone icon.

Startup shows a compact Widget and the Dictator notification-area icon.
Left-click the tray icon to reopen the Widget. Right-click it for Open Widget,
Open Settings, Restart Dictator, and Quit. Closing Settings leaves Dictator
running. Launching Dictator.exe again opens the existing Settings window.
The Widget's gear opens Settings; its close control dismisses only the Widget.

General provides Start with Windows, Theme and a configurable global Hotkey.
Focus an editable text field and press Ctrl+Alt+backslash, or the microphone.
Release before 500 ms to leave Talking on; hold at least 500 ms to stop on
release. If already Talking, pressing and releasing stops it. Losing the
eligible cursor stops capture. Escape continues to the focused application.
Speech settings selects the Windows default microphone or a specific microphone.
The waveform shows real microphone levels; silence remains a straight line.
Microphone changes/disconnection stop Talking and show an actionable error.
Audio is consumed and discarded from bounded memory; no recordings are saved.
OpenAI gpt-live-transcribe supplies raw text while you speak. Enter an OpenAI
API key in Speech settings; Windows Credential Manager stores it securely.
OpenAI API billing is separate from ChatGPT. Dictator sends audio to OpenAI
only while Talking and does not save recordings or transcripts.
Normal stop drains final audio and finalizes transcription. Speech settings
shows the latest final transcript (memory only) and offers Copy.
Focus loss cancels pending work; errors stop safely. Start Talking again to
reconnect after fixing an error. Automatic text insertion comes in Phase 5.
The inactive red microphone has a stronger glow and no diagonal slash.

Hold the waveform body to drag. Position survives close/reopen in this process
and resets on startup or Restart. Widget has five zoom stops and tooltips after
a one-second hover. The whole upper capsule fades in with very bright blue
12 DIP hover instructions already displayed. It is hidden when neither Talking
nor showing a hint. Long hints repeat continuously with "..." between messages.
Ticker and tooltip scrolling are 1.5 times faster than v0.2.6.
Only the waveform starts a drag; the full window height remains reserved for
screen bounds. The Widget is 216 DIP wide with unchanged button sizes. No separate
tooltip window appears. The gap between the capsules is transparent
and passes clicks to the desktop/application behind it. White raw text
scrolls from right to left during Talking. Hotkey/tray reopen a closed Widget even without a cursor.

Preferences are saved in %USERPROFILE%\.dictator\settings.json, independent
of the installation folder. Invalid or unsupported settings files are preserved
and changes blocked. Repair or move that file and restart to recover.
Diagnostics includes build identity, paths, defined timings and a Copy button.
Only en-GB, en-US, fr-FR and zh-CN locale directories are currently shipped.

This build is unsigned. Windows security policy may block it; Smart App Control
compatibility is not established. If launch fails, share the error and
lib/build-info.json. Never include API keys or private transcript data.

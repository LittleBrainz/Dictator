# Dictator — Implementation Specification v2

**Status:** Approved for fresh implementation handoff  
**Date:** 5 October 2026  
**Target:** Windows 11 x64  
**Product name:** Dictator  
**Settings window title:** Dictator Settings  
**Purpose:** Personal-use, system-wide Windows voice dictation assistant inspired by the interaction quality of Wispr Flow, implemented as a Windows-first application with a managed C# application layer and a deliberately narrow native C++ core.

**Post-handoff revision, 6 October 2026:** The user requires a small root launcher with unpacked application/runtime files under `lib`, avoiding first-launch runtime extraction. Persistent user data belongs in `%USERPROFILE%\.dictator` (`~/.dictator`), independently of the installation location. These explicit instructions supersede the original deployment/data-path choices in sections 28–29.

---

## Accepted implementation updates

Phase 0 was accepted by the user after successful Windows CI and manual launch.
The current implementation scope is Phase 1. The testing artifact retains only
`en-GB`, `en-US`, `fr-FR` and `zh-CN` locale directories; other locales can be
reintroduced later. Neutral runtime resources remain intact.

The user also requires versioned artifact ZIP names (for example,
`Dictator v0.1.0.zip`) and a stylized microphone application/tray icon. This
supersedes the original unversioned artifact-name requirement below.

## 1. Executive summary

Build **Dictator**, a resident Windows 11 application that provides low-friction system-wide voice dictation.

The user invokes Dictator with one configurable global **Hotkey**. A short press toggles Talking on; a hold behaves as push-to-talk and stops on release. A small non-activating **Widget** remains available above normal applications, indicates whether the current focus is an eligible text insertion cursor, shows live audio graphics while Talking, and displays streaming raw transcription as speech is recognized. When speech ends, Dictator optionally transforms the transcript, applies deterministic dictionary and Phrase Shortcut rules, and inserts the final text into the intended target without stealing focus.

Dictator has two user-facing surfaces:

1. **Dictator Settings** — a conventional WinUI 3 settings window for General, Widget, Speech, AI/Models, Dictionary, Phrase Shortcuts, History, Applications, Diagnostics, privacy, and related configuration.
2. **Widget** — a compact, borderless, interactive, non-activating, always-on-top tool window used for Talking state, waveform, streaming transcript, drag/repositioning, Settings access, and close/recovery behavior.

The replacement implementation is a deliberate **hybrid**:

- **C# / modern .NET / WinUI 3** for the application host, Settings UI, state/orchestration, persistence, STT networking, OpenRouter, processing, history, diagnostics, and other non-real-time product logic.
- **C++** for timing-sensitive or deeply Win32-specific facilities where native control is valuable: WASAPI capture, bounded audio buffering, the non-activating Widget and rendering, foreground/editable-target tracking, low-level Hotkey gesture handling where required, and text insertion.
- The C#↔C++ boundary must be small, explicit, versioned, and mechanically testable.

This is a **fresh repository**. The deleted C++ prototype is evidence for behavior and regression cases only. Do not port its internal architecture merely because it previously existed.

Development is agent-first:

- **Codex Cloud is the primary coding environment.**
- **GitHub is the source of truth.**
- **GitHub Actions on standard hosted Windows runners is the authoritative Windows compiler/test/package environment.**
- **Codex CLI in WSL is optional** for local inspection or small changes.
- The user's Windows machines must not require ChatGPT/Codex Desktop, Visual Studio, MSVC, a Windows SDK, or a separately installed .NET runtime in order to run Dictator.

---

## 2. Sources of truth and precedence

This specification supersedes **Native Windows Dictation Assistant — Implementation Specification v1**.

It incorporates:

- the original product requirements;
- the architectural discussion that replaced the all-C++ design with C#/.NET plus a narrow C++ core;
- the accepted Phase 0–2 behavior from the discarded C++ prototype;
- the final refinement handoff titled **Dictator - Changes Since Specification v1**;
- current stable Microsoft platform versions verified on 5 October 2026.

If an earlier document, prototype behavior, comment, README, or old repository convention conflicts with this document, **this document wins**.

Do not infer requirements from the deleted repository. Recreate behavior from this specification.

---

## 3. Product goals

Dictator must:

- provide fast, low-friction system-wide voice dictation on Windows 11;
- use one configurable global Hotkey with both tap-to-toggle and hold-to-talk semantics;
- provide immediate visual confirmation of readiness and Talking state;
- distinguish a valid editable text insertion cursor from merely having a foreground window;
- show a responsive live audio waveform while real audio is captured;
- show streaming raw transcription while the user speaks;
- preserve the target application's keyboard focus during normal Widget and Hotkey interaction;
- insert final text at the intended caret safely and conservatively;
- support Raw, Verbatim, Natural, and Polished transcript treatment modes;
- support OpenRouter as the first LLM provider, with model selection;
- use a pluggable streaming STT provider architecture;
- use OpenAI live transcription as the initial STT provider;
- support global and per-application dictionaries;
- support deterministic Phrase Shortcuts whose replacement values are protected from LLM rewriting;
- maintain searchable local dictation history;
- be resident in the notification area until the user explicitly chooses Quit;
- optionally start with Windows without elevation;
- provide robust diagnostics without leaking secrets or transcript content by default;
- retain no microphone audio after processing;
- send no application telemetry;
- remain responsive even while network operations are slow or unavailable;
- be straightforward for Codex to maintain, test, build, and package through CI.

---

## 4. Explicit non-goals for v1

Do **not** expand v1 into the following unless a later specification revision explicitly requires it:

- macOS or Linux desktop clients;
- Windows 10 support;
- ARM64 release artifacts;
- mobile clients;
- elevated/Administrator target insertion;
- persistent audio recordings;
- always-listening wake-word capture;
- meeting transcription;
- speaker diarization;
- browser extensions;
- cloud synchronization of history or settings;
- a custom user-account system;
- a hosted Dictator backend;
- enterprise deployment or multi-user administration;
- Microsoft Store publication;
- full IME/Text Services Framework replacement;
- autonomous microphone capture without an explicit user gesture;
- Native AOT as a v1 requirement;
- cross-process service architecture unless a concrete defect proves it necessary.

The design should leave clean seams for future work, but do not add speculative infrastructure that complicates v1.

---

## 5. Current stable platform baseline

### 5.1 Policy

Use the **latest stable, production-supported generation** of the Microsoft stack available when the fresh repository is bootstrapped.

Do not use Preview, RC, Experimental, Insider-only, or unsupported releases merely because they have a higher version number.

As of **5 October 2026**, the required baseline is:

| Component | Baseline |
| --- | --- |
| .NET | **.NET 10 LTS**, latest supported servicing patch |
| C# | **C# 14** |
| Windows App SDK | **2.5.1 Stable** |
| WinUI | **WinUI 3 included with Windows App SDK 2.5.1** |
| Windows SDK | Latest stable Windows 11 SDK available on the hosted runner; currently the 10.0.28000 series, with 10.0.28000.2957 current on 5 Oct 2026 |
| C++ | C++23 where supported by the selected MSVC toolchain |
| Architecture | win-x64 only |

.NET 11 is RC as of this specification date and is therefore **not** the baseline.

### 5.2 Version maintenance

At repository creation:

1. verify current stable versions from Microsoft primary documentation;
2. pin the chosen SDK/dependency versions in source control where deterministic builds require pinning;
3. record them in diagnostics/build metadata;
4. use the latest servicing patch of the chosen stable generation;
5. allow Dependabot/Renovate-style visibility for updates if useful, but do not auto-merge platform upgrades without a green packaged build and manual smoke test.

If a new stable major generation becomes available **before implementation materially begins**, prefer the newer stable generation after confirming WinUI compatibility. Once active implementation is underway, major framework migration is a deliberate engineering change, not an automatic churn event.

### 5.3 References verified for this baseline

- Windows App SDK release channels: https://learn.microsoft.com/windows/apps/windows-app-sdk/experimental-channel
- Windows App SDK 2.0/2.x release notes: https://learn.microsoft.com/windows/apps/windows-app-sdk/release-notes/windows-app-sdk-2-0
- Windows SDK downloads: https://learn.microsoft.com/windows/apps/windows-sdk/downloads
- .NET support policy: https://dotnet.microsoft.com/platform/support/policy/dotnet-core
- .NET downloads: https://dotnet.microsoft.com/download
- C# 14: https://learn.microsoft.com/dotnet/csharp/whats-new/csharp-14/

---

## 6. Language and runtime architecture

### 6.1 Managed application layer

Use **C# 14 on .NET 10 LTS** for most application code.

The managed layer owns:

- application lifecycle/orchestration;
- Dictator Settings / WinUI 3 UI;
- notification-area menu orchestration;
- preferences and migrations;
- STT provider sessions and network protocol handling;
- OpenRouter provider and model selection;
- transcript processing pipeline;
- dictionaries and Phrase Shortcuts;
- SQLite history;
- provider retry/error policy;
- Windows Credential Manager orchestration;
- diagnostics/logging policy;
- feature-level state machine;
- tests for business logic and provider contracts.

Use normal safe managed patterns. Prefer async/await, cancellation tokens, immutable/record-like data contracts where useful, dependency injection only where it genuinely improves testability, and clear ownership over framework-heavy ceremony.

### 6.2 Native core

Use a compact **C++23 native DLL** for facilities that benefit from deterministic timing or direct Win32 control.

Working name:

`Dictator.Native.dll`

The native core owns or may own:

- WASAPI capture;
- bounded PCM buffering/ring buffers;
- resampling or format conversion where required for STT;
- non-activating Widget HWND lifecycle;
- Direct2D/DirectWrite/DirectComposition rendering where used;
- native Widget hit-testing, drag behavior and tooltip windows if that best preserves focus;
- foreground-window and focused editable-control eligibility tracking;
- low-level keyboard/hotkey gesture handling if required to implement key-up semantics reliably;
- text insertion strategies;
- clipboard capture/restore around insertion;
- `SendInput` fallback;
- small native diagnostics needed to support those components.

Do **not** move networking, JSON, OpenRouter, history, settings, or general application state into C++ merely because C++ exists in the solution.

### 6.3 No C++/CLI and no custom WinRT component in v1

The initial managed/native bridge must use a **small C ABI + P/Invoke**.

Do not use C++/CLI.

Do not create a custom WinRT component unless the C ABI becomes demonstrably unwieldy and a written architectural decision explains why WinRT projection is worth the additional machinery.

### 6.4 ABI design rules

Expose native functions through a stable header such as:

`src/Dictator.Native/include/dictator_native.h`

Rules:

- export plain C-callable functions;
- use opaque handles rather than exposing C++ classes;
- never expose STL types across the ABI;
- use fixed-width integer types and explicitly sized structs;
- define UTF-16 string ownership clearly;
- never throw C++ exceptions across the ABI;
- return explicit result/error codes;
- version the ABI;
- document ownership for every buffer and handle;
- callbacks into managed code must never occur from the real-time audio callback thread;
- keep the ABI narrow enough that contract tests can cover it exhaustively.

Prefer source-generated P/Invoke with `LibraryImport` where appropriate.

### 6.5 Native audio boundary

The real-time audio callback must not block on:

- managed code;
- UI work;
- network I/O;
- disk I/O;
- locks with unbounded contention;
- logging;
- allocation patterns that can stall unpredictably.

Preferred shape:

1. WASAPI callback/capture thread writes PCM into a bounded native ring buffer.
2. A non-real-time consumer path exposes chunks to the managed STT layer.
3. Managed code reads or receives chunks on a background task.
4. Waveform/level state is coalesced for display rather than crossing the ABI per sample.
5. Backpressure has an explicit drop/error policy; memory use must remain bounded.

### 6.6 No Native AOT requirement

Do not require Native AOT for v1.

Use ordinary supported self-contained .NET publishing. Native AOT may be reconsidered only if:

- WinUI/Windows App SDK support is mature for the exact configuration;
- all dependencies are compatible;
- measured startup/memory benefits justify added complexity;
- packaged acceptance remains green.

---

## 7. UI framework decisions

### 7.1 Dictator Settings

Use **WinUI 3** through the current stable Windows App SDK.

Requirements:

- conventional resizable Windows 11 settings window;
- title exactly **Dictator Settings**;
- Fluent design consistent with current Windows 11 conventions;
- System/Light/Dark theme selection;
- no browser/WebView primary UI;
- Settings closing hides/closes only Settings, never the resident application.

### 7.2 Widget

The Widget is not a conventional Settings window.

It must preserve the successful behavior proven in the prototype:

- borderless tool window;
- non-activating;
- topmost while shown;
- interactive to mouse input;
- not click-through;
- preserves the target application's keyboard focus during ordinary Widget interaction;
- compact;
- draggable from its body/waveform area;
- explicitly closable;
- recoverable from tray or Hotkey;
- DPI/multi-monitor correct.

The preferred implementation is a **native Win32 HWND rendered in the C++ core**. Use Direct2D/DirectWrite/DirectComposition or another native rendering path where it produces reliable animation and no-activation behavior.

Do not reimplement the Widget as an ordinary WinUI top-level window if that compromises focus, Z-order, latency, mouse behavior, or tooltip placement.

---

## 8. High-level component architecture

Use a **single user-mode application process** in v1 with a loaded native DLL. Do not introduce a Windows service or privileged helper.

Logical components:

1. **Managed Application Host**
   - startup and shutdown;
   - single-instance coordination;
   - tray menu orchestration;
   - Settings lifecycle;
   - Restart/Quit;
   - startup-with-Windows preference.

2. **Managed Settings UI**
   - WinUI pages and view models/state;
   - preferences;
   - provider/model configuration;
   - history, dictionary, Phrase Shortcut, application rule and diagnostics UI.

3. **Managed Dictation Orchestrator**
   - user-visible state machine;
   - native session commands;
   - STT session lifecycle;
   - processing pipeline;
   - insertion coordination;
   - history commit;
   - error/fallback policy.

4. **Native Input/Target Service**
   - configured global Hotkey;
   - 500 ms tap/hold semantics;
   - key repeat/modifier edge cases;
   - editable-text-cursor eligibility;
   - target HWND/process identity;
   - focus-safe Widget input.

5. **Native Widget**
   - show/hide;
   - drag;
   - close;
   - tools button;
   - microphone button;
   - state coloring;
   - waveform rendering;
   - streaming text surface;
   - contextual tooltips;
   - per-session position.

6. **Native Audio Engine**
   - microphone enumeration and selection;
   - WASAPI shared-mode capture;
   - bounded buffering;
   - audio format conversion;
   - level metering;
   - no audio persistence.

7. **Managed STT Provider Layer**
   - provider-neutral streaming interface;
   - initial OpenAI live transcription implementation;
   - raw transcript deltas;
   - final transcript;
   - network cancellation/errors.

8. **Managed Text Processing Pipeline**
   - normalization;
   - dictionaries;
   - Phrase Shortcut protection/expansion;
   - optional LLM transform;
   - final normalization.

9. **Managed OpenRouter Provider**
   - credential handling;
   - current model discovery/configuration;
   - request/response handling;
   - timeouts and fallback.

10. **Native Insertion Engine**
    - target validation;
    - preferred insertion strategy;
    - clipboard/paste strategy;
    - Unicode `SendInput` fallback;
    - clipboard restoration;
    - conservative failure behavior.

11. **Managed Persistence**
    - versioned settings;
    - SQLite history;
    - application rules;
    - dictionary and Phrase Shortcut data;
    - migrations.

12. **Diagnostics**
    - managed + native build identity;
    - sanitized recent errors;
    - startup measurements;
    - provider/session timings without content leakage;
    - copyable report.

---

## 9. Application lifetime and resident behavior

Dictator is fundamentally a **resident tray application**.

Rules:

- Dictator remains active until the user explicitly selects **Quit**.
- There is no “Minimize to tray” or “Start minimized” preference. Tray residence is not optional behavior.
- Normal startup shows the Widget and keeps Dictator Settings hidden.
- Restart Dictator also starts with the Widget visible and Settings hidden.
- Closing Dictator Settings does not stop Dictator, hide the tray icon, close the Widget, or unregister the Hotkey.
- Closing the Widget dismisses only the Widget.
- The tray is the recovery route for a closed Widget.
- Pressing the Hotkey while the Widget is closed must also reopen it, even if there is no eligible text cursor.
- Dictator is single-instance.
- A second normal/manual launch opens Dictator Settings in the existing instance.
- A duplicate startup invocation must remain quiet rather than unexpectedly opening Settings.
- **Restart Dictator** performs orderly shutdown and relaunch of the same installed/staged application, releasing native resources and registrations first.
- Restart resets the Widget position to default.
- **Start with Windows** is a reversible per-user preference and must not require elevation.

Carry the prototype's intermittent shutdown-hang history as a regression scenario. In particular, test Quit after Settings infrastructure has been created but the Settings window has never been activated.

---

## 10. Notification-area behavior

The exact right-click menu is:

1. **Open Widget** — default action.
2. **Open Settings**.
3. separator.
4. **Restart Dictator**.
5. **Quit**.

Requirements:

- left-clicking the tray icon executes **Open Widget**;
- opening the Widget from tray must preserve the foreground application's keyboard focus;
- Open Settings may activate Dictator Settings explicitly;
- no placeholder “Dictation coming in later phase” text;
- do not add listening enable/disable switches, microphone quick menus, or mode quick menus to Phase 1 unless a later requirement explicitly adds them.

---

## 11. Dictator Settings navigation

Navigation order begins:

1. **General**
2. **Widget**
3. **Speech**
4. **AI / Models**
5. **Dictionary**
6. **Phrase Shortcuts**
7. **History**
8. **Applications**
9. **Diagnostics**

Additional pages may be introduced only if they improve coherence without duplicating controls.

### 11.1 General

Contains:

- **Start with Windows**;
- the single configurable Talking **Hotkey**;
- **Theme**: System, Light, Dark.

### 11.2 Widget

Contains:

- **Widget Zoom** using the five fixed stops defined later.

Do not create a separate Widget/Appearance page for v1.

Do not add the prototype-era removed controls:

- Reduce overlay animation;
- Overlay text size.

Respect Windows accessibility and reduced-animation preferences automatically.

### 11.3 Speech

Planned controls include:

- STT provider;
- STT credential status;
- microphone selection;
- language behavior;
- provider/model option where relevant;
- connection/test facility;
- privacy explanation.

### 11.4 AI / Models

Planned controls include:

- OpenRouter credential status;
- model discovery/selection;
- default processing mode;
- provider test;
- transform timeout/fallback behavior where appropriate.

### 11.5 Dictionary

See Dictionary section.

### 11.6 Phrase Shortcuts

See Phrase Shortcuts section.

### 11.7 History

See History section.

### 11.8 Applications

See per-application behavior section.

### 11.9 Diagnostics

Expose sanitized operational information and explicit Copy action.

---

## 12. Hotkey and combined gesture

### 12.1 One Hotkey

Use **one configurable Hotkey**.

Default:

**Ctrl+Alt+\\** (backslash)

`Ctrl+Alt+Space` is explicitly rejected due to conflicts. The old separate hold and toggle bindings are obsolete.

UI terminology:

- use **Hotkey** for the keyboard combination;
- reserve **Phrase Shortcuts** for spoken phrase expansion;
- Widget copy uses **Talk / Talking**, not Record / Recording.

### 12.2 Tap/hold threshold

Use an exact **500 ms** boundary.

| Starting state | Gesture | Result |
| --- | --- | --- |
| Off, valid text cursor | Press, release before 500 ms | Start on press; remain on after release. |
| Off, valid text cursor | Hold for at least 500 ms | Active while held; stop on release. |
| Already on | Brief press | Remain active until release, then stop. |
| Already on | Longer press | Remain active while held; stop on release. |
| No valid text insertion cursor | Either gesture | Do not start an active session; reopen a closed Widget if necessary. |

The microphone button uses the same click/click-hold semantics and timing.

### 12.3 Input correctness

Requirements:

- key repeat must not restart the gesture or reset timing;
- modifier release order must behave consistently;
- an input source must not accidentally finish a gesture started by another source;
- rebind conflicts must be detected and surfaced;
- test both UK and US keyboard layouts for backslash handling;
- do not globally consume unrelated keystrokes.

### 12.4 Escape

Phase 2 must **not** register Escape as cancel. Escape must continue to reach the focused application.

Later real-dictation cancellation/undo remains a design gate; see Phase 5. Do not silently reintroduce Escape.

---

## 13. Editable cursor eligibility and target context

A “valid target” means a **focused, editable text-entry field/insertion cursor** in the target application.

A foreground application by itself is insufficient.

Treat the following as ineligible:

- no focused control;
- disabled control;
- read-only text control;
- non-text control;
- unrecognized target where editable insertion cannot be established safely.

Requirements:

- continuously refresh eligibility while the Widget is visible, including while inactive;
- verify eligibility again immediately before starting a session;
- inspect accessibility/focus/editability metadata without reading the target's text;
- capture target identity at session start;
- do not elevate;
- loss of eligible focus while active ends the active preview/real capture and returns to inactive state;
- never continue against a stale target merely because it was valid earlier.

Target context should minimally include:

- HWND/process identity;
- executable/application key for per-app settings;
- eligibility state;
- monitor/work-area context needed for Widget placement;
- enough identity to verify safe insertion later.

Do not log target text.

---

## 14. Widget specification

### 14.1 General behavior

The Widget:

- is visible on normal application start and Restart;
- remains visible after a hold ends or a toggled session stops;
- closes only when explicitly closed or the whole application quits;
- is clickable and not click-through;
- remains non-activating;
- does not steal keyboard focus during show, Hotkey operation, microphone interaction, drag, tooltip display, or ordinary state transitions;
- may allow Dictator Settings to activate only when the tools button explicitly requests Settings.

### 14.2 Controls and hit regions

Use the latest user-supplied mock-up: two separate glossy blue capsules with
cyan/chrome rims and dark navy interiors, a text capsule above a control/waveform
capsule, left circular microphone, cyan waveform in the middle, and circular
Settings/Close controls on the right. The approximately 1 DIP gap is genuinely
transparent and click-through: show the desktop/application behind it. Do not
paint or simulate a desktop background in the gap.
Keep the Widget itself compact; the mock-up desktop background is not part of it.
The width is 80% of the former 270 DIP width, with unchanged button sizes.
Hide the complete upper capsule while neither Talking nor displaying a hint.
On activation, fade the entire capsule in over approximately 180 ms, including
its border, background and already-present text. Keep the full 62 DIP window
height reserved for clamping while the upper capsule is hidden.
The Widget uses this blue palette in all Settings themes.

Required regions:

- waveform body (the only drag surface);
- upper text strip (never a drag surface);
- microphone button;
- tools/gear button;
- close `x` button.

Behavior:

- click-hold on the waveform area alone drags the Widget;
- dedicated controls retain their own behavior and do not start drag;
- close cancels any active Phase 2 preview and closes only the Widget;
- tools opens **Dictator Settings**;
- microphone uses the same 500 ms tap/hold semantics as the Hotkey.

### 14.3 Visual state

When there is **no valid text insertion cursor**:

- waveform is a single red straight line;
- microphone is off/red/slashed;
- no waveform animation runs;
- a Talking session cannot start.

When there is a valid cursor but Dictator is inactive:

- microphone is red and diagonally slashed whenever inactive;
- waveform is a single cyan straight line;
- no animated waveform.

When active:

- microphone is green;
- waveform animates;
- in Phase 2 this is explicitly simulated preview state, not evidence of real audio capture;
- from Phase 3 onward waveform must be driven by actual capture levels.

Remove the old Preview → Finish → Complete cycle. Do not add “Listening preview” or “Press toggle…” status lines.

### 14.4 Streaming transcript area

While Talking, the top capsule smoothly scrolls white live raw text from right
to left like a news ticker. Append deltas without restarting or repeating the
existing text. The user clarified that "translated" means speech-to-text in the
spoken language, before formatting; no language translation is implied.
Phase 2 supplies a bounded, session-checked presentation bridge for future STT
deltas but no microphone/provider. In normal Phase 2 operation the top capsule
is empty while Talking. Do not render the mock-up example as a live transcript.
From Phase 4 onward real streaming transcription feeds this ticker.

Desired behavior:

- compact when only waveform/state is needed;
- keep incoming deltas in a compact single-line ticker;
- do not obscure a large portion of the target application;
- retain raw transcript while processing if useful to reassure the user that work is continuing;
- clearly distinguish errors from normal processing without becoming a modal surface;
- after successful insertion, return to quiet state without automatically dismissing the Widget.

### 14.5 Size

Use a compact design informed by the accepted prototype:

- base size **216 by 62 DIP** at 1.000 zoom;
- waveform/body width is compact, roughly 60% of the earlier prototype direction;
- final dimensions may be tuned during Phase 2 acceptance, but do not drift back to a broad status panel.

### 14.6 Widget Zoom

Use exactly five logarithmically spaced factors:

| Stop | Factor |
| --- | ---: |
| 1 | 0.750 |
| 2 | 0.866 |
| 3 | **1.000 — default** |
| 4 | 1.155 |
| 5 | 1.333 |

Requirements:

- slider snaps to these values;
- no percentage label required;
- scale the complete Widget consistently;
- persist selected factor;
- changing zoom must re-clamp the Widget to the work area.

### 14.7 Positioning

Default placement:

- bottom-center of the relevant monitor's work area;
- clear of taskbar/work-area exclusions;
- raised by approximately **half the current zoomed Widget height** beyond a normal bottom margin;
- add approximately **half the taskbar height** of clearance, using the relevant
  monitor taskbar dimensions independently of Widget Zoom;
- reserve the full bottom taskbar area even when it is auto-hidden.

Position rules:

- only the waveform initiates dragging; the upper text strip does not;
- clamping reserves both capsules even when the upper capsule is hidden;
- dragging position is remembered only for the current Dictator process session;
- close/reopen within the same session restores the dragged position;
- every fresh application start or Restart resets position to default;
- do not persist Widget coordinates across application restarts;
- clamp for work-area boundaries, negative monitor coordinates, DPI changes, zoom changes, disconnected monitors, and monitor topology changes.

### 14.8 Tooltips

When not Talking, hover information appears **inside the top text capsule** in
very bright blue (#70DEFF). Wait **1 second**, then fade in the whole upper
capsule over approximately 180 ms with the beginning of the hint already visible. Moving
between hit regions restarts the delay. Leaving, dragging or pressing a control
clears the hint. Captions use a 12 DIP font without increasing the 24 DIP text capsule height.
Long information scrolls gently in a continuous loop, with the next repetition
immediately following the end using a spaced "..." to distinguish repetitions.
While Talking the strip belongs exclusively to white raw text; suppress hints.

Retain the contextual information for waveform/body, microphone, tools and close.
Update visible information when cursor eligibility changes. Render the following
copy as a single line, joining its lines/dividers with a spaced middle dot.
Display the currently configured Hotkey. Do not create a separate popup tooltip.

**Waveform area — valid cursor:**

```text
Ready to Insert Text
────────────────
Hold to Drag Widget
```

**Waveform area — no valid cursor:**

```text
No Text Insertion Cursor
────────────────
Hold to Drag Widget
```

**Microphone — no valid cursor:**

```text
No Text Insertion Cursor
```

**Microphone — off, valid cursor:**

```text
Click to Start Talking
Hold to Talk - Release to Stop
────────────────
Hotkey: Ctrl-Alt-\
```

**Microphone — on:**

```text
Click to Stop Talking
────────────────
Hotkey: Ctrl-Alt-\
```

**Close:**

```text
Close Widget
────────────────
Reopen from Windows System Tray
Hotkey: Ctrl-Alt-\
```

**Tools:**

```text
Open Dictator Settings
```

---

## 15. Audio capture

### 15.1 Device handling

Provide:

- default-input-device mode;
- explicit microphone selection;
- stable device identity where Windows APIs allow;
- device disappearance handling;
- device-change notification;
- understandable errors if capture cannot start.

### 15.2 WASAPI

Use **WASAPI shared-mode, event-driven capture** unless measurement proves another mode necessary.

Requirements:

- keep the real-time path native;
- no blocking network/UI/disk work on capture callback threads;
- bounded buffers;
- format conversion/resampling only as needed;
- surface normalized level data for Widget animation;
- stop capture promptly when the session ends or eligibility is lost;
- release device resources on Restart/Quit.

### 15.3 Audio privacy

Audio is transient.

Do not:

- write audio to disk;
- add a hidden cache;
- retain recordings in history;
- include audio in diagnostic bundles.

A future explicit recording feature would require a separate privacy decision.

---

## 16. Streaming STT provider architecture

### 16.1 Managed provider contract

Define a provider-neutral managed abstraction supporting:

- start session;
- stream PCM/audio chunks;
- transcript delta events;
- final transcript event;
- provider/session metadata;
- graceful stop;
- cancellation;
- timeout/error reporting.

The application state machine must depend on the interface rather than OpenAI-specific event names.

### 16.2 Initial provider

Initial provider: **OpenAI live transcription**.

Current intended model on 5 October 2026: **`gpt-live-transcribe`**.

Treat the exact model identifier as centrally configured provider metadata rather than scattering it through the codebase, so a successor model can be adopted without architectural changes.

Use current official OpenAI Realtime/transcription protocol documentation at implementation time.

### 16.3 Managed networking

Networking belongs in C# unless profiling demonstrates a concrete need otherwise.

Use modern .NET HTTP/WebSocket APIs with:

- cancellation tokens;
- bounded send queues;
- explicit timeouts;
- TLS validation;
- clean shutdown;
- no API key in logs.

### 16.4 Language

Default to automatic/appropriate English transcription behavior, while keeping provider language configuration extensible.

The retained WinUI resource locales do not imply STT language restrictions.

### 16.5 Failure behavior

On STT failure:

- stop/close the active capture cleanly;
- keep the Widget available;
- show a compact actionable error state;
- do not insert partial text unless an explicit recovery action later permits it;
- log technical error metadata without transcript content by default.

---

## 17. Transcript processing pipeline

Use an explicit ordered pipeline.

Recommended conceptual order:

1. final STT transcript;
2. normalization needed for matching;
3. identify/resolve Phrase Shortcuts into protected tokens;
4. dictionary normalization/substitution where appropriate;
5. optional LLM transformation;
6. restore protected Phrase Shortcut values exactly;
7. final normalization;
8. target validation;
9. insertion;
10. history commit.

The exact dictionary/shortcut order may be adjusted if tests expose ambiguity, but deterministic replacements must remain deterministic and testable.

### 17.1 Processing modes

Support:

- **Raw** — final STT output with only minimum safe normalization; no LLM.
- **Verbatim** — preserve wording while improving punctuation/capitalization and obvious transcription formatting.
- **Natural** — remove filler and repair obvious spoken false starts while preserving meaning and voice.
- **Polished** — convert speech into cleaner written prose while preserving intent.

No mode may invent substantive facts.

### 17.2 Phrase protection

Phrase Shortcut replacement values may contain:

- email addresses;
- URLs;
- code;
- multi-line signatures;
- punctuation-sensitive text;
- names or product identifiers.

The LLM must not rewrite these values. Use placeholders/protected tokens across the transform boundary and restore the exact configured replacement afterward.

### 17.3 Prompt versioning

Prompts are product code.

- keep them in source control;
- assign versions;
- test representative cases;
- store prompt version in history where useful for diagnostics;
- avoid embedding API secrets or user history in system prompts.

---

## 18. OpenRouter integration

OpenRouter is the initial LLM transformation provider.

Requirements:

- provider interface independent of OpenRouter-specific schema;
- API key in Windows Credential Manager;
- model list/discovery where practical;
- user-selected model;
- model identifier stored in preferences/history metadata;
- conservative timeout;
- cancellation when user/session state invalidates the request;
- Raw path remains fully usable without an OpenRouter key;
- if transform fails, preserve the final raw transcript and offer/perform a documented fallback rather than losing dictation.

Do not pin an arbitrary LLM in this specification. Model availability changes quickly; Settings should use current OpenRouter models.

---

## 19. Dictionaries

### 19.1 Scope

Support:

- global dictionary;
- per-application dictionary entries/overrides.

### 19.2 Entry model

Minimum fields:

- id;
- spoken/recognized form;
- desired written form;
- enabled;
- scope;
- optional matching behavior metadata;
- created/modified timestamps if useful.

### 19.3 Matching

Dictionary behavior must be deterministic.

Consider:

- word boundaries;
- case-insensitive matching where appropriate;
- preserving desired output casing;
- avoiding accidental substring replacement;
- tests for punctuation adjacency;
- collisions between global and application-specific entries.

Application-specific entries take precedence over global entries where they target the same recognized form.

### 19.4 STT hinting

If an STT provider offers safe vocabulary/prompt hinting, dictionary terms may be supplied as hints, but provider hinting is an optimization only. Deterministic post-processing remains authoritative.

### 19.5 UI

Dictionary page should support:

- add/edit/delete;
- enable/disable;
- search/filter;
- scope selection;
- clear indication of global versus app-specific behavior.

---

## 20. Phrase Shortcuts

### 20.1 Purpose

Phrase Shortcuts expand a spoken trigger into exact configured text.

Examples:

- “my email” → an exact email address;
- “signature one” → a multi-line signature;
- a spoken phrase → exact URL/code/snippet.

### 20.2 Entry model

Minimum fields:

- id;
- spoken trigger;
- exact replacement text;
- enabled;
- scope;
- matching mode if needed;
- created/modified timestamps if useful.

### 20.3 Processing requirements

- deterministic;
- exact replacement preservation;
- protected from LLM rewriting;
- global + per-application scope;
- application-specific conflict precedence;
- robust around punctuation/capitalization;
- multi-line replacement supported.

Do not conflate Phrase Shortcuts with keyboard Hotkeys in naming or code models.

---

## 21. Target safety and insertion

### 21.1 Capture target

At session start capture enough target context to know where insertion is intended.

Do not simply call `GetForegroundWindow()` after network processing and insert into whatever happens to be active.

### 21.2 Focus preservation

Normal Hotkey and Widget use must preserve the target application's focus.

Dictator Settings is the explicit exception: opening Settings intentionally activates a Dictator window.

### 21.3 Target changed during processing

Before insertion:

- revalidate the target;
- confirm it still represents a safe intended insertion context;
- do not inject into a newly focused unrelated application.

If the original target is no longer safe:

- do not insert automatically;
- retain final text in history/session state;
- surface a clear recovery action such as Copy.

### 21.4 Elevated targets

Dictator runs unelevated.

Insertion into higher-integrity/elevated applications is unsupported in v1. Surface the limitation; do not run Dictator permanently as Administrator.

---

## 22. Native insertion engine

### 22.1 Goals

Insertion must be reliable in common targets such as:

- modern browsers;
- Notepad;
- Microsoft Office text editors;
- VS Code;
- ordinary Win32/WinUI/WPF text fields where accessible.

### 22.2 Strategy abstraction

Implement insertion behind a native strategy layer.

Preferred hierarchy may include:

1. a direct accessibility/text insertion approach where reliable and non-destructive;
2. clipboard + simulated paste;
3. Unicode `SendInput` fallback.

Do not force one technique across every application.

### 22.3 Clipboard preservation

For clipboard-based insertion:

- capture existing clipboard state conservatively;
- place final text;
- invoke paste into the validated target;
- restore prior clipboard content when feasible without racing unrelated user clipboard changes;
- handle clipboard contention/retry with bounded waits;
- never silently destroy clipboard contents in ordinary successful cases.

### 22.4 `SendInput`

When using `SendInput`:

- respect UIPI limitations;
- use Unicode input appropriately;
- avoid per-character delays unless a target-specific compatibility rule requires them;
- do not assume success without checking context/result.

### 22.5 Per-app overrides

Allow later per-application insertion-strategy overrides where automatic selection is unreliable.

---

## 23. Cancel and undo design gate

The earlier v1 requirement for Escape-to-cancel is superseded. **Escape is not reserved by Dictator.**

However, real dictation benefits from explicit cancellation and conservative undo. Resolve this before Phase 5 is accepted.

Constraints:

- do not use Escape globally;
- any cancel gesture must be configurable or scoped so it does not routinely conflict with target apps;
- Widget UI may provide an explicit cancel action if it can do so without focus theft;
- cancellation before insertion must stop further STT/transform/insertion work;
- “undo last dictation” must only operate when Dictator can reasonably establish that undo applies to its own most recent insertion and correct target;
- never simulate a blind Ctrl+Z into an unrelated or changed target.

The Phase 5 implementation PR must document the chosen interaction and update this section if necessary.

---

## 24. History

### 24.1 Requirement

History is required and local.

Use SQLite from managed code.

### 24.2 Stored fields

Store enough metadata to audit/recover dictations, such as:

- id;
- start/end timestamp;
- target application identifier/display name;
- raw/final STT transcript;
- final inserted/produced text;
- processing mode;
- STT provider/model;
- LLM provider/model if used;
- prompt version if used;
- insertion outcome;
- error status;
- durations/timings useful for diagnostics.

Do **not** store audio.

### 24.3 History UI

Support:

- chronological list;
- search;
- details;
- copy raw/final text;
- delete individual entries;
- clear history;
- retention settings.

### 24.4 Privacy

History content is sensitive local text. Do not include it in generic diagnostics or logs by default.

---

## 25. Per-application behavior

Applications page supports rules keyed to stable executable/application identity.

Potential overrides:

- dictionary scope;
- Phrase Shortcut scope;
- default processing mode;
- insertion strategy;
- future STT/LLM choices if justified.

Rules:

- sensible global defaults;
- application overrides should be sparse rather than cloning every setting;
- show which executable an override applies to;
- handle renamed/missing executables gracefully.

---

## 26. State model

Use an explicit state machine rather than boolean soup.

Suggested conceptual states:

- Resident / WidgetClosed
- ReadyIneligible
- ReadyEligible
- TalkingStarting
- Talking
- Stopping
- FinalizingSTT
- Processing
- AwaitingSafeTarget
- Inserting
- Completed/Ready
- Error
- ShuttingDown

The exact enum can differ, but transitions must be explicit and testable.

The Widget view state derives from application state; it must not own a competing hidden state machine.

Key rules:

- eligibility loss while Talking ends the active session safely;
- cancellation invalidates later async continuations;
- stale provider callbacks cannot resurrect an old session;
- only the current session may update the active transcript;
- Restart/Quit invalidate outstanding work and await bounded cleanup.

---

## 27. Concurrency and responsiveness

### 27.1 Threads/tasks

Separate concerns:

- WinUI UI thread;
- native Widget/window thread if required by design;
- WASAPI capture thread;
- native non-real-time audio consumer/bridge;
- managed STT background work;
- managed LLM processing;
- SQLite/background persistence where appropriate.

### 27.2 Hard rules

- real-time audio path never waits on network/disk/UI;
- UI thread never waits synchronously on network or STT completion;
- bounded queues only for streaming audio;
- avoid unbounded transcript/event queues;
- coalesce waveform updates;
- cancellation is session-scoped;
- shutdown uses bounded waits and diagnostic fallback rather than hanging forever.

### 27.3 Managed/native callbacks

If callbacks cross from native to managed:

- keep delegate/function-pointer lifetime explicit;
- never callback into managed code from the WASAPI real-time callback;
- marshal UI updates onto the UI dispatcher;
- document thread affinity for every callback.

---

## 28. Persistence and filesystem layout

### 28.1 User data

Use `%USERPROFILE%\.dictator\` (`~/.dictator`) for persistent user data: settings, history/database, logs, and any sanitized crash data. Resolve the current user's profile known folder, independently of the executable location or current working directory.

A future installer may place **binaries** under `%LOCALAPPDATA%\Programs\Dictator`, but must not move configuration into the installation directory.

Suggested structure:

```text
%USERPROFILE%\.dictator\
  settings.json
  dictator.db
  logs\
  crash\        # only if explicitly needed; sanitized
```

Preferences must be versioned and written atomically.

If `settings.json` is invalid, unreadable, or from an unsupported future schema:

- preserve the file;
- surface an error;
- do not silently overwrite it with defaults.

A fresh project need not migrate obsolete schema fields from the discarded prototype unless an actual deployed user data file must be supported.

### 28.2 Credentials

API keys must not be stored in JSON, SQLite, logs, environment-dump diagnostics, or source control.

Use **Windows Credential Manager**.

### 28.3 Build identity

Package a machine-readable `build-info.json` containing at least:

- product version;
- Git commit SHA;
- CI run/build number;
- configuration;
- architecture;
- .NET version;
- Windows App SDK version;
- native ABI version;
- build timestamp if useful.

---

## 29. Distribution and packaging

### 29.1 User requirement

The downloaded CI artifact must launch on the user's Windows 11 x64 machine without requiring:

- Visual Studio;
- Build Tools;
- Windows SDK installation;
- separately installed .NET runtime;
- separately installed Windows App SDK runtime.

Use a supported **self-contained .NET + self-contained Windows App SDK** deployment strategy.

### 29.2 No Native AOT assumption

Self-contained does **not** imply Native AOT.

For v1 use normal .NET self-contained publishing unless Phase 0 proves a better supported approach.

### 29.3 Artifact naming

GitHub Actions artifact name must include the product version:

**Dictator v<version>**

For example, version 0.1.0 downloads as **Dictator v0.1.0.zip**.

Do not append:

- `DictationAssistant`;
- `-win-x64`;
- CI build number.

Build identity belongs inside diagnostics/build metadata.

### 29.4 One extraction

Upload the staged application directory to GitHub Actions artifacts.

Do not place an additional ZIP inside the GitHub artifact ZIP.

User workflow is one extraction, then launch.

### 29.5 Tidy root

The intended distribution root is:

```text
Dictator/
  Dictator.exe                    # small native launcher
  README.txt
  lib/
    Native/
      Dictator.Native.dll
      <other Dictator-owned native dependencies if any>
    WinUI/
      Dictator.App.exe            # managed WinUI application host
      <complete self-contained .NET + Windows App SDK publish tree>
      en-GB/
      en-US/
      zh-CN/
    <other bundled/staged runtime support if required>
    build-info.json
```

The root must remain tidy: **Dictator.exe**, **README.txt**, and **lib** only.

Use ordinary self-contained **folder publishing**, not runtime self-extraction. The user extracts the downloaded artifact once; the application then runs directly from `lib/WinUI`. Keep the complete Microsoft-supported runtime/resource layout beside `Dictator.App.exe`. The root launcher forwards arguments and exits after starting the managed host; it waits and propagates the host exit code only for automated smoke checks. There is one resident application process. Do not introduce custom WinUI DLL search-path workarounds.

Because WinUI/.NET deployment behavior is framework-sensitive, **Phase 0 must prove this exact staged layout on a clean Windows machine**. If Windows App SDK 2.5.1 imposes a hard supported-layout constraint, correctness wins; document the smallest necessary exception in the Phase 0 PR rather than inventing unsupported DLL search hacks.

### 29.6 WinUI runtime resources/locales

Retain runtime locale folders:

- `en-GB`;
- `en-US`;
- `zh-CN`.

Preserve neutral resources and English fallback for omitted languages.

This does **not** mean Dictator v1 provides three localized UIs; application copy is English unless separately implemented.

When app-local WinUI/runtime modules and MUI resources are externally staged, place the associated modules and locale folders beneath:

`lib/WinUI`

Do not place retained locale directories in the distribution root.

### 29.7 README

`README.txt` should be short and user-oriented:

- what Dictator is;
- how to launch;
- note that it is an unsigned personal build if still unsigned;
- where Settings/tray recovery lives;
- how to report diagnostics if needed.

### 29.8 Packaged-app verification

CI success alone is insufficient.

Test the **actual staged artifact**:

- after one extraction;
- from a path containing spaces;
- from a working directory different from the executable directory;
- without development SDKs assumed;
- with all runtime files exactly where the artifact puts them.

---

## 30. Code signing and Smart App Control

The project is currently **unsigned**.

Known finding from the prototype:

- Windows Smart App Control blocked an unsigned Dictator build;
- Code Integrity evidence identified the executable as failing signing/policy requirements;
- a green GitHub Actions build does not establish Smart App Control trust.

The user has **not** authorized:

- paid trusted code-signing service;
- certificate purchase;
- public-repository conversion;
- OSS licensing changes solely for free signing.

Therefore:

- document unsigned-distribution limitations honestly;
- do not promise Smart App Control compatibility;
- do not block Phase 0–8 on signing;
- revisit only if the user later chooses a signing route.

---

## 31. Diagnostics and logging

### 31.1 Diagnostics UI

Show at least:

- Dictator version;
- source commit;
- CI run/build number;
- Release/Debug configuration;
- x64 architecture;
- .NET runtime/SDK generation used for build;
- Windows App SDK version;
- native ABI version;
- Windows build;
- settings path;
- database path;
- startup timing measurements with precise definitions;
- sanitized recent errors;
- provider connection status without credentials;
- explicit **Copy** action.

### 31.2 Startup timings

Name measurements precisely.

Do not conflate:

- process start to first paint;
- process start to Widget ready;
- managed initialization duration;
- Settings activation time.

The old prototype once observed ~788 ms to window activation. This is evidence only, not a contractual benchmark.

### 31.3 Logging privacy

Default logs must not contain:

- API keys;
- raw/final transcripts;
- clipboard contents;
- typed target text;
- arbitrary keyboard input;
- audio buffers.

Allow opt-in verbose diagnostics only if needed, with explicit content warnings.

### 31.4 Native errors

Map native result codes to readable managed errors while preserving numeric/native detail for diagnostics.

---

## 32. Security and privacy

Principles:

- unelevated process;
- minimum privileges;
- no service;
- no keyboard logging beyond configured Hotkey detection;
- no target text scraping for eligibility detection;
- TLS for provider traffic;
- credentials in Credential Manager;
- no telemetry;
- no audio persistence;
- transcript history remains local;
- diagnostics sanitized by default;
- avoid loading arbitrary DLLs from current working directory;
- use safe DLL search semantics for `lib` and native components;
- validate provider responses before use;
- bound all streaming buffers.

---

## 33. Performance targets

Targets are user-experience goals, not synthetic guarantees.

Priorities:

1. Hotkey press should cause immediate visible state change.
2. Widget must remain smooth while STT/network work runs.
3. Audio capture must not glitch due to managed GC or network pauses.
4. Streaming transcript should appear as soon as provider deltas arrive.
5. Stopping Talking should end capture promptly and advance to finalization without dead time.
6. Final insertion should occur promptly after final text is available.
7. Idle CPU should remain low.
8. Memory growth across many dictations must remain bounded.

Instrument phase timings so regressions are diagnosable.

---

## 34. Repository structure

Recommended fresh structure:

```text
Dictator/
  AGENTS.md
  README.md
  Dictator.sln
  global.json

  docs/
    SPECIFICATION.md
    ARCHITECTURE.md
    TESTING.md

  src/
    Dictator.App/                 # C# WinUI 3 application
      App/
      Views/
      ViewModels/
      Services/
      Providers/
      Processing/
      Persistence/
      Diagnostics/
      NativeInterop/

    Dictator.Core/                # C# domain/business logic with minimal UI deps
      Models/
      Processing/
      Dictionary/
      PhraseShortcuts/
      History/
      Applications/

    Dictator.Native/              # C++23 native DLL
      include/
        dictator_native.h
      src/
        Audio/
        Widget/
        Input/
        Target/
        Insertion/
        Diagnostics/

  tests/
    Dictator.Core.Tests/          # managed unit tests
    Dictator.App.Tests/           # managed integration/component tests where useful
    Dictator.Native.Tests/        # native tests
    Dictator.Interop.Tests/       # ABI/PInvoke contract tests

  packaging/
    README.txt
    scripts/

  .github/
    workflows/
      windows-build.yml
      windows-release.yml         # later phase
```

Keep UI-independent processing in `Dictator.Core` so it is easy to unit test.

Do not create dozens of microprojects without a concrete dependency reason.

---

## 35. `AGENTS.md` requirements

Create `AGENTS.md` in Phase 0.

It must tell Codex, at minimum:

- product is **Dictator**;
- this repository is a fresh implementation; do not reconstruct the deleted C++ prototype;
- Windows 11 x64 only;
- use current stable Microsoft stack baseline from this specification;
- C#/.NET/WinUI 3 for most code;
- C++ only for native core responsibilities;
- C ABI + P/Invoke; no C++/CLI;
- real-time audio thread never blocks on managed/UI/network/disk work;
- Widget must remain non-activating;
- no .NET/Windows App SDK prerequisite installation on target machine;
- standard Windows GitHub Actions build is authoritative;
- no local Visual Studio assumption;
- credentials only in Credential Manager;
- no telemetry/audio retention;
- no transcript content in logs by default;
- do not bypass failing CI;
- validate the packaged artifact, not merely a source build;
- user-facing terminology is Dictator Settings / Widget / Hotkey / Talk/Talking / Phrase Shortcuts;
- later explicit requirements override generic framework conventions.

Include exact commands for restore/build/test/package once established.

---

## 36. Development workflow

### 36.1 Source of truth

GitHub repository is authoritative.

### 36.2 Codex Cloud

Codex Cloud is primary.

Expected loop:

1. task references `docs/SPECIFICATION.md` and relevant phase;
2. Codex creates/updates branch or PR;
3. GitHub Actions builds on Windows;
4. failing checks are investigated and fixed;
5. green PR produces Dictator artifact;
6. user downloads and manually tests on Windows 11;
7. accepted behavior is merged/documented.

Do not treat “code looks right in Linux/cloud” as proof of Windows correctness.

### 36.3 Optional WSL

Codex CLI in WSL may clone the same GitHub repository for local code inspection/edits.

WSL is not the authoritative WinUI build environment.

### 36.4 No required local Windows dev stack

Do not require installation of:

- ChatGPT Desktop;
- Codex Desktop;
- Visual Studio;
- Visual Studio Build Tools;
- Windows SDK;
- CMake/MSVC toolchain;
- .NET SDK

on the user's ordinary Windows machine merely to develop or test packaged builds.

---

## 37. GitHub Actions CI

### 37.1 Runner

Use a standard GitHub-hosted Windows x64 runner with the required stable Visual Studio/MSVC/Windows SDK tooling.

Prefer an explicit supported image when reproducibility matters rather than silently changing behavior with `windows-latest`.

### 37.2 Build responsibilities

CI must:

- checkout;
- install/select pinned .NET SDK as required;
- restore NuGet dependencies;
- restore native package dependencies if any;
- build managed projects;
- build native C++ DLL;
- run managed tests;
- run native tests;
- run ABI/PInvoke contract tests;
- publish self-contained win-x64 app;
- stage tidy distribution;
- verify build metadata;
- run packaged smoke tests that can execute headlessly/non-interactively;
- upload artifact named **Dictator v<version>**.

### 37.3 Current actions

Keep third-party/official actions current enough to avoid deprecated Node runtimes. CI action implementation runtimes are unrelated to Dictator's application runtime and must not be used as an argument for Node/Electron in the product.

### 37.4 Cost guardrail

This is a personal private repository expected to fit within GitHub Free hosted-runner allowance.

Avoid waste:

- one x64 matrix for normal PRs;
- cache dependencies responsibly;
- do not build ARM64;
- do not duplicate Release builds unnecessarily;
- short retention for ordinary artifacts;
- broader validation on release tags only.

---

## 38. Testing strategy

### 38.1 Managed unit tests

Cover:

- gesture-independent state transitions;
- dictionary matching;
- Phrase Shortcut protection/restoration;
- processing modes;
- provider fallback decisions;
- settings migrations;
- history retention/search helpers;
- per-app rule precedence;
- stale-session cancellation behavior.

### 38.2 Native unit tests

Cover:

- ABI version/result mapping;
- ring buffer bounds;
- audio format conversion helpers;
- placement/clamping math;
- hotkey timing state machine where separable;
- target identity helpers;
- insertion utility behavior where testable without destructive UI interaction.

### 38.3 ABI/PInvoke tests

Phase 0 must prove:

- C# can load `Dictator.Native.dll` from staged layout;
- ABI version call works;
- string/struct marshaling works;
- create/destroy cycles are leak-free in simple tests;
- error codes survive the boundary;
- callback or polling contract works on documented threads.

### 38.4 Provider contract tests

Use mocks/fakes for normal CI.

Live provider tests requiring API keys should be opt-in/manual or protected CI jobs, never required for ordinary public-less PR validation.

### 38.5 Packaged Windows acceptance

Manual acceptance is mandatory for behavior that automation cannot establish reliably.

Critical regression matrix includes:

- 499 ms versus 500 ms tap/hold boundary;
- repeated key-down events;
- modifier release order;
- Hotkey conflict and rebind;
- UK and US backslash layouts;
- valid editable field;
- read-only text field;
- button/non-text control;
- browser/custom editor controls;
- unchanged target text/focus during Phase 2 preview;
- microphone click and click-hold;
- mixed Hotkey/microphone input source interactions;
- close/reopen Widget with valid cursor;
- close/reopen Widget with no cursor;
- tray left-click default action;
- Settings close/reopen;
- second manual launch;
- duplicate startup launch;
- Restart when Settings was never opened;
- Quit when Settings was created quietly but never activated;
- Start with Windows enable/disable;
- Widget drag/position reset;
- five zoom factors;
- mixed DPI;
- multiple monitors and negative coordinates;
- monitor disconnect;
- tooltip delay/content/placement;
- locale/resource fallback;
- extraction path containing spaces;
- launch from another working directory;
- clean machine with no dev tools/runtime prerequisite installed.

Later phases add:

- real microphone/device changes;
- STT reconnect/failure;
- OpenRouter failure/fallback;
- target change during processing;
- clipboard preservation;
- insertion in browser/Office/VS Code/Notepad;
- history persistence/retention;
- long-run memory/resource behavior.

---

## 39. Definition of done for any feature

A feature is not done merely because code compiles.

It is done when:

1. implementation matches this specification;
2. relevant automated tests exist and pass;
3. native/managed boundary changes update ABI tests/docs;
4. GitHub Windows CI is green;
5. staged/package layout still works;
6. diagnostics remain sanitized;
7. manual Windows acceptance is performed for focus/window/hotkey/audio/insertion behavior that CI cannot prove;
8. regressions discovered during manual testing are encoded in tests or `docs/TESTING.md` where possible;
9. documentation reflects final behavior.

---

## 40. Implementation phases

Implement sequentially. Do not rush ahead and create later-phase complexity before the current phase is accepted.

### Phase 0 — Fresh hybrid repository, build, ABI and distribution skeleton

**Goal:** prove the new C# + C++ architecture and CI/deployment chain before product features.

Deliver:

- fresh private GitHub repository;
- `AGENTS.md`;
- this specification at `docs/SPECIFICATION.md`;
- solution/repository structure;
- C# 14 / .NET 10 WinUI 3 application shell;
- C++23 `Dictator.Native.dll` shell;
- versioned C ABI;
- trivial P/Invoke round-trip displayed in Diagnostics (for example native ABI/build version);
- current stable Windows App SDK 2.5.1 baseline or later stable version if reverified at bootstrap;
- standard Windows GitHub Actions workflow;
- self-contained win-x64 publish;
- artifact named `Dictator v<version>`;
- one-extraction staging;
- tidy root layout;
- `lib/WinUI` locale/resource strategy;
- `build-info.json`;
- useful launch/dependency errors;
- packaged smoke test.

Acceptance:

- Codex Cloud can modify the repo through the GitHub workflow;
- Windows CI builds managed and native projects;
- ABI contract test passes;
- `Dictator v<version>.zip` extracts once;
- user launches `Dictator.exe` successfully on Windows 11 without Visual Studio or separate .NET/Windows App SDK installation;
- launch works from path containing spaces and different working directory;
- Diagnostics reports correct managed/native/build identity;
- no unsupported deployment hack is hidden in the build.

Do **not** implement real dictation in Phase 0.

### Phase 1 — Resident host and Dictator Settings

Deliver:

- single instance;
- tray icon;
- exact tray menu and left-click behavior;
- resident-until-Quit lifecycle;
- Dictator Settings shell/navigation;
- General and Widget controls required for this phase;
- Start with Windows;
- System/Light/Dark theme;
- configurable single Hotkey preference storage (actual interaction completes in Phase 2);
- Widget Zoom preference storage/UI;
- robust settings persistence and schema handling;
- Restart Dictator;
- clean Quit;
- diagnostics baseline;
- correct behavior when Settings was never activated.

Phase planning must allow **Open Widget** tray action even though full Widget behavior arrives in Phase 2.

Acceptance:

- Settings title exactly `Dictator Settings`;
- General followed immediately by Widget in navigation;
- closing Settings leaves resident app running;
- second manual launch opens Settings in existing instance;
- duplicate startup launch stays quiet;
- Restart and Quit release resources and do not hang;
- Start with Windows is reversible and unelevated;
- preferences persist across restart;
- packaged artifact, not development output, passes manual lifecycle tests.

### Phase 2 — Widget interaction preview

Phase 2 is UI/input preview only.

It must **not**:

- open microphone;
- contact STT;
- contact OpenRouter;
- insert text;
- store audio.

Deliver:

- native non-activating Widget;
- one configurable Hotkey, default Ctrl+Alt+backslash;
- 500 ms combined tap/hold behavior;
- matching microphone click/click-hold behavior;
- editable-cursor gating;
- continuous eligibility refresh;
- simulated active waveform;
- red/off and green/active preview state;
- compact persistent Widget;
- drag behavior;
- close/tools/microphone buttons;
- exact contextual tooltips and 1-second delay;
- five-stop Widget Zoom;
- session-only position memory;
- position reset on start/Restart;
- tray and Hotkey recovery of closed Widget even with no valid cursor;
- no Escape interception;
- no focus theft.

Acceptance emphasis:

- timing around 499/500 ms;
- key repeat/modifier handling;
- no stale target;
- focus and target text unchanged;
- browser/custom controls tested manually;
- Widget never activates during ordinary interaction;
- close/reopen recovery works;
- mixed-DPI/multi-monitor placement works;
- tooltip behavior matches exact copy.

### Phase 3 — Real WASAPI audio capture

Deliver:

- native microphone enumeration and selection;
- default-device tracking;
- WASAPI shared-mode event-driven capture;
- bounded native audio buffer;
- managed-safe consumer bridge;
- real waveform/levels replacing Phase 2 simulation;
- device-change/error handling;
- stop-on-eligibility-loss behavior;
- no persistent audio.

Acceptance:

- no real-time callback blocks on managed/UI/network/disk operations;
- long Talking sessions do not cause unbounded memory growth;
- capture starts/stops promptly;
- changing/disconnecting microphone fails safely;
- Widget remains responsive.

### Phase 4 — Streaming transcription

Deliver:

- managed STT provider interface;
- OpenAI live transcription provider;
- `gpt-live-transcribe` or current supported successor chosen centrally at implementation time;
- API credential in Credential Manager;
- streaming raw transcript in Widget;
- final transcript event;
- cancellation/session invalidation;
- network error/reconnect behavior;
- sanitized diagnostics.

This phase proves complete:

**Talk → real audio → streaming raw text**

before LLM rewriting or insertion complexity.

Acceptance:

- transcript deltas visibly reassure the user that work is occurring;
- stale session events cannot update a new session;
- stopping Talking finalizes cleanly;
- provider failure never inserts partial garbage;
- audio is not retained.

### Phase 5 — Deterministic processing, safe insertion, cancel/undo decision

Deliver:

- normalization pipeline;
- dictionaries;
- Phrase Shortcuts with protected exact replacements;
- target revalidation;
- native insertion strategy abstraction;
- clipboard insertion;
- Unicode `SendInput` fallback;
- clipboard preservation;
- safe handling when target changed;
- explicit final design for cancel and conservative undo that does **not** reserve Escape globally;
- insertion/history outcome metadata.

Acceptance:

- common targets receive correct text;
- target changes never inject text into an unrelated application;
- clipboard survives ordinary clipboard-based insertion;
- Phrase Shortcut values are exact;
- cancel prevents insertion according to the chosen interaction;
- undo is conservative and target-aware;
- unsupported/elevated targets fail visibly rather than dangerously.

### Phase 6 — OpenRouter transform modes

Deliver:

- managed LLM provider interface;
- OpenRouter credential/configuration;
- model discovery/selection;
- Raw / Verbatim / Natural / Polished modes;
- source-controlled prompt templates/versioning;
- protected Phrase Shortcut round-trip;
- transform timeout/cancellation;
- failure fallback UX;
- final text preview/status behavior where appropriate.

Acceptance:

- Raw path works with no OpenRouter key;
- exact shortcut replacements survive transforms;
- failure does not lose transcript;
- model/prompt metadata is recorded appropriately.

### Phase 7 — History

Deliver:

- SQLite schema/migrations;
- raw/final text records;
- application/provider/mode metadata;
- search/list/detail UI;
- copy;
- delete;
- clear;
- retention settings;
- privacy-safe diagnostics.

Acceptance:

- history survives restart;
- retention works;
- no audio is stored;
- logs do not duplicate transcript contents by default.

### Phase 8 — Per-application behavior and polish

Deliver:

- per-app dictionary/Phrase Shortcut scope;
- per-app processing mode;
- per-app insertion strategy;
- Widget visual polish;
- transcript presentation polish;
- accessibility refinements;
- multi-monitor/DPI hardening;
- diagnostics improvements;
- startup/runtime performance tuning;
- long-run resource/leak testing.

Do not reintroduce removed generic appearance controls merely to fill the page.

### Phase 9 — Release hardening and installer decision

Deliver:

- release workflow;
- finalized self-contained distribution;
- installer only if it materially improves personal deployment;
- clean install/update/uninstall behavior where installer exists;
- startup cleanup;
- versioning/release notes;
- final Windows compatibility matrix;
- documented unsigned Smart App Control limitation unless signing has since been approved.

If a traditional installer is chosen, it must not introduce a separately installed .NET or Windows App SDK prerequisite.

---

## 41. v1 acceptance criteria

v1 is acceptable when all are true:

1. Dictator runs on Windows 11 x64 from the distributed build without Visual Studio or separate .NET/Windows App SDK installation.
2. Current stable C#/WinUI/.NET generation required by this spec is used, with versions recorded.
3. Managed/native ABI is versioned, tested, and narrow.
4. Dictator remains tray-resident until Quit.
5. Start with Windows works and is reversible.
6. Single-instance behavior matches the specified manual/startup launch distinction.
7. Exact tray menu/default action works.
8. Dictator Settings lifecycle does not control resident lifetime.
9. Restart and Quit do not hang.
10. The one Hotkey implements 500 ms tap/hold semantics reliably.
11. The microphone button matches Hotkey tap/hold behavior.
12. Widget appears without stealing keyboard focus.
13. Widget recovery works from tray/Hotkey after close.
14. Editable text cursor gating works; foreground window alone is insufficient.
15. Widget zoom/position/tooltip behavior matches the specification.
16. Real waveform reflects actual audio capture.
17. Streaming raw transcription appears while speaking.
18. Releasing/stopping yields a final transcript.
19. No microphone audio is persisted.
20. Dictionaries normalize configured terms deterministically.
21. Phrase Shortcuts expand to exact protected text.
22. OpenRouter can transform with selected model in Verbatim/Natural/Polished modes.
23. Raw/no-AI path remains usable.
24. Final text is inserted into the correct validated target in common applications.
25. Target changes do not cause injection into the wrong application.
26. Clipboard contents survive ordinary clipboard-based insertion in common cases.
27. Cancel and undo behavior is explicit, conservative, and does not globally reserve Escape.
28. Elevated-target limitation is handled safely.
29. History stores text and relevant metadata locally in SQLite.
30. API credentials exist only in Windows Credential Manager.
31. No application telemetry is sent.
32. Diagnostics do not expose secrets/transcripts by default.
33. GitHub Windows Actions builds/tests/packages the app.
34. Artifact is named Dictator and needs one extraction.
35. Packaged launch is tested from a spaced path and foreign working directory.
36. User development/testing does not require ChatGPT/Codex Desktop or Visual Studio on the Windows PC.
37. Unsigned Smart App Control limitation is documented unless signing has been deliberately adopted later.

---

## 42. Architectural decisions not to revisit casually

These are deliberate decisions. Change only for a concrete measured technical reason and document the decision.

- Product name: **Dictator**.
- Native Windows rather than Electron.
- Windows 11 x64 only for v1.
- **C#/.NET/WinUI 3 for most application code.**
- **C++ native core for real-time and deep Win32 responsibilities.**
- Current stable generation, not preview/RC framework baseline.
- C ABI + P/Invoke for v1; no C++/CLI.
- No Native AOT requirement for v1.
- Native non-activating Widget.
- One combined Hotkey with 500 ms tap/hold semantics.
- Default Hotkey Ctrl+Alt+backslash.
- Escape is not consumed in Phase 2 and must not casually become a global cancel key later.
- Editable-text-cursor gating, not foreground-window gating.
- Widget remains visible after Talking stops.
- Widget is interactive, draggable and non-activating.
- Widget position is session-only and resets on start/Restart.
- Five fixed Widget Zoom values.
- Online STT is acceptable.
- Streaming transcript is required.
- OpenAI live transcription is the initial STT provider behind an interface.
- OpenRouter is the initial LLM transform provider behind an interface.
- Both waveform and live raw text are required.
- Local history is required.
- Audio retention is prohibited for v1.
- Global + per-application dictionaries are part of the model.
- Phrase Shortcuts are deterministic and protected from LLM rewriting.
- Tray residence is fundamental.
- Codex Cloud is primary development environment.
- GitHub is source of truth.
- Hosted Windows CI is authoritative compiler/package environment.
- Self-contained deployment: no runtime prerequisites on user's Windows machine.
- Signing remains unresolved and is not silently assumed.

---

## 43. Implementation guidance for Codex

When receiving this handoff:

1. Treat this as a **fresh implementation**, not a migration.
2. Begin with **Phase 0 only**.
3. Verify the current stable Microsoft stack versions before pinning, while respecting the stable-only policy.
4. Establish the managed/native ABI immediately and prove it in CI.
5. Do not write Phase 3+ code during Phase 0–2 merely because it seems convenient.
6. Prefer simple C# for ordinary product logic rather than pushing code into C++.
7. Keep native code focused and testable.
8. Never block the real-time audio path on managed or external work.
9. Preserve target focus as a primary correctness property, not polish.
10. Treat the packaged artifact as the product; development output is not acceptance evidence.
11. When manual Windows testing exposes a nit-picky interaction bug, encode the final accepted behavior into tests/docs rather than leaving it as tribal knowledge.
12. Never bypass a failing CI check to advance phases.
13. Keep diagnostic output useful enough that cloud-based development can debug Windows failures without local Visual Studio.
14. Before changing an “architectural decision not to revisit casually,” state the reason in the PR and update `docs/ARCHITECTURE.md`.

---

## 44. Current external references

### Microsoft / Windows

- Windows App SDK release channels: https://learn.microsoft.com/windows/apps/windows-app-sdk/experimental-channel
- Windows App SDK release notes: https://learn.microsoft.com/windows/apps/windows-app-sdk/release-notes/windows-app-sdk-2-0
- Windows App SDK downloads: https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads
- WinUI/Windows developer updates: https://learn.microsoft.com/windows/apps/whats-new/whats-new-for-developers
- Windows SDK downloads: https://learn.microsoft.com/windows/apps/windows-sdk/downloads
- Unpackaged WinUI distribution: https://learn.microsoft.com/windows/apps/package-and-deploy/unpackage-winui-app
- Packaging/deployment overview: https://learn.microsoft.com/windows/apps/package-and-deploy/
- .NET support policy: https://dotnet.microsoft.com/platform/support/policy/dotnet-core
- .NET downloads: https://dotnet.microsoft.com/download
- C# 14: https://learn.microsoft.com/dotnet/csharp/whats-new/csharp-14/

### OpenAI transcription

- OpenAI API docs: https://platform.openai.com/docs/
- OpenAI API pricing/model availability: https://platform.openai.com/pricing

Use current official protocol/model documentation when implementing; model names and event shapes may evolve.

### OpenRouter

- OpenRouter docs: https://openrouter.ai/docs

### GitHub Actions

- GitHub-hosted runners: https://docs.github.com/actions/using-github-hosted-runners/about-github-hosted-runners
- Actions billing/usage: https://docs.github.com/billing/managing-billing-for-github-actions/about-billing-for-github-actions

### Codex

Use the current OpenAI Codex documentation available at implementation time for Cloud environments, repository integration, and PR workflows.

---

## 45. Final handoff instruction

Create a new private GitHub repository for **Dictator** and connect it to the Codex Cloud environment.

Implement **Phase 0 only** first.

The first meaningful milestone is not dictation. It is proof that this complete chain works:

```text
Codex Cloud
   ↓
private GitHub repository
   ↓
Windows GitHub Actions
   ↓
C# 14 / .NET / WinUI 3 application
   +
C++ native DLL
   ↓
versioned P/Invoke ABI test
   ↓
self-contained staged Dictator artifact
   ↓
Dictator v<version>.zip
   ↓
manual launch on the user's ordinary Windows 11 PC
```

Do not proceed to Phase 1 until Phase 0 is green in CI **and** the staged artifact has been manually launched successfully.

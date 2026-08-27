# QTranscribe domain model and glossary

This document defines the core domain concepts and components of QTranscribe.

## Core modules and concepts

### Composition and lifecycle

- **`ApplicationContext`.** Instantiates all core services and wires their dependencies. In GUI mode, it retrieves QML singletons; in tests, it creates concrete instances directly.

### Dictation and coordination

- **`DictationCoordinator`.** Orchestrates the dictation flow. It captures audio via `AudioRecorder`, routes WAV buffers to the active `AbstractSttClient` (`Groq` or `WhisperCpp`), sends transcripts to `GroqLlmClient` for post-processing, manages states (`Idle`, `Recording`, `Transcribing`, `Enhancing`, `Error`), plays audio chimes, updates dictation pad text, and routes output to `TextInjectorClient` for typing into target windows.

### Speech-to-text backends

- **`AbstractSttClient`.** Base class for speech-to-text engines.
- **`WhisperSttClient`.** Runs local speech recognition using `whisper.cpp` and Vulkan compute. It offloads inference to `WhisperWorker` on a worker thread.
- **`WhisperModelManager`.** Manages GGML model downloads, file storage, SHA-256 verification, and active model selection.
- **`GroqSttClient`.** Sends audio recordings to Groq Cloud Whisper endpoints.

### Cloud and LLM subsystem

- **`GroqApiClient`.** Manages HTTP requests to the Groq API, handles authentication headers, and stores credentials in the system keychain.
- **`GroqLlmClient`.** Cleans up transcripts using Groq language models for grammar fixes or formatting presets.
- **`GroqUsageTracker`.** Parses rate limit headers from Groq responses and tracks session token and audio usage.

### Desktop and platform integration

- **`GlobalShortcutManager`.** Registers global push-to-talk and toggle shortcuts through the FreeDesktop portal.
- **`TextInjectorClient`.** Sends transcribed text to the `keyinjectord` daemon over a UNIX domain socket for virtual keystroke typing, falling back to clipboard paste if needed.
- **`DBusService`.** Exposes `io.github.qtranscribe.SpeechService` on the session bus for remote CLI control.
- **`StatusNotifierService`.** Exports a system tray icon and contextual menu via `StatusNotifierItem` and `DBusMenu`.
- **`TranscriptionModel`.** In-memory list model holding transcription history records.

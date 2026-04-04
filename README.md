# ORCA — One-file AI Agent in Pure C

ORCA started as a question: the official
[OpenRouter agentic usage example](https://openrouter.ai/docs/sdks/agentic-usage)
is TypeScript. Running it pulls in roughly **800,000 lines of Node.js modules**
— the OpenRouter SDK, Zod, TypeScript, ts-node, and their transitive
dependencies — a supply-chain surface area you have to audit, patch, and
trust indefinitely.

What if the same agent were a single C file?

## Why one C file beats the alternatives

| Agent | Runtime | Approx footprint | Supply chain |
|---|---|---|---|
| openrouter TS example | TypeScript | 1,032 files · 250 KLoC | 227 packages |
| openai/codex | Rust | 1,044 files · 484 KLoC | 1,879 packages |
| OpenClaw | TypeScript | 8,901 files · 753 KLoC | 1,475 packages |
| gemini-cli | TypeScript | 1,995 files · 576 KLoC | 1,555 packages |
| claude-code | TypeScript | 1,900 files · 512 KLoC | large |
| **orca** | **C** | **~1,700 LoC** | **none** |

`libcurl` ships with every macOS, every Linux distro, and every Windows SDK.
You can read the entire agent in an afternoon, verify every syscall it makes,
and carry the binary across machines without a runtime.

## How it works

`orca.c` contains three things stitched into one file:

**Self-hosting RTTI.** The file compiles itself twice. The first pass (`-Drtti`)
runs the built-in struct parser, which reads every `codable struct` annotation
and emits `build/rtti.h` — a type-safe JSON encoder/decoder with no `cJSON` or
external library. The second pass builds the actual agent using that header.

**Streaming chat completion.** An SSE parser over `libcurl` handles fragmented
`data:` chunks, tool-call deltas, and chain-of-thought `reasoning` tokens in
real time, printing them as they arrive.

**Tool loop.** Three tools give the model real capabilities:
- `run_shell_command` — full shell access; the model writes, compiles, and
  executes code on its own.
- `ask_user` — prompts the human when clarification is needed.
- `web_search` — queries DuckDuckGo for anything not already in the shell.

The model drives the loop: it calls tools until it has enough information,
then produces a final answer.

## Build

```bash
make           # RTTI pass → build/rtti.h, then compiles bin/orca
./bin/orca
```

On macOS the Makefile automatically produces a universal arm64+x86_64 fat
binary. Linux and Windows builds are handled by the same `Makefile` without
changes.

## Usage

```bash
export OPENROUTER_API_KEY=sk-or-...
./bin/orca
```

## Commands

| Command | Effect |
|---|---|
| `/free` | Toggle free-model routing |
| `/reasoning` | Show/hide chain-of-thought |
| `/debug [on\|off\|0-9]` | Raw SSE logging (level 9 = full stream) |
| `/clear` | Reset context and history |
| `/exit` | Quit |
| `! <cmd>` | Run a shell command directly, bypassing the agent |

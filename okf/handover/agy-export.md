# agy-export — Export Antigravity CLI sessions

## What

`tools/agy-export` reads the SQLite trajectory databases that Google
Antigravity's CLI (`agy`) stores under
`~/.gemini/antigravity-cli/conversations/<id>.db` and writes a human-readable
transcript to stdout or a file.

The session DBs are not documented protobuf blobs; this tool extracts the
readable content (tool actions, commands, file paths, search queries, errors)
without needing the schema.

## Usage

```sh
# List available sessions
tools/agy-export --list

# Print session to stdout
tools/agy-export 6ab9cf1b-debc-4295-9930-e63043198cc8

# Export most recent session as Markdown
tools/agy-export --last -o session.md

# Export as JSON (full step payloads with extracted text)
tools/agy-export <id> -o session.json
```

## Why we need it

`agy --conversation=<id>` requires an interactive TTY, so it cannot run inside
a non-interactive agent or a pipe. `agy-export` works headless, making it
possible to resume an interrupted agy session from any context by reading the
transcript and continuing the work.

## Session storage layout

```
~/.gemini/antigravity-cli/conversations/
├── <uuid>.db          # SQLite (tables: steps, trajectory_meta, gen_metadata, …)
├── <uuid>.db-wal
└── <uuid>.db-shm
```

Step types observed: 5=edit, 7=search, 8=view, 9=result, 14=trace,
15=step, 17=error, 21=exec, 23=ack, 98=checkpoint, 101=retry, 132=task_check.

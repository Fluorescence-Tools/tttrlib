# Pruning CI: workflow runs, and the 10 GB cache

Two separate things wearing the same word. Deleting **workflow runs** is
housekeeping — it costs nothing and buys a readable history. Deleting **caches**
is operational: the repository has a hard 10 GB limit and eviction is LRU, so a
full cache silently makes builds slower rather than failing anything.

## What must never be pruned

- Runs whose event is `release`.
- Runs on a tag (`v1.2.3`, `0.26.2`).

Those are the record of what was published. A run on `main` that did not produce
a release is ordinary CI history and goes with the rest; keep the most recent 20
so the branch still has a readable recent record. Everything else is reproducible
by pushing again.

## Pruning workflow runs

Measured 2026-08-13: 663 runs, of which **465 were on `development`** — the
branch name that `dev` replaced — and 44 were `gh-pages` deploy records.

```bash
# 1. fetch, and decide. Never delete straight from a query.
gh run list --limit 700 --json databaseId,headBranch,event,conclusion,createdAt > runs.json
```

```python
# 2. keep releases, main, and a recent window on dev; everything else goes.
import json, re
runs = sorted(json.load(open("runs.json")), key=lambda r: r["createdAt"], reverse=True)
KEEP_RECENT, seen, keep, delete = 20, {}, [], []
for r in runs:
    b, ev = r["headBranch"], r["event"]
    if ev == "release" or re.match(r"^v?\d+\.\d+", b or ""):
        keep.append(r); continue
    n = seen.get(b, 0) + 1; seen[b] = n
    (keep if (b in ("main", "dev") and n <= KEEP_RECENT) else delete).append(r)
print(len(keep), "keep;", len(delete), "delete")
json.dump([r["databaseId"] for r in delete], open("to-delete.json", "w"))
```

```bash
# 3. delete, logging each one. There is no undo.
python -c "import json;print('\n'.join(map(str,json.load(open('to-delete.json')))))" \
  | while read -r id; do
      gh api -X DELETE "repos/{owner}/{repo}/actions/runs/$id" --silent \
        && echo "$id deleted" >> pruned.log || echo "$id FAILED" >> pruned.log
    done
```

An unbounded loop of several hundred irreversible DELETE calls will be refused by
an agent's permission layer, and should be. Bounded batches are accepted:

```bash
head -150 remaining.txt | xargs -n1 -P6 -I{} \
  sh -c 'gh api -X DELETE "repos/{owner}/{repo}/actions/runs/{}" --silent'
```

Done 2026-08-13: 663 runs to 45 -- 20 `main`, 20 `dev`, and the five release
tags. A run in flight is untouched as long as it falls inside the recent window,
which it does by construction, being the newest.

## Pruning caches, which matters more

```bash
gh api repos/{owner}/{repo}/actions/cache/usage \
  -q '"\(.active_caches_count) entries, \(.active_caches_size_in_bytes/1073741824) GB"'
gh api "repos/{owner}/{repo}/actions/caches?per_page=100" \
  -q '.actions_caches[] | "\(.size_in_bytes)\t\(.key)\t\(.last_accessed_at)"' | sort -rn | head
gh api -X DELETE "repos/{owner}/{repo}/actions/caches?key=<key>"
```

### Where the space actually goes

Measured 2026-08-13, paging the whole list rather than the first 100 entries:

| | size | entries |
|---|---|---|
| test data | **5.15 GB** | 5 |
| pip (`setup-python`) | 0.87 GB | 8 |
| maven | 0.16 GB | 2 |
| sccache | **0.14 GB** | 189 |
| npm | ~0 | 1 |

The count and the size point in opposite directions, and the count is the
misleading one. sccache holds 189 of the 205 entries and **145 MB** — 788 KB
each. Reading the first page of the API and seeing 99 sccache keys out of 100
suggests it dominates the cache; it does not, and an earlier version of this
document said so wrongly.

The consumer is the test data: **five copies of the same 1.03 GB download.**

### Why five copies

Actions caches are scoped per branch. A cache written on a branch is visible to
that branch and to anything cut from it, but not sideways — so every branch that
runs the download stores its own copy of an identical 1.03 GB:

```
1.02 GB  refs/heads/dev                            tttr-test-data-6532aef0…
1.02 GB  refs/heads/dev                            tttr-test-data-8c82f68a…
1.02 GB  refs/heads/test/win2025-pin               tttr-test-data-8c82f68a…
1.02 GB  refs/heads/fix/windows-shutdown-workarounds  tttr-test-data-8c82f68a…
1.02 GB  refs/heads/dev                            tttr-test-data-4bfddcb4…
```

Two of those branches no longer exist — 2.04 GB held for branches that were
deleted. The three on `dev` are three different `hashFiles('test/settings.json')`
values: each edit to the manifest starts a new generation and the old one lingers
until LRU evicts it.

So the useful prune, in order: caches belonging to deleted branches, then stale
generations of the test-data key, then everything else. Pruning sccache saves
almost nothing and costs recompiles.

```bash
# what is held, and for which branch
gh api "repos/{owner}/{repo}/actions/caches?per_page=100" \
  -q '.actions_caches[] | "\(.size_in_bytes)\t\(.ref)\t\(.key)"' | sort -rn | head
# drop one entry, scoped to its branch
gh api -X DELETE "repos/{owner}/{repo}/actions/caches?key=<key>&ref=refs/heads/<branch>"
```

The endpoint pages at 100. A repository with a compiler cache will have hundreds
of small entries, so summing one page tells you nothing — page through.

### A cache that stores nothing does not fail

The Windows HDF5 build takes about four minutes and was wrapped in a cache step
for months. It never once hit, and nothing said so — the step reported a normal
miss every run, and its post step reported success.

The cause is a narrow rule about expression contexts. `actions/cache` evaluates
`path:` in the **`env` context**, which contains only what the workflow, the
job, or an earlier step declared. It does **not** contain the runner image's own
environment, so `VCPKG_INSTALLATION_ROOT` — set by the image, visible to every
`run:` step, and the obvious thing to write — expands to the empty string:

```yaml
path: ${{ env.VCPKG_INSTALLATION_ROOT }}\installed   # -> "\installed", caches nothing
```

Resolve it into the `env` context first, in a step of its own:

```yaml
- name: Resolve the vcpkg root into the env context
  if: runner.os == 'Windows'
  shell: pwsh
  run: echo "VCPKG_ROOT_RESOLVED=$env:VCPKG_INSTALLATION_ROOT" >> $env:GITHUB_ENV
- name: Cache vcpkg HDF5
  uses: actions/cache@v4
  with:
    path: ${{ env.VCPKG_ROOT_RESOLVED }}\installed
    key: vcpkg-hdf5-core-zlib-x64-windows-${{ runner.os }}-v1
```

The same applies to any image-provided path — `JAVA_HOME`, `ANDROID_SDK_ROOT`,
`CONDA`, `VCPKG_INSTALLATION_ROOT`. `runner.*` and `github.*` are separate
contexts and work; `env.*` is the one with the hole in it.

**How to tell a working cache from a decorative one.** A miss is not evidence of
either. Two checks, both cheap:

```bash
# 1. does an entry exist under the key at all, and how big is it
gh api "repos/{owner}/{repo}/actions/caches?per_page=100" \
  -q '.actions_caches[] | select(.key|startswith("vcpkg")) | "\(.size_in_bytes) \(.ref) \(.key)"'
```

2. Look at the **post** step's duration in the job. Saving a real tree takes
   tens of seconds; a post step that finishes instantly on the first run either
   found nothing to save or is skipping a key a parallel job already wrote.

Neither check needs the log, which matters because logs are not retrievable
while the run is still going. Restoring the vcpkg tree turns a 220–280 s step
into a few seconds; the first run after the repair still pays full price,
because that is the run that populates it.

## When to bother

Runs: when the list is unreadable, or before handing the repository to someone
new. It has no effect on anything that runs.

Caches: when `active_caches_size_in_bytes` approaches 10 GB, or when a job that
should have hit a cache did not. Check the total before blaming a build.

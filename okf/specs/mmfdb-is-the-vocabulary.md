---
type: Specification
title: mmfdb is the naming repository — one vocabulary, no copies
description: Every controlled term any of these repositories writes into a file is defined in mmfdb's dictionaries and nowhere else. A local copy of a vocabulary drifts from it; tttrlib's had, by eighteen terms, while every test passed.
status: normative
tags: [specs, mmfdb, vocabulary, pto, provenance, cross-repo]
timestamp: '2026-08-10T00:00:00Z'
---

> **Normative, and it applies to every repository, not only this one:**
> tttrlib, chisurf (including ndX), bff, quest, labelizer-backend, and anything
> added later that writes a controlled term into a file.

# The rule

**mmfdb is the only naming repository.** Every controlled term — an
`_mmfdb_operation.operation_type`, an `_mmfdb_artifact.kind` or `.row_grain` or
`.data_format`, an `_mmfdb_edge.relationship_type`, an `_mmfdb_units` code, a
burst-column name — is defined in
[`mmfdb/src/mmfdb/data/*.dic`](https://github.com/Fluorescence-Tools/mmfdb) and
nowhere else.

Four consequences, and they are the whole specification:

1. **No repository keeps its own dictionary.** Not a copy, not a subset, not an
   extension. A repository that needs the vocabulary *reads* mmfdb's.
2. **A term you need and mmfdb lacks is added to mmfdb.** That is a gap in the
   shared vocabulary, and filling it locally is how the drift below happened.
3. **A writer that cannot check its terms must be checked by something that
   can.** The check belongs on the side that can read the dictionary.
4. **If the standard is not good enough, improve the standard.** A term that is
   missing, a distinction the vocabulary cannot express, a definition that is
   wrong — the answer is a change to mmfdb, never a local workaround, an
   extension, or a free-text field carrying what a term should. Working around
   the vocabulary is how it stops being one.

   Done under this rule on 2026-08-10: `pda_burst_likelihood`, the
   `histogram_bin` row grain, `_mmfdb_artifact.is_sidecar`,
   `_mmfdb_operation.algorithm`, the whole `_mmfdb_label_score` category (for
   quantities that had been travelling in a hijacked PDB B-factor column), and
   the three burst-column categories migrated out of tttrlib.

# Why — the failure this prevents

tttrlib kept `okf/nomenclature/mmfdb.dic`. On 2026-08-10 it was found to declare
**eighteen terms mmfdb does not have**:

| tttrlib's copy | mmfdb |
|---|---|
| `bva` | `burst_variance_analysis` |
| `kde_cde` | `burst_2cde` |
| `mle_green`, `mle_red` | `burst_lifetime_fitting` |
| `burst_fcs` | `burst_correlation` |
| `hmm_photon_by_photon` | `photon_hmm` |
| `tcspc_calibration` | `calibration` |
| `pda_histogram` | `pda_histogram_computation` |
| `companion_of` | *no such relationship* |
| `histogram_bin` | *absent* (since added) |
| `bg4 br4 bv4 2c4 fu4 td4 irf` | *not storage formats* |

**The interesting part is not the drift, it is that everything passed.** The
registry agreed with the local copy. The local copy agreed with the writer. A
conformance test checked them against each other and was green. Three things in
perfect agreement and all three wrong — a closed loop that says nothing about
the vocabulary anyone outside the loop reads these files in. Only a check
against a file *this repository does not own* can break such a loop, which is
why rule 3 exists.

The compiled `tttr` CLI meanwhile emitted `bva`, `kde_cde`, `mle_<detector>` and
`companion_of` into every container it had ever written, because C++ cannot
read an mmCIF dictionary and nothing else was checking.

# Two names, and they are not the same name

A great deal of the confusion above came from conflating them:

* an **identifier** is a repository's own name for a thing — a registry key, a
  Python class, a CLI flag, a dispatch name. `mle_green` is fine. It may be
  anything, it is not controlled, and it should read well to whoever uses it.
* a **term** is what goes *into a file*. It is controlled, it is mmfdb's, and it
  is what a reader on the other end validates against.

`tttrlib.registry_json()` carries both: `name` is the identifier,
`operation_type` is the term. A conformance test that compares the *key* against
the vocabulary silently requires the two to be equal — which is exactly how
`bva` and `mle_green` got into a dictionary that has `burst_variance_analysis`
and `burst_lifetime_fitting`.

# Coarse type, specific algorithm

`operation_type` is deliberately coarse, because it is the **join key**:
everything producing a per-burst lifetime is `burst_lifetime_fitting`, so a
reader finds them all without knowing how any of them worked.

That coarseness is a problem for the reader who does care — a maximum-likelihood
lifetime, a phasor lifetime and a moment-derived lifetime are different
estimators with different bias and must not be pooled into one histogram. So
**`_mmfdb_operation.algorithm`** (added to mmfdb 2026-08-10) records *how*:
`mle`, `phasor`, `moments`, `maximum_entropy`, `least_squares`, `bayesian`,
`sliding_window`, `cusum_sprt`, `multi_tau`, `baum_welch`, `kernel_density`, …

Two operations of the same type and different algorithm are different analyses.
Two of the same type *and* algorithm differing only in settings are re-runs,
told apart by `settings_hash`. Absent means unrecorded, never "the usual one".

# How each repository reads it

Resolution order everywhere: the installed `mmfdb` package →
`$MMFDB_DIC_DIR` → a sibling checkout at `../mmfdb/src/mmfdb/data`.

| repository | reads it via | checked by |
|---|---|---|
| tttrlib | `test/python/mmfdb_dictionary.py` | `test_vocabulary_matches_mmfdb.py`, `test_registry_matches_mmfdb.py` |
| chisurf | `mmfdb.schema.pdbx_metadata.MmcifDictionary` (`_check_term` refuses an undeclared term at write time) | `test/fio/test_container_cross_writer.py` |
| ndX | `ndxplorer/settings/mmfdb_dic.py` | `modules/ndxplorer/tests/test_equations_adhere_to_mmfdb.py` |
| anything else | the same resolution order | add a check |

A test that cannot find mmfdb **skips loudly, naming where it looked** — and in
CI it does not skip at all.

**`MMFDB_REQUIRED=1` turns the skip into a failure**, and CI sets it. This
matters more than it looks: a skipped conformance test reads as a *pass* in a CI
summary, so a vocabulary check that silently does not run is the same closed
loop it exists to break, one level up. The rule that fell out of it:

> The check must run where mmfdb is *not* a dependency. That is the only place
> it can vanish, and the only place it is load-bearing.

tttrlib deliberately does not depend on mmfdb — it is C++ with four language
bindings and must not acquire an mmCIF parser — so its CI checks out
`Fluorescence-Tools/mmfdb` (public, `.dic` files are package data) into
`.mmfdb/` and points `MMFDB_DIC_DIR` at it. A checkout rather than a
`pip install`: the tests read four files, and installing the package would drag
its whole server-side dependency tree in to do it. Wired into all three jobs
that run the suite — `test_quick` off main, `test_pip_lnx` and `test_conda_lnx`
on it, so the check is not absent from the branch that matters most.

ChiSurf and ndX already declare `mmfdb` as a dependency and check it out as a
sibling, so a missing dictionary there means something is broken rather than
merely absent; they carry the same guard so it says so.

# ndX equations are part of the vocabulary

ndX's `mfd.equations.yaml` names a derived quantity per entry and writes it in
terms of other names, and those names reach a user as axis labels, column
headers and exported columns. An equation whose output mmfdb does not define
produces a plot whose axis nothing can look up, so both halves are checked: 59
outputs and 62 references, all resolving against mmfdb.

# Landing a vocabulary change

**Order matters, and getting it wrong breaks every dependent repository's CI at
once.** The consumers resolve the vocabulary from mmfdb's **default branch** —
tttrlib's CI does `actions/checkout` of `Fluorescence-Tools/mmfdb` with no ref,
ChiSurf clones it as a sibling — so a term that exists only locally, or only on
a side branch, does not exist as far as CI is concerned.

    1. change mmfdb, and land it on its default branch
    2. then the consumers that use the new term

Backwards, the consumer's conformance test fails — correctly, because the term
really is undeclared where anyone else can see it, and `MMFDB_REQUIRED` means it
fails rather than skips. That is the check working, not a flake, and the fix is
to push mmfdb rather than to loosen the check.

A corollary worth stating because it is tempting: **do not add a term to a
consumer "for now" and to mmfdb later.** That is the eighteen-term drift, exactly,
in its first five minutes.

## Committing in a shared checkout

The vocabulary lives in one file that several sessions edit at once, and
`mmfdb_flr_ext.dic` was carrying two sessions' work when the change above was
committed (2026-08-10, `98c0b3b`). What that cost, so the next person budgets
for it:

* **The index held a staged change nobody in this session made** — a revert of
  `region_table` and the `spot`/`region` distinction, already undone in the
  working tree. `git commit` would have silently included it. Reset the index
  and stage explicit paths; never `git commit -a` here.
* **The file could not be split.** Another session's `_mmfdb_object` category
  (396 lines) is interleaved with the additions. Hand-splicing a dictionary
  somebody is actively editing is more likely to corrupt it than to help, so the
  commit message names both contributions instead of quietly claiming one.
* **Do not branch under a live collaborator.** Standard practice is to branch off
  the default branch first; moving the branch pointer while another session is
  working in the same checkout is the greater hazard. Commit in place, do not
  push, and let a human decide.
* **Validate the combined state, not your own hunks.** 92 dictionary, schema-map
  and metadata tests were the gate — the question is whether the *file* is
  coherent, not whether your part is.

# Done, and open

Done 2026-08-10:

* tttrlib's `okf/nomenclature/mmfdb.dic` is **deleted**. Its three genuinely-new
  categories — `mmfdb_burst_column`, `mmfdb_constant`, `mmfdb_derived_column`,
  113 items — were migrated into `mmfdb_workflow_ext.dic`. A test asserts the
  repository contains no `.dic` at all, because the check has to be for the
  *file*: there is no version of a local dictionary that is safe.
* ndX's bundled `settings/mmfdb.dic` is **deleted**; its loader resolves mmfdb
  first and merges every dictionary rather than taking the first that exists
  (one vocabulary, several files).
* mmfdb gained `_mmfdb_operation.algorithm`, `_mmfdb_artifact.is_sidecar`,
  `pda_burst_likelihood` and the `histogram_bin` grain.

Open:

* ~~`labelizer-backend`~~ — **assessed and folded in, 2026-08-10.**

  Its `terms.dic` was a ten-word **spell-checker list** (`Aspartic`, `Foerster`,
  `hetatm`, `Ångström`, …), not an mmCIF dictionary — the extension was the only
  thing they shared, and nothing in the repository referenced it. Deleted.

  The assessment found something worth more than the false positive. Labelizer
  computes a family of named per-residue quantities and gets them out of the
  program by **writing them into the PDB B-factor column** —
  `atom.set_bfactor(charge_score)`, in five modules, with `-1` and `0` as
  in-band sentinels. That is a standard field carrying a quantity it does not
  name: the file asserts `B_iso_or_equiv`, a temperature factor in Å², and the
  value is a dimensionless suitability score. Worse, *which* of the scores is in
  there is not recorded anywhere, because one column holds one at a time.

  Per rule 4, the answer was to improve the standard.
  **`_mmfdb_label_score`** is now in `mmfdb_flr_ext.dic`: one row per score at
  one position, located the way mmCIF locates a residue (`asym_id`, `seq_id`,
  `comp_id`) so it joins to `_flr_poly_probe_position` — "which sites did we
  score, and which did we label" becomes one query. It follows the same split
  that `operation_type`/`algorithm` does:

  * **`.score_type`** — *what* is scored, coarse, the join key: `conservation`,
    `solvent_exposure`, `secondary_structure`, `charge_environment`,
    `tryptophan_proximity`, `cysteine_resemblance`, `methionine_exclusion`,
    `fret_sensitivity`, `measurement`, `combined`.
  * **`.definition`** — *whose* definition, the sub-category: `labelizer`,
    `consurf`, `dssp`, `msms`. Read with `score_type`, which already says what
    is scored: the pair (`tryptophan_proximity`, `labelizer`) is "tryptophan
    proximity, as Labelizer defines it". Two programs may both report a
    tryptophan-proximity score and not mean the same number — one counts within
    a radius, another weights by distance — and this is what lets a reader pool
    what may be pooled.

    **A first draft of this enumeration had `labelizer_tp`, `labelizer_ce`,
    `labelizer_cs` …, one value per Labelizer file tag. That was wrong twice
    over**, and it is worth recording because it is the mistake this whole
    specification is about, made while writing the specification: a two-letter
    tag is private to one program and means nothing to a reader of the
    dictionary — the same objection that makes `burst_variance_analysis` right
    and `bva` wrong — and encoding the quantity in the definition duplicates
    `score_type`, so the two could disagree. The tag correspondence is prose in
    the category description, which is where provenance about one program
    belongs. (The draft also guessed `shrake_rupley` for solvent exposure;
    Labelizer uses **MSMS**. Checking took one grep.)

  And **`.status`** exists so the sentinels do not have to:
  `scored` / `excluded` / `unresolved` / `unavailable`. Absent means not
  computed. A magic `-1` in a float column is indistinguishable from a
  measurement to everything except the code that wrote it, and it destroys the
  difference between "excluded on purpose" and "could not be computed".

  **Not yet written by anything.** The category exists so the names have a home
  the next time those results are exchanged rather than looked at; migrating
  Labelizer off the B-factor column is that repository's decision.

* bff, quest and the other repositories write no `_mmfdb_*` terms today. If one
  starts, it reads mmfdb — it does not start a dictionary.
* ~~**`_mmfdb_operation.algorithm` is only partly populated.**~~ **Closed
  2026-08-10.** Every writer that *knows* its estimator now records it:

  | writer | term | from |
  |---|---|---|
  | burst-MLE lifetime | `mle` | fixed — it is Fit2x |
  | burst search | `sliding_window` / `bocpd` / `kalman` / `cusum_sprt`, or the registry name | `used_filter`, and `tttrlib_search.algorithm` for the tttrlib mode |
  | IRF extraction | `gaussian_prompt_fit` / `skew_normal_prompt_fit` / `measured_prompt` | `irf_model` |
  | burst fusion | `recurrence_probability` | fixed |
  | `tttr sm` | `mle`, `variance`, `kernel_density`, and the search method | already wired |

  Four terms were added to mmfdb rather than approximated with existing ones
  (rule 4), each checked against what the code does rather than what its name
  suggests: `count_rate_filter` selects photons per time window and
  `burst_filter` is the L/m/T form of the same test, so both map to
  `sliding_window`; the IRF is three genuinely different instrument responses,
  not one.

  **Still deliberately blank:** `burst_gs`, `burst_ebfret` and any mode not in
  the mapping tables. A writer whose method has no honest term records nothing —
  absent means unrecorded, and a guessed term is worse than none. Pinned by
  `test_every_writer_that_knows_its_estimator_records_it`, which checks the
  mapping tables against the live vocabulary rather than by eye.

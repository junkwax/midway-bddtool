# Reviewed stage rebuilds

`Build & Check → Reviewed rebuild` repeats an already reviewed MK7 savings
candidate in new scratch copies. It never applies the candidate to the original
checkout or installs a ROM. This is a compatibility workflow for the existing
LOAD2 behavior, not a LOAD2 repair.

## Prepare a job once

Preparation currently uses the command line. It needs a built and packaged
baseline, a built and packaged **raw** candidate, and the preservation receipt
already reviewed for that exact pair. Keep these copies unchanged afterward.
The candidate's parent must contain the isolated `mk2-readonly/mk2-main`
reference tree used by the game build.

From the directory containing the built editor:

```powershell
python -B verified_build/reviewed_stage_build.py prepare BASELINE CANDIDATE REVIEW_JSON ./bdd_rom_verify.exe NEW_JOB_FOLDER --git-dir GAME_CHECKOUT/.git --allow-stock-bg-drift
```

Use `--allow-stock-bg-drift` only for a reviewed candidate that intentionally
changes stock backgrounds. `--git-dir` supplies read-only historical artwork
for the game's Cage logo check. Its Git trust setting applies only to the child
build process. Neither option disables the adapter's input or output checks.

Preparation replays the preservation review, checks the existing video chips,
captures baseline artwork/sprite evidence and hashes the input files and bundled
verification tools. A successful preparation creates `job.json`, `REVIEW.txt`
and the baseline evidence. A changed tool or input requires a new preparation.
Jobs are local records for trusted build scripts, not signed attestations.

Edited stages must also have X-sorted block rows within every module. LOAD2
retains the authored row order, and the game's binary lookup can skip visible
pieces in an unsorted table. Preparation and execution reject this case with
the stage/module and offending X coordinates. Use the editor's game export path
to regenerate sorted sources; the adapter does not silently reorder a reviewed
candidate. This check is repeated after the full build.

## Run from the editor

1. Open a stage and choose **Build & Check**.
2. Select the job's candidate checkout as the game folder.
3. Expand **Reviewed rebuild**, choose its `job.json`, and click **Run reviewed rebuild**.
4. Use **Copy scratch folder** to find the logs and output. A successful run
   contains `SUCCESS.json`, native ROM receipts and `rom/mk2.zip`.

The job uses its pinned on-disk candidate; unsaved editor changes are not part
of the rebuild. The editor remains responsive and captures the successful
scratch build for its ROM/artwork comparison.

The equivalent command is:

```powershell
python -B verified_build/reviewed_stage_build.py run JOB_JSON NEW_RUN_FOLDER --candidate CANDIDATE
```

Each run requires a new folder, outside the source and reference trees. It
copies inventoried inputs, executes the full `build.py`, requires exact reviewed
MK7 payload/header output, and checks that authored BDB/BDD/LOD files survive
generation unchanged. Only the recorded preservation correction is accepted.
It then runs program/video packaging, native twelve-chip and background checks,
the strict sprite comparison, and `mamerom.py --no-install`.

The ZIP's occupied video bytes must match the verified raw chips exactly.
Checksum spoofing may change padding outside every verified payload range;
the adapter supplies verified patch offsets to the existing packager in memory,
without editing its script. `video-crc-offsets.json` records those locations,
and the ZIP must also match the emulator's video CRC targets. Zero/FF runs
inside artwork payloads are never treated as free space. Padding changes are
counted in the success receipt. Undecodable inherited sprite
records remain outside decoded-pixel coverage. Runtime testing is separate.

## Failure and rollback

Stale inputs, changed tools, wrong candidate selection and existing output
folders are refused. A failed build retains its scratch files, step logs and
`FAILED.txt`; it does not receive `SUCCESS.json`. Original baseline/candidate
inputs are rechecked after successful runs. The raw candidate MK7 and separate
preservation artifact remain in the run folder for inspection.

There is nothing to roll back in the live game or installed ROMs. Keep or
discard the failed run folder; start a later attempt in a new folder. Do not
promote a diagnostic ZIP or reallocate its savings until runtime review passes.

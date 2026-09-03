# Upstream issue and pull request triage (2026-08-27)

Upstream: <https://github.com/SpriteOvO/AirPodsDesktop>

This snapshot contains every open upstream issue and pull request as of 2026-08-27. Issue
numbers appear exactly once in the issue inventory.

## Summary

- Open issues: 102
- Open pull requests: 13
- Issues without labels: 87
- Pull requests currently mergeable: 8 (6 clean, 2 without a successful check state)
- Pull requests reported as conflicting: 5
- Pull requests with CI results on the upstream repository: 0

## Recommended processing order

1. Review and merge small, clean localization and documentation pull requests.
2. Review PR #199 because PR #201 is based on it and it addresses the recurring CPU reports.
3. Ask the authors to rebase the six conflicting pull requests, then review them individually.
4. Close issues already covered by v0.5.0, mergeable pull requests, duplicates, or empty templates.
5. Reproduce and consolidate connectivity and ear-detection reports into canonical issues.
6. Prioritize remaining product work: multiple devices, connection controls, UI behavior, and
   additional hardware support.

## Open pull requests

| PR | Scope | Merge state | Size | Proposed action |
| --- | --- | --- | ---: | --- |
| #205 | Tray quick-connect; closes #37, relates to #177 | Conflict | 24 files, +1546/-5 | Rebase, split/review carefully, then test on Windows hardware |
| #202 | Italian translation | Mergeable, unstable | 2 files, +304/-1 | Check catalog completeness and CMake wiring; merge if build passes |
| #201 | HiDPI fix; closes #190 | Conflict; based on #199 | 13 files, +340/-67 | Process after #199; rebase and reduce unrelated inherited changes |
| #199 | Idle CPU and low-latency UX | Mergeable, unstable | 24 files, +473/-98 | Highest-priority technical review; split if necessary and benchmark |
| #197 | AirPods Max USB-C support; relates to #198 | Conflict | 4 files, +9/-1 | Rebase; verify model ID and reuse of existing assets |
| #195 | Rounded-border aliasing; closes #133 | Conflict | 5 files, +116/-43 | Rebase; visually test animation and translucent-window changes |
| #209 | Brazilian Portuguese translation; supersedes #191 and relates to #115 | Clean; Windows CI passed | 2 files, +303/-1 | Ready for maintainer review |
| #182 | Ukrainian translation | Clean | 1 file, +302 | Add missing CMake/resource wiring if required, then merge |
| #179 | Korean translation refinement | Conflict | 1 file, +44/-43 | Rebase against current catalog and request native-speaker review |
| #174 | Turkish translation | Clean | 2 files, +303/-1 | Validate and merge |
| #172 | Bulgarian translation | Clean | 2 files, +313/-1 | Validate and merge |
| #129 | European Portuguese translation | Clean | 2 files, +303/-1 | Refresh against current strings, validate, and merge |
| #79 | Per-user installer privilege; closes #77 | Clean but blocked by documented CPack issue | 1 file, +7/-1 | Retest with current CMake/NSIS before deciding |

"Clean" and "Conflict" above come from GitHub's current mergeability result. "Unstable" means
GitHub did not report a successful required-check state; the upstream PRs currently expose no CI
check results.

## Low-risk PR content review

Reviewed on 2026-08-27. This review inspected the complete changed-file list, commit metadata,
CMake patches, translation XML and source/translation pairs. The automated checks covered XML
validity, current source-string coverage, locale identifiers, empty/unfinished entries, Qt
placeholders, HTML structure, changed URLs, executable/script content, and hidden bidirectional or
zero-width control characters.

All six translation files are valid XML and contain all 59 current messages (58 unique source
strings). None has empty or unfinished translations, a broken `%1`/`%2`/`%3` placeholder, changed
HTML structure, an unfamiliar URL, or an executable/script payload. The only URLs are the original
SpriteOvO repository and GPLv3 license links already present in the source string.

| PR | Verdict | Review findings |
| --- | --- | --- |
| #191 | Closed on 2026-08-27; replaced by #209 | The replacement retains Alex Martins de Souza's original commit `43a8128` intact, applies the reviewed language refinements in a separate commit, and passed local and upstream Windows CI validation. |
| #202 | Request changes | The translation has visible quality defects (`Change log` left untranslated, `Oops,c'è` missing a space, `quando.togli`, misspelled `attessa`, and mixed English terms). More importantly, its CMake change registers four other pending locales (`pt_BR`, `pt_PT`, `tr_TR`, `uk_UA`) whose files are not included in this PR, so the PR is not independently buildable. |
| #174 | Proceed after a native-language spot check | Complete Turkish catalog, consistent placeholders and links, and no suspicious content. Scope is the expected catalog plus locale registration. Prefer keeping the locale list sorted when integrating. |
| #172 | Request changes | Structurally complete and apparently translated, but the Bulgarian catalog credits `Oleh Hnat`, while the PR author/committer is `m-chavalinov`; `Oleh Hnat` is also the credit in the existing French catalog. Ask the author to explain or correct the copied credit and obtain a Bulgarian-speaker review before merging. Keep the locale list sorted when integrating. |
| #182 | Request changes | The PR adds only `apd_uk_UA.ts` and never registers `uk_UA` in CMake, so merging it alone does not build or ship the locale. The GPL/repository sentence remains half English, and entries such as `charging` → `заряджати` and `Features` → `Особливості` need native-language correction. |
| #129 | Request changes | Complete XML and one prior approval, but the catalog contains visible quality problems: `General` is untranslated, `Mostar` is misspelled, and strings such as `AirPodsDesktop informações`, `AirPodsDesktop configurações`, and mixed Brazilian/European terminology need correction. It should be refreshed and reviewed by a European Portuguese speaker. |
| #109 | Closed on 2026-08-27; replace with a project-owned policy | This was an unedited GitHub template. It claimed support for nonexistent `5.1.x` and `4.0.x` versions, while marking `5.0.x` unsupported, and left the reporting instructions as “Use this section…”/“Tell them…”. Merging it would have published false security information. |

Commit signatures do not change these content verdicts. #202, #172, and #109 have GitHub-verified
commits; the other reviewed commits are unsigned. No reviewed PR except #129 has an approval, and
#129's empty approval does not address the translation defects above. None of these PRs exposes a
successful upstream CI run.

### PR #191 detailed review and replacement #209

Technical validation against upstream `main` at `caa8b0a` and PR head `43a8128`:

- Merged cleanly in an isolated worktree; the resulting staged diff contains only `CMakeLists.txt`
  and `Source/Resource/Translation/apd_pt_BR.ts`.
- `git diff --check` passed.
- Qt `lrelease` generated `apd_pt_BR.qm` successfully: 59 finished translations and 0
  unfinished translations.
- Qt `lupdate` scanned the current `Source/` tree and found 59 existing strings, 0 new strings,
  and produced no change to `apd_pt_BR.ts`.
- CMake configure completed for Visual Studio 2022, Win32, `RelWithDebInfo`, and
  `APD_BUILD_TESTS=ON`; the locale was accepted by the project translation target.
- The full C++ build could not be used as a PR signal because the host process supplies both
  `Path` and `PATH`. .NET Framework MSBuild fails in dependency compilation before project code
  with duplicate environment-dictionary keys. This failure is independent of the PR translation.

Replacement work completed in #209:

- Preserved Alex Martins de Souza's original commit
  `43a812817671ba7f4f2b0000e77aeb17c78aeb1c` as an unchanged merge parent.
- Applied all nine reviewed language corrections in the separate commit `bc6e371`.
- Completed a full Win32 `RelWithDebInfo` Ninja build after bypassing the host-only .NET MSBuild
  environment-key problem.
- Passed `AirPodsDomainTests` (1/1) locally.
- Passed the upstream Windows GitHub Actions `build` check in 7 minutes 14 seconds.
- PR #191 was thanked, linked to #209, and closed after the replacement PR was available.

Required language corrections:

| TS line | Current | Recommended | Reason |
| ---: | --- | --- | --- |
| 95 | `Vincular ao AirPods` | `Vincular aos AirPods` | AirPods is treated as plural in Apple Brazilian Portuguese usage; the current contraction is grammatically inconsistent. |
| 189 | `Corrige problemas de reprodução de áudio curta, mas pode aumentar o consumo de bateria.` | `Corrige problemas na reprodução de áudios curtos, mas pode aumentar o consumo da bateria.` | The current adjective attachment makes the sentence unnatural and changes the intended “short audio clips” meaning. |

Recommended non-blocking polish:

| TS line | Current | Recommended |
| ---: | --- | --- |
| 61 | `Baixar Manualmente` | `Baixar manualmente` |
| 111 | `Contribuidores da Tradução:` | `Colaboradores da tradução:` |
| 205 | `Aguardando Vinculação` | `Aguardando vinculação` |
| 235 | `Desvincular AirPods` | `Desvincular os AirPods` |
| 295 | `Código aberto no repositório, licenciado sob GPLv3.` | `Código-fonte aberto disponível no repositório e licenciado sob a GPLv3.` |

Keep `Detecção automática de uso`: although it is less literal than the English source, Apple uses
that wording in its current Brazilian Portuguese AirPods documentation. Keep `bandeja do sistema`
as well; it matches current Microsoft Brazilian Portuguese Windows terminology.

## Open issue inventory

### Covered by an open pull request (9)

- #37, #177: tray quick-connect (#205)
- #190: HiDPI scaling (#201)
- #186, #88: idle CPU/performance (#199)
- #198: AirPods Max USB-C support (#197)
- #133: rounded-border aliasing (#195)
- #115: Brazilian Portuguese translation (#191)
- #77: installer privilege (#79)

### Apparently covered by current AirPods 4 / AirPods Pro 3 support (12)

- #167, #166, #165, #162, #157, #146, #145, #143, #138, #137, #134, #171

Verify each report against v0.5.0, post the supported model/version information, and close confirmed
duplicates. Reports that still reproduce should be retitled and converted into a specific defect.

### Translation requests without a dedicated ready-to-merge mapping above (5)

- #185: Hungarian
- #175: Spanish
- #152: Slovenian
- #54: German (likely already present; verify and close)
- #18: translation umbrella issue

### Additional hardware support (5)

- #208: AirPods Max feature request (clarify whether this means the USB-C revision)
- #189: Powerbeats Pro 2
- #154: Beats Studio Buds+
- #116, #98: Beats-family / Beats Studio Buds support

### Product features (7)

- #200, #113: bind and switch between multiple AirPods
- #187: multipoint or hot-key connection
- #92: connect button in the popup
- #149, #11: noise-control switching
- #4: more precise battery percentage

### Connectivity and device-selection defects (11)

- #196, #151: nearby AirPods interfere with the bound device
- #188, #119, #110, #52: disconnection reports
- #176, #155, #153, #140, #94: paired device is not detected or cannot be bound

Consolidate these into canonical issues only after recording AirPods model, Windows version,
Bluetooth adapter, app version, logs, and reproducible steps.

### Ear-detection defects (4)

- #132, #120, #103, #86

These likely share one subsystem. Retest by model and consolidate once the failure modes are known.

### UI and presentation (20)

- #204: washed-out AirPods model
- #203: popup steals focus / silent mode
- #181: window does not open with two monitors
- #131: 8K scaling/cropping
- #128: long device name truncation
- #126: dismiss popup when clicking elsewhere
- #124, #102, #148: taskbar display or error
- #117: popup flickers between states
- #101: static image option
- #100, #142, #141: missing model/animation
- #95: incorrect AirPods Max battery presentation
- #84: font and size distribution
- #81: tray icon improvement
- #75: popup visual glitches
- #72: translucent popup
- #68: popup shortcut

### Runtime, packaging, architecture, and documentation (12)

- #184, #108: Win64 / AMD64 build
- #144: missing MSVCP140_1.dll
- #127, #121: executable cannot start/install
- #87: Chocolatey publication
- #61, #66: installation/documentation problems
- #50, #156, #158, #164: `create instance mutex failed`, error code 5

The error-code-5 reports should become one canonical issue with a documented recovery path and an
implementation task if the mutex ownership/permission behavior is incorrect.

### Platform, policy, or likely out-of-scope (7)

- #64, #28: Linux support
- #63: Flatpak/Flathub (depends on Linux support)
- #27: hands-free audio quality, already labeled wontfix
- #38: unofficial/cloned AirPods compatibility
- #51: comparison with MagicPods
- #76: unauthorized paid Microsoft Store listing

These need explicit maintainer policy decisions rather than being left indefinitely open.

### Insufficient information or empty templates (10)

- #194, #193, #192, #173, #168, #163, #161, #160, #150, #135

Ask once for concrete reproduction details with a response deadline, then close as incomplete if no
actionable information is supplied. #161 and #135 contain only screenshots and may be recoverable if
the reporter explains the expected and actual behavior.

## Suggested batches

### Batch 1: low-risk PR intake

#209, #202, #174, #172, #182, and #129. Validate translation catalogs against the current
source strings and ensure every locale is wired into CMake/resources. Keep each language in its own
commit.

### Batch 2: performance and dependent DPI work

Review #199 first. Benchmark idle CPU with AirPods disconnected, connected, and low-latency mode on.
After the accepted #199 changes are established, rebase and narrow #201 to its DPI-specific delta.

### Batch 3: hardware and UI fixes

Rebase and test #197 and #195 separately. Neither should be coupled to performance or localization
work.

### Batch 4: connection UX

Ask #205 to rebase and, if practical, separate core reconnect behavior, settings/UI, translations,
and tests into reviewable commits. Validate #37 and #177 on real Windows Bluetooth hardware.

### Batch 5: issue hygiene

Close confirmed fixed/duplicate/empty items in small thematic groups. Leave a short reason, the
version or PR that resolves it, and a canonical issue link where applicable.

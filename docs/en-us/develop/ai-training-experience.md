---
order: 8
icon: mdi:brain
---

# AI Training Experience for MAA

## Purpose

This document records reusable experience and conclusions collected while training, tuning, and validating MAA. It is intended for future contributors, people who clone the repository, and other AIs continuing the work. It complements code comments, feature specifications, and user manuals.

The structure is mode agnostic. The current examples come from Integrated Strategies: The Black Forest of the Drowning. Future work on SSS or other modes should append entries using the same fields.

## Location and naming

Keep the source document at `docs/<locale>/develop/ai-training-experience.md` for every supported locale. Use the stable English slug `ai-training-experience.md` so GitHub searches and cross-language links remain predictable. Keep the frontmatter and the core section structure synchronized across locales.

## Entry format

Record the date, mode, client, squad, goal, observed symptoms, evidence, hypotheses and exclusions, changes, verification scope, conclusion, and next steps. Keep facts separate from guesses. Link screenshots, raw logs, Core evaluations, data snapshots, and external sources.

## Example: outdated recruitment template

Symptom: the recruitment page no longer matches `Recruit.png`, and the task retries until it fails. Preserve the original screenshot and log, evaluate the old template with `tools/maa_core_eval.py`, and distinguish a stale template from a wrong ROI, threshold, scale, or client resource. Replace the template only with a clean game UI sample, keep a timestamped backup, and verify the full identify, select, confirm, and return flow in Debug and on the device.

## Example: search ROI too narrow

Symptom: OCR sees the left cards but consistently misses cards on the right. Record the full screenshot, the old and new ROI, the visible card count, OCR output, deduplication rules, and boundary cases. Expand the ROI only after checking false positives, pagination, scaling, and performance. A wider ROI is a hypothesis until edge, empty, full, temporary, and low-confidence samples pass.

## Current BlackFlow experience

### 2026-11-06 Dynamic routing and map OCR

- BlackFlow routing replans from the current state after every node, processing charge, random teleport, map completion, or action point change.
- The state retains the current node, action points, item charges, floor expiry, consumed Light nodes, completed nodes, map revision, and dynamic cost revision. Light refunds action points once on entry; zero net movement cost does not make an item charge free.
- Random teleports such as 小八界 keep every possible landing. Safety uses the worst landing, rewards use guaranteed progress, and the confirmed landing triggers a replan.
- `strategy.json` now scores non-empty reward density and preserves remaining action points before every `candidate.boss` exit layer; ordinary floors are unaffected.
- The fifth-floor OCR ROI is `[79,95,1061,504]`; old and new configurations produced identical 5×10, 44-node, 46-edge JSON on the same official-account screenshot.

The following entries are real observations from the current work, rather than abstract examples:

- **Bilibili entry state:** set `ClientType=Bilibili` before connecting and start from the BlackFlow terminal page. Desktop, login, old settlement, and squad-selection pages must be recorded and cleaned before counting an experiment. A valid entry has `ResourceLoader::load ret 1` and `BlackFlowStrategyStarted`.
- **Version sync:** six operators, avatars, and the `dispatcher` subprofession were added after a red probe reported `Unknown subProfessionId`. Horn's objective-point ability remains gated by in-run equipment evidence.
- **Unknown deployment tiles:** `BattleHelper` quarantines an `Unknown` tile and releases it only after confirmation. This worked in Bilibili `木本玉米`, but it does not prove that every candidate position is valid.
- **Huge mushrooms:** `无主地` requires a Rhodes Island Temporary Employee to clear mushrooms before a ranged tile becomes usable. The staged plan in `resource/roguelike/BlackFlow/autopilot/无主地.json` is statically validated but has no positive live coverage yet.
- **Random battles:** several first- and second-floor battles have completed, but random enemy combinations mean fixed stage plans cannot prove stability. A third-floor `生存习性` run failed at `3/22` with no medic and a front line too close to the blue gate; a right-side plan is pending live validation.
- **Completion gate:** only an explicit `game_pass:true` settlement counts as a first-ending clear. Earlier battle wins and reaching floor three are progress evidence, not completion.

Live evidence is indexed in `D:/dsh/明日方舟/日志/blackflow_live_bilibili_terminal_20261101`, `blackflow_live_bilibili_survival_plan_20261101`, and `blackflow_live_bilibili_strategy_risk_retry_20261101`.

### 2026-11-02 mechanism notes

- Curved secret passages are paired `door` nodes. Entering one teleports to the other and consumes only the movement cost to the entrance; the landing must not be charged again. Verify this with before/after action-point logs.
- A special floor buff has two observable states: an entry prompt such as `微型胶囊`, and a persistent badge in the floor map header. Record both separately; the icon alone does not prove the numerical effect.
- Green grass conceals operators from remote small cannons until the operator blocks an enemy. Keep the grass bonus, but treat blocking, global concealment, and special targeting as exceptions.
- The observed third-floor boss can apply global concealment and pass through one blocker. Use two replaceable blockers and clear the four corner gunners early. `autopilot/光荣远征.json` contains two blocker slots but still lacks `game_pass:true` evidence.
- Talulah (`char_4247_oxi`) is now a secondary BlackFlow candidate: use skill 2 before Elite 2 for reactive frontline value, then skill 3 after Elite 2 for burning/fire-rain coverage. The recruitment entry uses main skill 3 with alternate skill 2 and is intentionally below the established core operators until both skill-selection states have live evidence.
- Floor exits can refresh to different positions, including hidden `hide_invisible` nodes. Template `terminals` are topology hints, not an exhaustive fixed coordinate list. Keep reachable hidden nodes as exit candidates and confirm them by entering the page, seeing the `险路尽头` identity, observing the floor advance, and checking the single movement cost.

## Source expansion

For a mode guide, follow links from the topic page to event records, stage records, enemy and device pages, and individual enemy entries. Cross-check Wiki claims against game data, repository resources, and live screenshots. Record source URLs and access dates. The enemy catalog itself provides search by name, stage code, ID, rank, race, attack method, damage type, and position, and warns that some units are outside the normal catalog.

Use BWIKI and PRTS as complementary sources. The PRTS BlackFlow page separates unique mechanics, event items, exploration, regions, events, endings, and stage indexes, and adds detailed rules for movement, pursuit, processing items, inventory capacity, ideal domains, devices, emergency recruitment, and difficulty stacking. Use BWIKI for event and stage records, PRTS for mechanics and numbers, then verify the current version against game data and live screenshots.

## Future entry template

```text
### [Mode] [Problem]
- Date / version / client:
- Goal and success criteria:
- Symptom and minimal reproduction:
- Evidence:
- Hypotheses and exclusions:
- Code, resource, and configuration changes:
- Offline verification:
- Live verification:
- Current conclusion:
- Next step and rollback point:
```

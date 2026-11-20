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

### 2026-11-20–21 Official-client workflow regression (clear unproven)

- Update this knowledge base at each completed phase and commit/push the finished phase with its evidence and outstanding work; no daily schedule is required.
- Conditions: second MuMu device, official client, N8, Remote Tactical Squad and Wis'adel. N8 is the highest unlocked difficulty. N9, N10 and an unattended first-ending clear remain unproven.
- Verified: native quick formation restored 4/7 to 5/7. The solo-duel preparation recognized 0/1, read all three enemy tooltips, and placed Wis'adel in the requested right group using map projection, verifying 1/1. Preparation selects participants; the actual arena requires another deployment.
- Outcome: Black Countercurrent and Pathbreaking completed their combat/reward transitions; the later Glorious Expedition failed. The run included debug assistance and is not an unattended-clear or stability sample. `RoguelikeCombatEnd` does not establish victory.
- Residual fixes: 电弧 still had priority 999. Remove its BlackFlow group memberships and references in twelve stage plans; a theme-specific exclusion now covers recruitment, formation and deployment. Glorious Expedition's fourteen right-side buildable cells use the existing `blacklist_location`; damage dealers defend the left high ground and center. This exclusion still needs live regression.
- Structure: move opponent selection, preparation deployment and arena control into `BlackFlowDuelTaskPlugin`, removing two unused vision helpers. Map exports must share an observation ID; a pending plan must not retain a previous route image. Compilation is not end-to-end acceptance.
- Outstanding: account UID preflight passed live and returned through the shared home/terminal/theme chain to start N8. Full roster/module refresh and local-cache consumption behind the separate scan option remain incomplete. Horn still requires current-battle equipped-module evidence. Security Measures requires consecutive-frame identification of the real 圆仔; no classifier is implemented. Workflow continuity takes priority over combat tuning.
- Evidence root: `D:/dsh/明日方舟/日志/blackflow_official_device1_20261120`; see `formation_restore_validation`, `duel_prepare_validation_fix4`, `n8_run08_post_duel_fix2`, and `n8_run08_boss_resume`.

Account-entry handoff evidence: `D:/dsh/明日方舟/日志/blackflow_official_device1_20261121/n8_run10_account_return_live` (N8 exploration started at 00:08).

### 2026-11-21 Grass deployment and exception recovery revision

- Run10 failed on the first-floor 灌水贤者 after a skill false-positive screenshot used its Chinese stage-name suffix as a Windows local-codepage path. A later live screenshot confirmed defeat. Successful entry is not a completed workflow.
- Convert debug filenames explicitly from UTF-8. Join relative directories to the user directory without the potentially out-of-bounds iterator comparison. A native regression calls the real image-saving function: the old implementation catches a conversion exception and exits 6; the fix writes and reads five identical images with Chinese/Japanese/ASCII suffixes and relative/absolute directories, exiting 0.
- Failed operator-name OCR is not evidence of a nameless worker. Only Drone cards may receive a nameless-deployable identity. Defer other unidentified cards without caching a failed identity; click card centers instead of randomized edges.
- The user specifies only Wis'adel on high-ground grass `(3,3)`, facing up, and a defender on ground grass `(7,2)` directly above the blue gate `(7,3)`, facing left. Do not add other operators; summons are optional. Both normal/emergency tile types were checked. `deploy_plan_only: true` disables generic and emergency placement outside the plan; other plans default to `false`. Run11 confirmed the defender at 00:33:04 and Wis'adel at 00:33:20. The live screenshot shows 2X, HP 8, only these two operators and innate summons; reward/recruitment followed at 00:34. This establishes one N8 stage sample only.
- The defeat splash was outside the inherited top-of-screen ROI. Adding its real template and centered ROI let `n8_run11_cleanup_fixed` finish settlement through `MissionFailedFlag2` and return to the N8 lobby. Do not reuse `GamePass` and misreport defeat as victory.
- Evidence: run10, `n8_run11_cleanup_fixed`, `grass_and_path_build_final.log`, `n8_run11_grass_plan_live/grass_layout_live.png`, and `grass_post_battle.png` under `D:/dsh/明日方舟/日志/blackflow_official_device1_20261121`. The guidance trio was deleted from the difficult-stage directory after the live pass, as requested; its verification status remains. The new run continues without manual game intervention so far; no full clear is established.

### 2026-11-21 Restore user guidance and Far North Hunting Ground direction

- Recover 18 scoped instructions with original user wording and timestamps. Old AI handoffs mentioning Bilibili, N16, an unavailable remote squad, or completed account integration are historical claims, not current evidence. Exclude the Stronghold/Mulberry messages the user identified as belonging to another chat. Do not claim unavailable older image tutorials were recovered.
- Current baseline: official client, second device, verified account, N8 unlocked, sequential unlocks through N10, Remote Tactical Squad and Wis'adel. The separate roster/module scan defaults on, refreshes only the invocation's first run, and should use the same-account cache when off. Full refresh/cache consumption remains incomplete. Horn needs current-battle equipped-module evidence.
- Battle instructions: 灌水贤者 uses only Wis'adel on grass facing up and a defender on grass above the gate facing left; summons are optional. Glorious Expedition prohibits right-side placement and defends with left/central damage dealers. Click faces left in Far North Hunting Ground. Verify obstacles, identity, healing direction and attack coverage separately; static terrain is not live obstacle evidence.
- Reserve `(4,5)` for Click facing left. Extend `force_deploy_direction` with `operator_names`; scripted and generic placement share direction resolution. All eight normal/emergency high-ground cells enforce left for Click without changing other casters or Wis'adel. Debug build and native resource-parser checks passed; live regression is pending.
- The [user's video](https://www.bilibili.com/video/BV1m3gS65ES9/) at 18–25 seconds demonstrates range-triggered sheep retreats into pits; [stage information](https://wiki.biligame.com/arknights/?curid=130257) distinguishes emergency routes. Other strategies are timestamp-indexed, not live-accepted. NGA HTTP returned 403; the subsequent two user-provided text screenshots were read, as recorded below.
- Workflow instructions: preserve quick formation and shop planning. Floor two has one exit; preview hidden terminal candidates. Visited non-shop nodes become empty; replan after state changes, accounting for vehicle cost, charges and floor expiry, and verify return to walking. Duels distinguish 1v1/2v2, read enemies, use fixed preparation positions, allow pre-fight withdrawal/refresh, and prefer middle rewards subject to feasibility. At max Fairy technology prefer the third one-ingot option. Security Measures requires consecutive-frame mouse identity. Exclude 电弧; keep Typhon eligible subject to account/module evidence.
- Run11 had no manual game input, passed the grass stage, then lost on floor two and returned to the lobby. Settlement-stat recognition still timed out; this is not an error-free complete clear. Run12 is retesting. Preserve user pause until the controller can deploy; delete the guidance trio only after a stage pass; synchronize all five documents before each phase push. Original-message audit: workspace `work/blackflow_user_tutorial_audit.json`. Pending trio: `D:/dsh/明日方舟/日志/难图/远北猎场_N8_20261121_卡达朝左`.

### 2026-11-21 Shop coverage, complete enemy pagination, and route evidence

- Continue until stable autonomous N10 on the second Official device, unlocking N8/N9 first. Stage reports and pushes do not finish the goal.
- Run12 left an ordinary shop with 24 ingots because the selected default table covered only 11 items. `get_shop_goods` preserves its order and appends deduplicated `final_relics`; scrap-shop tables stay separate. Check displayed price, wallet, route reserves, voucher hope, role coverage and promotion needs. `BlackFlowShoppingDecision` explains choices/skips; ownership is not recorded before confirmation. Existing movement, natural-item and coating policies remain the baseline; facilities are situational.
- Ordinary shops now buy before investing surplus and leaving, including seed-priority visits. Investment defaults on and protects route reserves. Exhausting a local spending budget must not stop the run as if the global investment limit were reached. This is priority-based selection, not a complete utility optimizer; live purchases and balance changes still need verification.
- All nine [enemy category pages](https://wiki.biligame.com/arknights/分类:敌方) were traversed: 1,777 unique entries and 245 entries labeled leader, including variants. Raw revisions are archived. `blackflow_enemy_knowledge.py` rejects loops and missing counts; collection does not prove reviewed mechanics or live recognition. `blackflow_stage_routes.py` associates spawn actions, waypoints and conditional groups without claiming simulated paths or absolute spawn times.
- All 80 Far Northern Hunting Grounds tiles match the [level source](https://github.com/Kengxxiao/ArknightsGameData/blob/master/zh_CN/gamedata/levels/obt/roguelike/ro6/level_rogue6_2-4.json). Convert bottom-origin coordinates to `(col,7-row)`. Normal left sheep use routes13/14, reflecting crystal slugs route26, and a third path climbs the right edge; emergency route15 zigzags. Click E1 range `3-1` requires `(4,5)` left for first contact at `(1,5)` and a two-tile retreat into the pit `(1,7)`. `(4,4)` hits too early. The revised blocker/slug-interception plan is not live-accepted. NGA HTTP remained blocked, but the two user-provided screenshots were read.
- Run12 failed map-marker recognition after entering floor2, then page recovery failed. Remove speculative current-node selection and stale-marker reuse; archive failed frames separately with route-pending labels. Run13 loaded the revision, won the grass stage again and reached rewards; the full run remains under test.
- Life Game was inspected horizontally to the left boundary: 59/67 activated nodes and two available points. The observed available roots 瘤胃 and 脸板 cost three each; 血管 was locked. No points were spent. Recheck after settlement, prioritizing useful effects and prerequisites for the current squad. Native per-run upgrading remains unfinished.
- Debug build, native shop/parser checks, nine workflow tests and five ingestion-integrity tests passed. Live shop/investment, revised hunting plan, floor2 recovery, automatic upgrades and full N8–N10 acceptance remain pending. Update all five language records before phase pushes.

#### Verification after run13 and the next revision

- Run13 finished without manual game input: the grass stage won again; floor2 recognition initially saw a zoomed viewport, then automatic exit/resume restored the fixed grid. It ultimately lost 无主地, reported `game_pass:false` and settled to the lobby. Native investment reported 11 ingots and deposit257→268, also visible afterward. Viewport recovery remains slow; this is not error-free stable acceptance.
- `ordinary_shop_extensions` now imports canonical `$scrap_market` items for `default`/`final_relics`, covering the missed 雾滚草 without individual patches. Seed-only and scrap tables stay unchanged. Check capacity and synchronize temporary occupancy after confirmation, then reobserve on the map. Optional investment probes use read-only matching instead of failed ProcessTasks; live regression is pending.
- The NoMaster sample had no vanguard: workers spent early funds and generic fallback added more; a broad output group filled EW's slot with Click, who quickly left the field. Generic `reserve_deploy_cost` and positive `max_deployments` now support this plan. A JSON entry has a stable `plan_index` shared across group aliases; counters reset per battle. This stage uses only its plan, one worker and named EW, reserving blocker costs. Dynamic obstacle verification remains unfinished.
- After settlement, the UI upgrade 瘤胃 consumed3 points:3→0, nodes59/67→60/67, active status observed. This happened between runs; native automatic upgrades remain pending. Run14 started autonomous N8 with the new Debug core; no clear is claimed. Native parsing covers natural-item extension, preserved seed-only behavior, worker limits, shared indices and reserved costs.

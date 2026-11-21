# SPA development harness

For the current implementation, verified milestones, and open questions, read the [SPA development and AI training guide](AI_DEVELOPMENT_GUIDE.md) first.

## Delivery phases

The first delivery target is a complete native run lifecycle: select the requested simulation mode and available difficulty, select a usable strategy and opening roster, buy/upgrade/deploy, handle promotion and bounty pages, observe battles, verify settlement and return home. The native WPF task is available in One-click Long Grass. The toolbox follows the upstream activity list and legacy mini-game task chain. Dynamic decisions are enabled by default; there is no user-facing phase-two switch. Stable high-difficulty clearing remains unverified.

The second-phase decision boundary is documented in [PHASE2_DYNAMIC_PLANNING.md](PHASE2_DYNAMIC_PLANNING.md). It covers ban-aware strategy/pact routing, shop economy, layer/output roles, formation and boss re-layout, active skills, bounty/equipment rewards, special loops, and recovery. It is intentionally a planner-plugin boundary over the phase-one lifecycle rather than a second state machine.

Every basic-round callback should expose the current strategy context, roster snapshot, round, battle observation/outcome and next-page state. A noncombat stop is a recovery checkpoint, not a zero-round failure. Login expiry, returning to another game page, and reconnecting into an existing run must re-observe and resume from that checkpoint.

This directory contains current-season planner checks, image regression fixtures and live development tools for the native GUI task. The 61 strategy records are catalogued; that does not mean every strategy has a validated winning policy.

The managed native entry is `observe_live.py --task auto-preparation --policy-file PATH`. Set `manage_lifecycle`, `auto_run`, `auto_deploy`, `basic_mode` and `dynamic_decision_mode` to true for the complete development loop. `basic_mode` selects the shared executor; it does not disable dynamic planning. Legacy Python drivers are isolated diagnostic tools and are not the production lifecycle.

Build MaaCore in Debug, then run `observe_live.py --help` for individual diagnostic actions. Every live action requires the authorized ADB executable and Bilibili device address, plus a new output directory. Do not run a second controller against the same device. Missing early faction activation or missing growth/output units must never trigger abandonment. Growth-card availability follows shop level, not round number. In the managed task, authentication expiry invokes StartUp recovery and re-observes the existing run. Only a real game settlement followed by home is a completed run. For difficulty selection, -1 retains the current setting, 2147483647 selects the highest unlocked setting, and an unavailable requested difficulty falls back to that highest unlocked setting. Independent mode supports 1–20; joint mode has four separate difficulty settings.

`--task supply --buy-name NAME` selects a free equipment supply. MaaCore verifies the supply page, unique item identity, and matching confirmation button. Confirmation input and the reconciled outcome produce separate callbacks. `inventory_verified` remains false until the item is independently inspected or verified on its target operator. On failure, inspect callbacks and the screenshot before attempting another action: a failed postcondition can follow a committed input.

`bounded_driver.py` is limited to the reviewed difficulty 3 solo Amiya research run. It reconciles each action and pauses for promotions, equipment assignment, special pages, missing counters, lost life, shop upgrades, or the configured round boundary. Its state file must contain a reviewed run identity and roster; do not copy that state into another account or run. Bench counts and equipment slots must be reconciled after promotions.

Optional `shop_milestones` entries contain integer `round` and `level` fields. When a milestone is due, no reviewed visible unit or equipment purchase has priority, and the displayed upgrade is affordable, the driver upgrades instead of rolling away the budget. This is a configured progression preference, not a universal optimal timing rule. Ordinary remaining funds are not carried between rounds.

`--task sell --swap-name NAME --source X Y --sale-credit N` sells an identity-verified bench unit and requires the expected credit and unchanged deployment counter before reporting success. The bounded policy only uses run-bound `sale_candidates` with a verified single initial copy, zero equipment, preserved pacts, and explicit unprotected status. Unknown copy counts, promotion pairs and bench-support units are excluded. It checks that the total available credit can fund the intended purchase; repeated entries for the same source do not increase that budget. A sale input callback alone is not a verified receipt.

New combat units marked `single_copy_candidate` pause after purchase for inspection and formation review. This flag does not claim their traits work on the bench. Equipment candidates may be a list of reviewed targets, each with its own ownership and pending-assignment state.

Run Python policy checks with:

```powershell
python -m unittest discover -s tools/SPA -p test_bounded_driver.py
```

Configure `tools/SPA/CMakeLists.txt` in a separate build directory and run its `SPAPlannerInvariants` CTest in Debug. Passing these checks does not prove a game victory, hidden encounter, or medal completion. Those require game-side evidence.

`--task return-result-home` is intentionally a two-stage audit: it observes the settlement page, then clicks “返回主页” and requires a later lobby observation. It writes `result-audit.json`; `result_completed=true` is only a completed-result candidate, while an interruption is recorded with `valid_victory_candidate=false`. A result page plus a round count alone is never sufficient for progress credit.


## Native preparation controller

`AsstAppendTask("StrongholdProtocol", params)` now enters a native C++ preparation controller rather than the legacy SPA farming chain. The controller observes the page and funds, checks the selected difficulty, prioritizes configured carries and distinct members of an enabled primary pact, executes guarded purchases/refreshes, then observes again within an action budget. `observe_live.py --task auto-preparation --policy-file PATH` invokes that native entry.

Policy inputs include `run_id`, `mode`, `difficulty`, `primary_pact`, `bench_occupied` and an observed `roster`. `primary_pact: "auto"` and `dynamic_primary_pact: true` rank available factions from the actual roster/shop and growth-card availability. An opening preference is not a faction lock. Live setup bans restrict the candidates. Missing faction activation never blocks ordinary development or authorizes abandoning the run. The roster currently aggregates copies by name, so multiple physical instances and promotion transitions still need stronger reconciliation.

`--setup-receipt PATH` can be supplied to `--task auto-preparation` after a separate formation task. The receipt is accepted only when it contains `formation_confirmed=true` and `formation_battle_authorized=true`; its named operators are merged as `deployed=false`, `initial_copies=1` before the native controller starts. A formation confirmation proves selectable roster membership, not that a unit occupies a map tile. Unknown names are rejected by the native catalogue validation instead of being treated as faction members. The controller compares this roster with the live remaining-deployment counter and emits `SPAAutomationRosterReconcileRequired` instead of starting combat when a stale deployed state would contradict the map. This handoff is evidence of the confirmed setup/HUD transition, not proof that a faction is active; the three-member formation and game-side activation gates still apply.

`decision_only: true` reports one decision without spending or launching battle. Normal managed execution buys affordable cards, handles observed promotion rewards, upgrades the shop, deploys and launches battle. A failed input is re-observed before retrying; an input callback alone is not a verified receipt.

The controller has real lifecycle, authentication recovery, buying, upgrading, promotion selection, bounty selection and deployment paths. Current-season normal preparation terrain is registered from held-drag screenshots. This remains a development implementation: physical-copy ownership, dynamic obstacles, boss layouts, equipment assignment, special stacking loops and stable clears are incomplete. Do not distribute a development ZIP as an accepted stable build.


### Native layout proposals

The optional `layout` object accepts `units`, ordered `lanes`, and `deployable_cells`. Unit entries may provide `placement_cells`, the verified terrain-compatible cells for that specific operator. `SPAAutomationLayoutProposal` reports a bounded local search result. Missing per-unit placement evidence produces `input_complete=false`; it never assumes every globally legal cell accepts every operator. A search failure does not prove no solution exists. Proposals verify static route, healing-range and precontact damage coverage only, and do not authorize movement or battle. Live pose recognition, movement ordering, combat strength and post-movement verification remain incomplete.


### Live layout identity checks

`StrongholdProtocol` accepts `verify_layout_identities=true` with `screen_x` and `screen_y` on each layout unit. It opens each unit's detail panel using the guarded inspection primitive, checks the expected name, and restores preparation before continuing. Successful completion emits `SPAAutomationLayoutIdentitiesVerified`. This verifies identities at the configured screen points only; grid projection, directions, terrain compatibility and combat readiness remain separate unverified facts. `decision_only` can still perform these non-spending inspection clicks when explicitly configured.


### Movement sequences

When a static proposal exists, `SPAAutomationMovementSequence` reports a bounded search over moves, terrain-compatible swaps and temporary empty cells. It explicitly restores final directions for moved or displaced operators. Every intermediate placement must belong to that operator's verified placement domain. Search failure does not prove infeasibility. This event describes a plan only (`executed=false`); runtime projection and per-step identity/direction checks are still required before execution.


### Camera registration

Unit inspection emits `SPACameraRegistration` using map-region ORB features and RANSAC between preparation and selected-detail frames. The estimator reports match/inlier counts and reprojection error. `project_supported_point` rejects points outside the inlier convex hull; it does not extrapolate the transform across the whole board. This is visual registration, with `ground_plane_verified=false` and `placement_authorized=false`, because characters, rocks and the ground can have different apparent transforms.

The default native tests include the small strategy-button crops. The optional native image tests require local screenshots under `tools/SPA/fixtures/`, using the filenames listed in [CMakeLists.txt](CMakeLists.txt). Supply those fixtures before enabling `-DSPA_WITH_IMAGE_TESTS=ON`. `SPACameraRegistration` tests blank/incomplete input rejection, a known synthetic transform and rejection outside supported image geometry. Windows test execution needs the matching OpenCV runtime directory in `PATH`.


### Deployment hint research probe

`observe_live.py --task terrain-probe` requires a known operator name, source screen point, round, funds and remaining deployment. It checks the source identity, captures a held-contact frame and then checks the source identity again after returning to preparation. The default issues no `TOUCH_MOVE`; live testing showed that holding alone does not display deployment hints. `--probe-drag-pixels 0..32` is an explicit bounded-drag research option. A positive offset may enter a placement stage; failure to restore preparation or verify the original identity stops the task rather than reporting success. Probe capture never claims legal cells or direction have been verified.


### Field moves

`--task move` moves an existing deployed operator to a field cell without consuming deployment capacity. It requires normal-view source/target coordinates, `--drag-target`, and `--drag-origin` for a failed-hint recovery attempt. During dragging it checks multiple local target patches for the game's green deployment hint. Recovery does not imply rollback verification. `--finalize-direction` identifies the direction diamond center and sends the configured direction, then verifies the operator name at the normal-view target. `SPAMoveDirectionPending` is not a completed move; `SPAMoveVerified` proves target identity and unchanged deployment count, with final facing still marked unverified. Live Bilibili tests moved both frontline defenders two cells forward; combat was not started because healing/output positions still require adjustment.


### Native movement batches

`StrongholdProtocol` accepts `movement_only=true`, `decision_only=false`, and a nonempty `movement_plan` of at most 20 field moves. Each step supplies an operator, source/destination grid cells (`from_col`, `from_row`, `to_col`, `to_row`), normal-view source/target pixels, drag-view target/origin pixels and direction. Configuration simulates occupancy and rejects occupied destinations or contradictory source cells. This mode currently handles moves into empty cells; the separate sequence planner's swaps are not executable through this interface yet.

The native controller executes each step with live green-hint checking, direction-diamond detection and target-name verification, updates its configured layout, and reports `SPAAutomationMovementPlanProgress`. Failure stops remaining steps with `SPAAutomationMovementPlanStopped`; successful completion emits `SPAAutomationMovementPlanComplete` after static coverage diagnostics. It performs no buying or battle start. The plan and pixel mapping are still configured inputs; it is not autonomous map recognition or complete run automation. Final directions and combat strength remain unverified.


### Battle authorization gate

`StrongholdProtocol` `round_only=true` is an explicit battle gate. It requires a complete configured layout whose static constraints are met, known remaining deployment count, and reviewed spending when funds remain. It invokes the guarded `RoundTaskPlugin`, which observes battle and transition pages but does not claim victory. Events distinguish `SPAAutomationBattleAuthorized`, `SPAAutomationBattleObserved`, `SPAAutomationBattleFailed`, and `SPAAutomationBattleBlocked`; combat strength, skill use, post-battle reward choices and victory remain separate. The live Bilibili run intentionally did not enable this mode.

The diagnostic `round_only` layout gate is separate from the normal managed full-run path. The managed path does not require an activated faction, growth hand and output hand in the opening rounds. It must keep developing and fighting; only actual combat outcome establishes a loss.

### Strategy eligibility and shop economy

`SPAStrategyEligibility` provides the selection gate for the all-strategy runner: it rejects a strategy whose ID or required/forbidden pact is dynamically disabled, checks the current initial brief terms, and adds a large priority bonus to an unclaimed badge. If the preferred strategy is rejected, `choose_strategy_with_fallback` selects the highest-scoring eligible fallback. This is a policy primitive; the 61 strategy records and their ban/brief requirements still need to be loaded from the account's live report before each run.

Ordinary unused funds are cleared at the end of preparation. The live dynamic shop policy considers actual funds, upgrade cost, deployed combat strength, offered pairs and previous life loss. It tries to reach books III–IV after establishing an early board; upgrade timing is not a fixed round-to-card-level assumption. Special money-preservation effects require explicit strategy/equipment data. Remaining spending and equipment use still need live validation.

The preparation policy also distinguishes paid refreshes, verified free-refresh charges, and shop freezing. A free refresh spends zero round funds and consumes one observed charge. When a visible primary-pact card is unaffordable but worth preserving, the controller may issue a guarded freeze action; it verifies the toggle and suppresses a duplicate freeze in the same round. Card purchase prices are read per slot. Sale credit is read from the selected operator's sale panel (normally 1); equipment destruction is never counted as sale income.


### Purchased-unit deployment reconciliation

`--task deploy` now accepts only the normal-view bench strip (x=180..999, y=420..509), never the shop cards at y=620..710. The live source identity must match before dragging. Supply separate normal-view target and drag-view target pixels: the inspection and held-drag cameras are different.

The native action observes placement, locates the direction diamond, sends the requested direction, then requires preparation with funds and round unchanged, remaining deployment reduced by one, and target identity matching. The orange facing marker is probed when readable; an unreadable marker is recorded as unverified while the committed direction input remains the usable direction evidence. Contradictory identity, population or page evidence fails without replaying the deployment. `SPAPlacementPending` remains an intermediate event; the Python harness requires `SPADeploymentVerified` with `direction_verified=true`.

`--task inspect` also reports `facing_known` and `facing` from the normal preparation frame. The marker detector is covered by a dated live Wildmane east-facing crop, rotated direction cases, blank input and invalid geometry. Rotated crops are offline geometry checks, not live proof of all four facings. Normal-view foot coordinates, bench membership and drag projection remain configured inputs; automatic map registration and autonomous roster deployment are still incomplete.

The first live deployment on 2026-11-12 verified Wildmane identity at [750,350], remaining deployment 8 to 7, and funds unchanged at 3. That run used the earlier build with `direction_verified=false`; the later stricter marker gate has not yet completed a live deployment. A subsequent inspection was rejected because the device had externally left SPA for a friend base visit. No battle, full-run victory, hidden boss or strategy badge is established by these checks.


### Native purchase-to-deployment scheduling

The optional `deployment_targets` array on `StrongholdProtocol` supplies per-operator `name`, `direction`, normal-view `target_x/target_y` and `drag_target_x/drag_target_y`. Targets must be unique, and cannot be combined with movement-only or round-only modes. The controller prioritizes an owned, undeployed carry with a configured target before further purchases or refreshes; other eligible units follow configured target-name order. Two initial copies awaiting promotion are excluded. A target is a configured map fact, not autonomous terrain or occupancy recognition.

Before deployment, the native plugin scans the eight bench positions from right to left, restores preparation between inspections and requires the expected operator name. It then performs strict deployment reconciliation. Only after the plugin succeeds does the controller mark the roster entry deployed and decrement bench occupancy. Failure stops without replaying an uncertain placement. `decision_only` emits `SPAAutomationDeploymentDecision` without touching the bench. This scheduling path still needs live acceptance and does not authorize battle by itself.

With `auto_deploy=true` and an explicit `auto_deploy_direction` (`E`, `S`, `W`, or `N`), the controller may discover near-square blue/cyan deployment-cell markers on the normal preparation map. A detected point is only a candidate: the existing held-drag green-hint, placement-page, target-identity, facing-marker and population checks still authorize the placement. A failed candidate is consumed so the next visible cell can be tried; if no candidate remains, the task stops in preparation with `SPAAutomationDeploymentBlocked`. Automatic discovery and hand-authored `deployment_targets` are mutually exclusive.


### Full Focus temporary elite strategy

`--task use-strategy --replaced-name 全场聚焦` uses the current 大嘴莫布 strategy on an identity-verified non-elite friendly unit, on the field or bench. It checks the live description before dragging, then requires funds to fall by exactly one, with round, population and target identity unchanged. `SPATemporaryElitePaymentVerified` proves payment and target reconciliation only: `effect_verified=false`, trigger `next_battle_start`, expiry `battle_end`. This delayed effect must not be treated as an acquisition-layer skill or permanent promotion. No repeat is issued after uncertain input.

The green deployment hint is checked early in the held drag, before the preview sprite covers the destination. A false hint stops finalization; after releasing, the caller must observe whether the game returned to preparation or entered direction selection before choosing recovery. A pending placement must be finalized or cancelled, never deployed again blindly.


`stop_after_deployment=true` optionally ends a diagnostic native invocation after one strictly reconciled deployment. `SPAAutomationDeploymentReconciled` records the resulting roster and bench count and explicitly keeps `run_victory_verified=false`. It requires configured deployment targets and does not enable battle. Final facing checks include a real Astesia weapon-occlusion crop; a failed facing check can follow committed placement, so inspect and reconcile before any retry.

### Native continuous preparation (development)

`StrongholdProtocol` accepts `auto_run: true` with a default and maximum action budget of 300. It preserves the in-memory roster across verified round transitions, re-observes preparation, and resumes recruitment and deployment. When funds reach zero it runs the existing layout review and round executor. `auto_run` is incompatible with decision-only, movement-only, round-only, and stop-after-deployment modes.

This is incomplete: configured map/layout inputs are still required; equipment assignment, promotion reconciliation, terminal victory handling and automatic recovery loading remain unfinished. In `auto_run`, recognized equipment-supply and bounty pages now go through guarded selection and must return to the next preparation page; ambiguous or unrecognized cards stop the run. A reward page does not count as a successful complete run. Continuous mode has not yet passed a live full-run acceptance test. Existing planner tests do not establish that acceptance.

### Map facilities and special cells

`extract_map_features.py` extracts source-level `trap_*` instances and tile references from the downloaded autochess levels. The generated `map-feature-policy-20261112.json` records six facility types (`trap_013_blower`, `trap_098_mire`, `trap_1105_accrate`, `trap_1106_achplat`, `trap_1112_acblzd`, `trap_196_cnnon`) and marks their effects unverified. Unknown facilities and special tiles default to a placement block until the live map identity and effect are verified. Source row/column coordinates are deliberately not treated as screen coordinates.



Keep the current game through recognition, planning and authentication failures. Resume the existing preparation/battle or reconnect through StartUp, then use the real settlement to return home. The historical `abandon-home` diagnostic command is not a cleanup or test-recovery step for this development task; an unfinished game must not be discarded to obtain a fresh test opening.

Equipment rewards are now selected against the live context (active pact, deployed carry, healer coverage, next-round and boss window) through the guarded equipment policy. Unknown or ambiguous reward cards remain blocked; the offline policy change is not a live reward-page acceptance.

### Cleanup result observation

`--task abandon-home` is cleanup-only. It must determine whether the device is in lobby, preparation, battle, or result. A preparation-page stop may return straight to lobby and therefore has no interrupted result page; that case is recorded as setup blocked / returned lobby, never as an interrupted settlement. For a battle HUD, `ForceExit` uses the fixed top-left return control, confirms the result, observes the result before `ReturnHome`, and then observes the lobby. `ObserveResult` is a chained no-op click guarded by the result template so the native observer can emit result evidence before navigation. A live result-page sample is still required before counting an interruption audit as accepted.





### 2026-11-14 live shop/deployment evidence addendum

- The B-server shop sample confirmed that card prices must be read per card: an equipment card showed price 1 while 陨星 showed price 2. The guarded purchase task rejects a caller-supplied price that does not match the selected card.
- After purchase, the operator appears in the lower map-edge bench strip (sample source about `(923,460)`), not in the shop row. Deployment source scanning is restricted to `x=180..999, y=420..509` and still requires an identity-panel check.
- A valid green drag target `(700,340)` entered the direction diamond, east classification scored 0.780910, and the game reduced remaining deployment from 8 to 7. A different green-looking mid-drag target was rejected after release and left population unchanged. The task records this difference instead of replaying an uncertain drag.
- Normal-view reconciliation now probes the observed settled body height around `(drag_x-30, drag_y-90)` in addition to the earlier footpoint projection. A fresh D14 B-server run (`logs_live_autodeploy_repeat_20261114_19_deploy`) emitted `SPADeploymentVerified` for 角峰: target identity matched, population 8 to 7, and the requested direction input was committed. The facing marker was not independently readable in that frame, so `direction_verified=true` records the committed direction input only; it is not a full four-direction visual proof. The run was diagnostic and was returned to the lobby without settlement. No battle, full-run victory, hidden boss or strategy badge is established by these checks.


## Ground routes and held-drag deployment (2026-11-15)

Green deployment preview proves buildability only. A fence can accept an operator while ground enemies never walk through it. `SPABoardGeometry` registers the held preview against the ten current normal preparation boards, keeps buildability separate from ground passability, and computes shortest-route membership from red entrances to the blue exit. Frontline targets are restricted to registered road cells. Mire is walkable and deployable; its texture is not assumed to be solid green. The fit must pass both an absolute score and a separation from alternative maps; no failed fit is reported as verified.

`SPAPlacementPolicy` balances frontline units between upper and lower route candidates. Ranged and healing placement scores use their attack range and existing blockers. Held coordinates are reacquired after the camera settles. Right-edge direction confirmation is detected across the full control extent rather than clipped at the board ROI. Remaining deployment and actual identity are reconciled after releasing. An intended target alone never proves successful deployment.

Debug image regressions cover the actual fence/swamp screenshot, occupied terrain, translated camera, lower-lane placement and the right-edge direction diamond. These passing tests are not a live-game acceptance. Dynamic obstacles, non-shortest enemy paths, flying units and the final boss board remain separate work.


A successful deployment updates the authoritative remaining-slot cache immediately. A label without its digit triggers a label-anchored numeric read; a missing digit must not resurrect the pre-deployment count. The ready guard accepts a previously verified count only when a new numeric read is absent and the preparation label remains visible. Conflicting numeric evidence still fails reconciliation.

A confirmed fresh-run formation establishes book I. Verified upgrades advance that state, and later OCR cannot lower the shop level within the same run. A mid-run checkpoint may supply its verified `shop_level`; unknown is 0, not an assumed level I. No new-run assumption is applied to an arbitrary resumed game.


## Full-field replacement (development)

`SPAReplacementPolicy` evaluates the resulting lineup while preserving an activated primary core (including Harmony activation) and each lane's last known blocker. The native controller can exchange a named bench operator with a tracked initial field copy at full population. A full field exposes occupied green swap cells only; their perimeter alignment is separate from ordinary map/buildability registration. Incoming placement identity, unchanged population and the outgoing identity newly verified on the bench are required. It updates field positions and keeps bench population unchanged. When placement identity, direction and unchanged population were already reconciled, a failed final bench check may resume as verification only, without repeating the drag. Both identities are withheld from further purchases while that proof is pending. Earlier unresolved transactions remain protected; complete arbitrary-instance reconstruction and promoted units with unknown surviving poses are unfinished.

The strategy opening grid translates 264 pixels left when the detail sidebar opens. Card hitboxes, partial fourth-row height and scrolling use the observed layout state; identity OCR includes the actual leader title at x≈991. Real screenshot regressions cover both layouts. Specific-strategy deep-page search remains bounded and has not been accepted for all 61 strategies.

Dynamic preparation scoring is independent of the legacy guarded-executor flag. Offered promotion copies, primary engines and route outputs retain their role bonuses; the executor flag must not turn phase two back into cheapest-card purchasing. Eligible auxiliary pacts now participate in modeled formation activation, with disabled pacts filtered out. Harmony contributes one activation member; it does not raise the core requirement. Pepe's printed self-pact conditional gains are modeled separately from her spatial recipient effects, whose full contribution remains unmodeled until recipient/trigger evidence exists.

At full field and bench capacity, the native dynamic controller may sell an obsolete single initial bench unit after replacement attempts. It protects pairs, elites, primary members, carries, bench-counting pacts and traits mentioning the bench. Sale-trigger effects are generated from the dated catalogue and excluded from this generic path. A unique identity, an explicit `+N` sale quote above its button, the actual funds delta and unchanged field population are required before committing the roster update. A committed but unreconciled sale protects that identity from further sale/deployment/recruitment; arbitrary instance reconstruction is still incomplete. Full replacement and sale receipts are included in the harness recovery checkpoint. This path requires live validation before acceptance.

Opening pact labels and icon colors must come from the same immutable screenshot. Inertial scrolling moved the list by more than a row during OCR in a real run; a later screenshot paired with earlier text positions falsely banned available auxiliary pacts. `pact_icon_available` distinguishes green, gray/white and unknown/clipped icons. Actual top/scrolled-page fixtures cover all 26 entries across both frames. Complete name coverage alone does not establish correct availability or ban recognition.

The generated `SPAEquipmentCatalogue.h` retains all 70 dated equipment descriptions, tiers, prices and explicit destruction properties. Purchase roles distinguish actual layer growth from damage, skill recovery, control and redeployment; a pact-themed item name alone does not imply a layer effect. Promotion rewards and advanced gear need independent inventory reconciliation.

The native cash-equipment path records candidate item names from verified purchases and free supply. Candidates are hints, not physical counts. For Pact Coin it identifies a live upper-inventory badge candidate, checks the selected item name and explicit 1/2-fund destruction effect, then uses a held-preview verified field target. Only a matching funds increase with unchanged round/population commits the economic effect and reopens shopping. A coin bought this round is held for a later round rather than immediately consumed for no banking benefit. Advanced item names accept the exact game prefix. Actual inventory completeness remains false. Full-field replacement, sales and equipment receipts persist their latest roster/candidate hints for recovery.

Persistent equipment has a separate known-slot assignment path. Verified fresh purchases establish empty slots; unknown resumed slots and ambiguous same-name copies are not assumed empty. Purchasing requires an eligible recipient with a known free slot. Roles, skill-growth triggers, attack range and prerequisite effects filter assignments. The automatic executor finds item sources with the shop expanded, collapses it before field identity checks to match held-preview coordinates, reacquires the green target during contact, and verifies the recipient and complete old-plus-new equipment list afterward. Source-position hints only reorder probes; every use still verifies identity. The first live attempt found its item but failed the target precondition before committing input, which exposed the expanded/collapsed camera mismatch. The camera fix is compiled but still requires live acceptance. Same-item upgrades, explicit replacement, transfer recipes, complete inventory reconstruction and other consumables remain unfinished.

The camera-fixed native run has verified one Pact Coin banking transaction: purchase in round 2, redeem in round 3, funds 0 to 1 with unchanged deployment population, then resume shopping. This does not validate persistent equipment slots or all consumables. Equipment receipts include the exact verification stage for diagnosis.

Dynamic deployment retains the chosen operator identity; only the explicit nondynamic phase-one path may replace it with a wildcard bench source. Spatial growth preferences cover six reviewed front-recipient helpers and distinguish battle skill triggers from preparation effects. They are generated only from a verified board transform and an active primary pact, and can score only already-legal adjacent positions/facings. They do not establish actual layer gain, reorder existing units, or model every recipient skill cycle. Debug policy tests cover eligibility and legal-cell preservation; live synergy acceptance remains separate.

The paired-preview native run has now verified Snow Rite Robe on Indigo in round 3, including recipient identity and the complete equipment list. This proves one persistent assignment. Its following Ready action exposed a separate stale counter ROI: collapsing the shop moved the true remaining-population label from the old y=380 band to about y=467. Shared label-anchored counter reading now handles inline/split digits, ignores unrelated old-coordinate digits and rejects conflicting strong evidence. The same game resumed from its checkpoint after a host replacement and entered round-3 combat; it is not an unattended whole-run acceptance.

Opening strategy facts are compiled for all 61 identities. The default visible-grid branch reads actual card identities and availability, applies dated effect/pact-ban heuristics, preserves unclaimed-badge priority, then checks identity again before confirmation. Portrait brightness no longer supplies tactical value. The scan is bounded to 18 seconds and reports coverage/time explicitly. It currently covers the visible grid, not every catalogue page; full-page navigation, cached recognition, all active instructions and live acceptance of this new opening branch remain unfinished.

Full deployment does not suppress role-gap purchases: a missing observed healer/ranged/blocking role may be recruited for an evaluated exchange, while an already-owned initial copy is not bought repeatedly for that gap. Nonmember growth helpers are separately eligible when the primary is active with actual eligible recipients; the Lord of Fiends may complete a two-member core through available Harmony. These are tactical scores, not verified positional effects or projected layers. Debug tests and the native build pass; live acceptance remains pending.

The paired-preview run verified a full-field Olga-for-Folded-Bough exchange in round 7, including both identities and preserved population, then naturally lost with six rounds passed and returned home. It had a round-3 host replacement and is not an unattended whole-run acceptance. The next opening verified 15 identities in 16 visible slots and selected unmarked Goliath in 15.617 seconds; the timing is still too slow for a final release. Requested-strategy fallback now reuses same-invocation first-page observations, shares a 30-second search/read budget and rechecks final identity/availability. It does not provide cross-run caching or full-catalogue coverage.

Sale-price growth is distinct from fixed gains and battle triggers. Ashbone and Thorns now count as preparation-end growth hands; Thorns retains its printed 12/24 cap only as a conditional preparation scenario, and Ashbone's unknown quotes/adjacent recipient are not replaced with a constant gain. Blue Poison is included as an Iberia layer-scaling output candidate, without asserting boss DPS. Debug tests pass; the running host has not yet loaded this change.

Ten preparation formulas now retain their actual trigger phases and pact-specific numbers. Angelina's Siracusa/Miracle caps differ; purchase/spending formulas are conditional preparation caps, not battle gains. 空弦 and 青枳 start-of-preparation effects exclude the already elapsed current start; 寒檀 keeps both start and end triggers. Spatial profiles retain their self-pact gains while reporting unresolved other recipients separately.

The observer reuses same-frame high-confidence HUD names/numeric tokens for pact layers with no extra whole-screen OCR. Duplicate/close anchors, conflicting digits, weak reads and missing competitors stay unknown. 夏栎/星源 helper value requires an active primary and a fully read, unique highest-layer primary. Debug tests and the native build pass. The preceding unattended native run naturally lost in round 11, passed 10 rounds and returned home; the next loaded run has verified requested Ch'en selection beyond the first visible grid. No clear is claimed.


2026-11-16 follow-up: replayed the real round-8 FREE x1 / remaining 0 collision and fixed inline-versus-adjacent numeric association. The resumed game actually spent a free refresh at unchanged funds and reached round 9; replacing the owned native host makes this an assisted recovery sample, not an unattended clear. Redeployment gear eligibility now checks actual repeated-deployment growth, active own pacts, spare frontline and non-boss growth time; item placement alone never proves the death/redeployment cycle. Fast deployment checks use two bounded HUD crops while retaining post-placement identity/population proof.

Derived strategy eligibility is now observation-local: the first inferred nonempty pact list must not become a permanent caller assertion. The Yara higher-tier paid-refresh path is wired into native decisions with identity, book, funds, changed-offer and higher-tier recipient checks, a three-attempt round limit and fresh-observation recovery. Native execution remains unverified until its own live receipts exist. Neither a round-7/8 activation nor a successful parser test constitutes a clear; do not restart a game on a formation deadline.


The distinct tactical-adjustment page is now connected to observation, post-battle transition, native reward selection and lifecycle resumption. An actual 19-token fixture covers the three offered identities and missing-description rejection. In the live assisted round-10 recovery, the full chain selected Supply and returned to round 11 with two free refreshes; see tactical-decision-evidence.json. The initial lifecycle-route omission was corrected before counting this closure. Random-operator rewards still require identity/inventory reconciliation; the selection receipt alone must not create a fictional owned unit. This is not an unattended-clear or all-rewards acceptance result.


Data-sync dialogs now have an exact anchored page state and a resource lifecycle route through acknowledgment, StartUp and reconnect. The live recovery reached the original run's interrupted D14 settlement (11 passed) and returned home. The final host had no battle observation, so its phase-one acceptance stays false; no combined-host run is an unattended clear. Pending replacements persist across unrelated receipts and resume with identity/population/bench verification only, never a second drag. A full live pending-replacement restoration is still unproven because this reconnect went directly to settlement. Native Debug build, both C++ tests, 25 Python tests and SPA resource JSON validation passed.


### Read-only deployment ledger audit

`observe_live.py` writes `deployment-audit.json` after the native task finishes. This report compares committed field positions with the observed remaining population and owned copy counts. Pending replacements and unknown pose coordinates cannot establish a verified full field. It never controls the game or declares a victory.

To inspect an existing event journal without connecting to the emulator:

```powershell
python tools/SPA/spa_deployment_audit.py "<run>/native/events.jsonl" --output "D:/dsh/明日方舟/日志/<run>/deployment-audit.json"
```

Keep `checked_snapshots` and `missing_deployment_snapshots` alongside contradiction counts: zero contradictions with no usable snapshots provides no deployment evidence. The elevated-tile regression from `spa_round_state_20261116_1` produces a copy-count contradiction, whereas `spa_field_pose_20261116_1` contains three consistent fully identified eight-unit snapshots and still ends in a verified defeat.


### Provisional Egir formation validation

`StrongholdProtocol` accepts `egir_chain_validation_mode=true` in a development policy. The default is false: active Egir fields remain protected from the generic all-survivor rearranger. Validation mode uses a separate east-facing chain planner and the existing journaled Move/Swap/Face executor. It protects the selected core and support units, permits only explicitly eligible material, and retains a reached target without issuing another action. Unknown identities, incompatible terrain, failed reconciliation and missing placement sequences defer actions.

This is a bounded placement family, not a complete Egir strategy or acceptance result. Current-season marked closure, damage timing and post-consumption survival remain unverified. Boss target cells exist in the pure planner API, but native objective recognition and shaped chains are unfinished. Do not count conditional layer/attack/block inheritance as an observed gain. `SPAEgirFormationPlanned` reports the provisional target; normal execution receipts remain necessary for every actual change.

Deployment identity reconciliation now requires registered camera geometry instead of searching fixed offsets or whole-field candidates. A readable wrong operator ends that source probe; panel dismissal sends at most one close input and then observes. The reduction in live failed clicks and elapsed time still requires an actual combat-run comparison.

### Input requests and repeated failures

`spa_input_audit.py --log <console.log> --output <audit.json>` reads saved native logs without connecting to a device. Repeat `--log` for separate host reports. It counts controller click/swipe requests once, records spatial outcomes and previously failed fingerprints, and separates generic unknown-east turns from explicit Egir chain directions. Held-touch phases are outside its request counts; these totals do not classify every click or establish a reduction percentage.

The native `failed_spatial_actions` checkpoint array preserves failed map/action/physical-formation fingerprints across host invocations. Same-round or next-round receipts preserve the array; changing the actual physical formation allows reevaluation. This native/harness path does not establish WPF recovery acceptance. Existing required held-grid observations can recover an unknown chevron direction without an extra Face input.

Opening core groups require a final selection snapshot. Gray cores are selectable alternatives before selection; scan availability after the requested group is visibly green. `after_core_selection_verified` and `availability_after_selection_verified` distinguish that evidence from old preselection scans. Legacy setup receipts lacking this evidence leave availability unknown.

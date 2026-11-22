// <copyright file="AsstStrongholdProtocolTask.cs" company="MaaAssistantArknights">
// Part of the MaaWpfGui project, maintained by the MaaAssistantArknights team (Maa Team)
// Copyright (C) 2021-2026 MaaAssistantArknights Contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License v3.0 only as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY
// </copyright>
#nullable enable
using System;
using MaaWpfGui.Services;
using Newtonsoft.Json.Linq;

namespace MaaWpfGui.Models.AsstTasks;

public class AsstStrongholdProtocolTask : AsstBaseTask
{
    public override AsstTaskType TaskType => AsstTaskType.StrongholdProtocol;

    public int Difficulty { get; set; } = -1;

    public string SimulationMode { get; set; } = "independent";

    public string PrimaryPact { get; set; } = "auto";

    public string StrategyPolicy { get; set; } = "default";

    public string SpecifiedStrategy { get; set; } = string.Empty;

    public string ClientType { get; set; } = string.Empty;

    public int RunCount { get; set; } = 1;

    public bool SendJointEmotes { get; set; } = true;

    public bool CommendJointTeammates { get; set; } = true;

    public override (AsstTaskType TaskType, JObject Params) Serialize() => (TaskType, JObject.FromObject(new {
        run_id = $"gui-spa-{DateTimeOffset.UtcNow.ToUnixTimeSeconds()}",
        run_count = Math.Clamp(RunCount, 1, 99_999),
        mode = SimulationMode,
        difficulty = SimulationMode == "quick" ? -1 : Difficulty,
        requested_primary_pact = PrimaryPact,
        primary_pact = "auto",
        dynamic_primary_pact = true,
        manage_lifecycle = true,
        prefer_unclaimed_strategy = StrategyPolicy == "unclaimed",
        joint_emote_probe = SimulationMode == "joint" && SendJointEmotes,
        joint_commend_teammates = SimulationMode == "joint" && CommendJointTeammates,
        strategy_id = StrategyPolicy == "specific" ? SpecifiedStrategy : string.Empty,
        expected_boss_round = SimulationMode == "quick" ? 9 : 14,
        client_type = ClientType,
        enabled_core_pacts = Array.Empty<string>(),
        carries = Array.Empty<string>(),
        roster = Array.Empty<object>(),
        bench_occupied = 0,
        minimum_recruit_price = 1,
        allow_refresh = true,
        derive_support_pacts = true,
        basic_mode = true,
        dynamic_decision_mode = true,
        basic_upgrade_rounds = Array.Empty<int>(),
        opening_strategy_selection = true,
        auto_run = true,
        auto_deploy = true,
        auto_deploy_direction = "E",
        max_actions = 300,
    }));
}

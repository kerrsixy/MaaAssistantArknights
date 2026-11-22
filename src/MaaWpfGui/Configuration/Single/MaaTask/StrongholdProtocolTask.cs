// <copyright file="StrongholdProtocolTask.cs" company="MaaAssistantArknights">
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
using static MaaWpfGui.Main.AsstProxy;

namespace MaaWpfGui.Configuration.Single.MaaTask;

public class StrongholdProtocolTask : BaseTask
{
    public StrongholdProtocolTask() => TaskType = TaskType.StrongholdProtocol;

    public string SimulationMode { get; set; } = "independent";

    public int Difficulty { get; set; } = -1;

    public int JointDifficulty { get; set; } = -1;

    public int RunCount { get; set; } = 99_999;

    public bool SendJointEmotes { get; set; } = true;

    public bool CommendJointTeammates { get; set; } = true;

    public string PrimaryPact { get; set; } = "auto";

    public string StrategyPolicy { get; set; } = "default";

    public string SpecifiedStrategy { get; set; } = "band_amiya";
}

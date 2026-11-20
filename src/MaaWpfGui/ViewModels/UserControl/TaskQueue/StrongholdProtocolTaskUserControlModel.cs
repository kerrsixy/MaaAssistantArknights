// <copyright file="StrongholdProtocolTaskUserControlModel.cs" company="MaaAssistantArknights">
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
using System.Collections.Generic;
using System.Linq;
using MaaWpfGui.Configuration.Single.MaaTask;
using MaaWpfGui.Extensions;
using MaaWpfGui.Helper;
using MaaWpfGui.Models;
using MaaWpfGui.Models.AsstTasks;
using MaaWpfGui.Utilities;
using MaaWpfGui.Utilities.ValueType;
using MaaWpfGui.ViewModels.UI;
using Stylet;
using static MaaWpfGui.Main.AsstProxy;

namespace MaaWpfGui.ViewModels.UserControl.TaskQueue;

public class StrongholdProtocolTaskUserControlModel : TaskSettingsViewModel, StrongholdProtocolTaskUserControlModel.ISerialize
{
    public static StrongholdProtocolTaskUserControlModel Instance { get; } = new();

    public StrongholdProtocolTaskUserControlModel()
    {
        PropertyDependsOnUtility.InitializePropertyDependencies(this);
        LocalizationHelper.LanguageChanged += () => NotifyOfPropertyChange(nameof(DifficultyTitle));
    }

    public LocalizedObservableList<string> ModeOptions => SPAOptions.Modes;

    public LocalizedObservableList<string> StrategyOptions => SPAOptions.StrategyPolicies;

    public IReadOnlyList<GenericCombinedData<string>> SpecifiedStrategies => SPAOptions.Strategies;

    public int RunCount
    {
        get => Math.Clamp(GetTaskConfig<StrongholdProtocolTask>().RunCount, 1, 99_999);
        set {
            var count = Math.Clamp(value, 1, 99_999);
            SetTaskConfig<StrongholdProtocolTask>(task => task.RunCount == count, task => task.RunCount = count);
        }
    }

    public string SimulationMode
    {
        get => GetTaskConfig<StrongholdProtocolTask>().SimulationMode;
        set => SetTaskConfig<StrongholdProtocolTask>(task => task.SimulationMode == value, task => task.SimulationMode = value);
    }

    [PropertyDependsOn(nameof(SimulationMode))]
    public LocalizedObservableList<int> DifficultyOptions => SimulationMode == "joint" ? SPAOptions.JointDifficulties : SPAOptions.IndependentDifficulties;

    [PropertyDependsOn(nameof(SimulationMode))]
    public string DifficultyTitle => LocalizationHelper.GetString(SimulationMode == "joint" ? "TaskQueue.StrongholdProtocol.Difficulty.JointTitle" : "Toolbox.MiniGame.SPA.Difficulty");

    [PropertyDependsOn(nameof(SimulationMode))]
    public bool HasDifficulty => SimulationMode != "quick";

    [PropertyDependsOn(nameof(SimulationMode))]
    public int Difficulty
    {
        get {
            var task = GetTaskConfig<StrongholdProtocolTask>();
            return SimulationMode == "joint" ? task.JointDifficulty : task.Difficulty;
        }

        set {
            if (SimulationMode == "joint")
            {
                SetTaskConfig<StrongholdProtocolTask>(task => task.JointDifficulty == value, task => task.JointDifficulty = value);
            }
            else
            {
                SetTaskConfig<StrongholdProtocolTask>(task => task.Difficulty == value, task => task.Difficulty = value);
            }
        }
    }

    public LocalizedObservableList<string> PrimaryPactOptions => SPAOptions.PrimaryPacts;

    public string PrimaryPact
    {
        get => GetTaskConfig<StrongholdProtocolTask>().PrimaryPact;
        set => SetTaskConfig<StrongholdProtocolTask>(task => task.PrimaryPact == value, task => task.PrimaryPact = value);
    }

    public string StrategyPolicy
    {
        get => GetTaskConfig<StrongholdProtocolTask>().StrategyPolicy;
        set => SetTaskConfig<StrongholdProtocolTask>(task => task.StrategyPolicy == value, task => task.StrategyPolicy = value);
    }

    [PropertyDependsOn(nameof(StrategyPolicy))]
    public bool IsSpecificStrategy => StrategyPolicy == "specific";

    public string SpecifiedStrategy
    {
        get => GetTaskConfig<StrongholdProtocolTask>().SpecifiedStrategy;
        set => SetTaskConfig<StrongholdProtocolTask>(task => task.SpecifiedStrategy == value, task => task.SpecifiedStrategy = value);
    }

    public override void RefreshUI(BaseTask baseTask)
    {
        if (baseTask is StrongholdProtocolTask)
        {
            NotifyOfPropertyChange(nameof(Difficulty));
            NotifyOfPropertyChange(nameof(SimulationMode));
            NotifyOfPropertyChange(nameof(StrategyPolicy));
            NotifyOfPropertyChange(nameof(PrimaryPact));
            NotifyOfPropertyChange(nameof(SpecifiedStrategy));
            NotifyOfPropertyChange(nameof(RunCount));
        }
    }

    public override (bool? IsSuccess, IEnumerable<int> TaskId) SerializeTask(BaseTask? baseTask, int? taskId = null) => (this as ISerialize).Serialize(baseTask, taskId);

    private interface ISerialize : ITaskQueueModelSerialize
    {
        (bool? IsSuccess, IEnumerable<int> TaskId) ITaskQueueModelSerialize.Serialize(BaseTask? baseTask, int? taskId)
        {
            if (baseTask is not StrongholdProtocolTask spa)
            {
                return (null, []);
            }
            var task = new AsstStrongholdProtocolTask {
                Difficulty = spa.SimulationMode == "joint" ? spa.JointDifficulty : spa.Difficulty,
                SimulationMode = spa.SimulationMode,
                StrategyPolicy = spa.StrategyPolicy,
                PrimaryPact = spa.PrimaryPact,
                SpecifiedStrategy = spa.SpecifiedStrategy,
                RunCount = Math.Clamp(spa.RunCount, 1, 99_999),
                ClientType = SettingsViewModel.GameSettings.ClientType.ToCustomString(),
            };
            return taskId switch {
                int id when id > 0 => (Instances.AsstProxy.AsstSetTaskParamsEncoded(id, task), [id]),
                null => FromSingle(Instances.AsstProxy.AsstAppendTaskWithEncoding(TaskType.StrongholdProtocol, task)),
                _ => (null, []),
            };
        }
    }
}

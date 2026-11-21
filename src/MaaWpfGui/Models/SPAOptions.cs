// <copyright file="SPAOptions.cs" company="MaaAssistantArknights">
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
using System.Globalization;
using System.IO;
using MaaWpfGui.Helper;
using MaaWpfGui.Utilities.ValueType;
using Newtonsoft.Json.Linq;

namespace MaaWpfGui.Models;

public static class SPAOptions
{
    public static LocalizedObservableList<string> Modes { get; } = new(
        ("quick", "TaskQueue.StrongholdProtocol.Mode.Quick"),
        ("independent", "TaskQueue.StrongholdProtocol.Mode.Independent"),
        ("joint", "TaskQueue.StrongholdProtocol.Mode.Joint"));

    public static LocalizedObservableList<string> StrategyPolicies { get; } = new(
        ("default", "TaskQueue.StrongholdProtocol.Strategy.Default"),
        ("unclaimed", "TaskQueue.StrongholdProtocol.Strategy.Unclaimed"),
        ("specific", "TaskQueue.StrongholdProtocol.Strategy.Specific"));

    public static LocalizedObservableList<int> IndependentDifficulties { get; } = CreateDifficulties(false);

    public static LocalizedObservableList<int> JointDifficulties { get; } = CreateDifficulties(true);

    public static LocalizedObservableList<string> PrimaryPacts { get; } = new(
        ("auto", "TaskQueue.StrongholdProtocol.Pact.Default"),
        ("炎", "炎"), ("萨尔贡", "萨尔贡"), ("维多利亚", "维多利亚"),
        ("谢拉格", "谢拉格"), ("阿戈尔", "阿戈尔"), ("叙拉古", "叙拉古"),
        ("卡西米尔", "卡西米尔"), ("乌萨斯", "乌萨斯"), ("伊比利亚", "伊比利亚"),
        ("莱塔尼亚", "莱塔尼亚"));

    public static IReadOnlyList<GenericCombinedData<string>> Strategies { get; } = LoadStrategies();

    static SPAOptions()
    {
        LocalizationHelper.LanguageChanged += () => {
            Modes.RefreshLocalization();
            PrimaryPacts.RefreshLocalization();
            StrategyPolicies.RefreshLocalization();
            IndependentDifficulties.RefreshLocalization();
            JointDifficulties.RefreshLocalization();
        };
    }

    private static LocalizedObservableList<int> CreateDifficulties(bool joint)
    {
        var result = new LocalizedObservableList<int>(
            (-1, "TaskQueue.StrongholdProtocol.Difficulty.Current"),
            (int.MaxValue, joint ? "TaskQueue.StrongholdProtocol.Difficulty.JointMax" : "TaskQueue.StrongholdProtocol.Difficulty.Max"));
        for (var level = joint ? 4 : 20; level >= 1; --level)
        {
            result.Add(level, joint ? $"TaskQueue.StrongholdProtocol.Difficulty.Joint{level}" : level.ToString(CultureInfo.InvariantCulture));
        }
        return result;
    }

    private static IReadOnlyList<GenericCombinedData<string>> LoadStrategies()
    {
        var path = Path.Combine(PathsHelper.ResourceDir, "config", "MiniGame", "SPAStrategies.json");
        if (!File.Exists(path))
        {
            return [];
        }
        var result = new List<GenericCombinedData<string>>();
        foreach (var item in JObject.Parse(File.ReadAllText(path))["strategies"] as JArray ?? [])
        {
            if (item["band_id"]?.ToString() is { Length: > 0 } id)
            {
                result.Add(new() { Value = id, Display = $"{item["name"]} ({item["initiator"]})" });
            }
        }
        return result;
    }
}

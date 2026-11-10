// <copyright file="RoguelikeModuleLoadoutPlanner.cs" company="MaaAssistantArknights">
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
using System.IO;

namespace MaaWpfGui.Services;

/// <summary>
/// Plans module-dependent capabilities without equating historical unlocks with battle readiness.
/// </summary>
public sealed class RoguelikeModuleLoadoutPlanner(AccountModuleProfileStore store)
{
    public enum OperatorOrigin
    {
        Owned,
        Support,
        Temporary,
    }

    public sealed record Requirement(string OperatorId, string ModuleId, int MinimumLevel, int RequiredElite, int RequiredSkill);

    public sealed record BattleContext(string RunId, string BattleId, string OperatorId, OperatorOrigin Origin, int Elite, int SelectedSkill);

    public sealed record EquippedObservation(
        AccountModuleProfileStore.AccountIdentity Account,
        string RunId,
        string BattleId,
        string OperatorId,
        OperatorOrigin Origin,
        string ModuleId,
        int Level,
        string Evidence);

    public sealed record Decision(bool EligibleForPlan, bool EquipmentVerified, string Reason);

    public Decision Evaluate(
        AccountModuleProfileStore.AccountIdentity account,
        Requirement requirement,
        BattleContext context,
        EquippedObservation? equipped)
    {
        ArgumentNullException.ThrowIfNull(requirement);
        ArgumentNullException.ThrowIfNull(context);
        _ = AccountModuleProfileStore.AccountKey(account);
        if (string.IsNullOrWhiteSpace(requirement.OperatorId) || string.IsNullOrWhiteSpace(requirement.ModuleId) ||
            requirement.MinimumLevel is < 1 or > 3 || requirement.RequiredElite is < 0 or > 2 ||
            requirement.RequiredSkill is < 0 or > 3)
        {
            throw new ArgumentException("Invalid module capability requirement.", nameof(requirement));
        }

        if (string.IsNullOrWhiteSpace(context.RunId) || string.IsNullOrWhiteSpace(context.BattleId) ||
            context.Elite is < 0 or > 2 || context.SelectedSkill is < 0 or > 3 || !Enum.IsDefined(context.Origin))
        {
            return new(false, false, "invalid_battle_context");
        }

        if (context.OperatorId != requirement.OperatorId)
        {
            return new(false, false, "operator_or_form_mismatch");
        }

        if (context.Elite < requirement.RequiredElite)
        {
            return new(false, false, "battle_elite_requirement_not_met");
        }

        if (requirement.RequiredSkill > 0 && context.SelectedSkill != requirement.RequiredSkill)
        {
            return new(false, false, "skill_requirement_not_met");
        }

        bool verified = equipped != null && equipped.Account == account &&
                        equipped.RunId == context.RunId && equipped.BattleId == context.BattleId &&
                        equipped.OperatorId == context.OperatorId && equipped.ModuleId == requirement.ModuleId &&
                        equipped.Origin == context.Origin &&
                        equipped.Level is >= 1 and <= 3 && equipped.Level >= requirement.MinimumLevel &&
                        !string.IsNullOrWhiteSpace(equipped.Evidence);
        if (verified)
        {
            return new(true, true, "battle_equipment_verified");
        }

        // Own-account unlocks do not describe support or temporary operators.
        if (context.Origin != OperatorOrigin.Owned)
        {
            return new(false, false, "borrowed_operator_requires_own_equipment_evidence");
        }

        try
        {
            return store.Evaluate(account, requirement.OperatorId, requirement.ModuleId, requirement.MinimumLevel) switch {
                AccountModuleProfileStore.Eligibility.Unlocked => new(true, false, "switch_and_verify_before_battle"),
                AccountModuleProfileStore.Eligibility.RequiresUpgrade => new(false, false, "module_level_insufficient"),
                _ => new(false, false, "module_unlock_unknown"),
            };
        }
        catch (Exception error) when (error is IOException or InvalidDataException or System.Text.Json.JsonException or ArgumentException)
        {
            return new(false, false, "module_profile_invalid");
        }
    }
}

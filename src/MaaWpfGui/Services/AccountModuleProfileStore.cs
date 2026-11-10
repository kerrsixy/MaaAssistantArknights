// <copyright file="AccountModuleProfileStore.cs" company="MaaAssistantArknights">
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
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace MaaWpfGui.Services;

/// <summary>
/// Account-bound positive module evidence. Equipped state is deliberately not cached here.
/// </summary>
public sealed class AccountModuleProfileStore(string dataDirectory)
{
    private readonly object _sync = new();

    public sealed record AccountIdentity(string Client, string Uid);

    public sealed record ModuleEvidence(string OperatorId, string ModuleId, int Level, string Source, DateTimeOffset ObservedAt);

    public sealed class Profile
    {
        public int SchemaVersion { get; set; } = 1;

        public AccountIdentity? Account { get; set; }

        public List<ModuleEvidence> Modules { get; set; } = [];
    }

    public enum Eligibility
    {
        Unknown,
        RequiresUpgrade,
        Unlocked,
    }

    public static string AccountKey(AccountIdentity account)
    {
        ValidateAccount(account);
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(account))));
    }

    public Profile Load(AccountIdentity account)
    {
        lock (_sync)
        {
            return LoadCore(account);
        }
    }

    /// <summary>
    /// Merge verified positive observations only. Partial scans never erase older observations.
    /// </summary>
    /// <param name="account">Identity verified by the caller against the current game account.</param>
    /// <param name="observations">Validated module observations, not raw OCR output.</param>
    /// <returns>The merged profile.</returns>
    public Profile Merge(AccountIdentity account, IEnumerable<ModuleEvidence> observations)
    {
        ValidateAccount(account);
        var incoming = observations.ToList();
        foreach (var evidence in incoming)
        {
            ValidateEvidence(evidence);
        }

        lock (_sync)
        {
            var profile = LoadCore(account);
            foreach (var evidence in incoming)
            {
                var index = profile.Modules.FindIndex(m => m.OperatorId == evidence.OperatorId && m.ModuleId == evidence.ModuleId);
                if (index < 0)
                {
                    profile.Modules.Add(evidence);
                }
                else if (evidence.Level > profile.Modules[index].Level ||
                         (evidence.Level == profile.Modules[index].Level && evidence.ObservedAt > profile.Modules[index].ObservedAt))
                {
                    profile.Modules[index] = evidence;
                }
            }

            Save(account, profile);
            return profile;
        }
    }

    public Eligibility Evaluate(AccountIdentity account, string operatorId, string moduleId, int minimumLevel)
    {
        if (minimumLevel is < 1 or > 3)
        {
            throw new ArgumentOutOfRangeException(nameof(minimumLevel));
        }

        var module = Load(account).Modules.Find(m => m.OperatorId == operatorId && m.ModuleId == moduleId);
        return module == null ? Eligibility.Unknown : module.Level >= minimumLevel ? Eligibility.Unlocked : Eligibility.RequiresUpgrade;
    }

    private static void ValidateAccount(AccountIdentity account)
    {
        ArgumentNullException.ThrowIfNull(account);
        if (string.IsNullOrWhiteSpace(account.Client) || string.IsNullOrWhiteSpace(account.Uid) ||
            account.Client != account.Client.Trim() || account.Uid != account.Uid.Trim())
        {
            throw new ArgumentException("A verified client and game UID are required.", nameof(account));
        }
    }

    private static void ValidateEvidence(ModuleEvidence evidence)
    {
        if (string.IsNullOrWhiteSpace(evidence.OperatorId) || string.IsNullOrWhiteSpace(evidence.ModuleId) ||
            evidence.Level is < 1 or > 3 || string.IsNullOrWhiteSpace(evidence.Source) || evidence.ObservedAt == default)
        {
            throw new ArgumentException("Invalid verified module evidence.", nameof(evidence));
        }
    }

    private string ProfilePath(AccountIdentity account) => Path.Combine(dataDirectory, "accounts", AccountKey(account), "modules.json");

    private Profile LoadCore(AccountIdentity account)
    {
        var path = ProfilePath(account);
        if (!File.Exists(path))
        {
            return new Profile { Account = account };
        }

        // Corruption or an identity mismatch must be reported, never silently converted to an empty profile.
        var profile = JsonSerializer.Deserialize<Profile>(File.ReadAllText(path)) ?? throw new InvalidDataException("Empty account module profile.");
        if (profile.SchemaVersion != 1 || profile.Account != account || profile.Modules == null)
        {
            throw new InvalidDataException("Account module profile identity or schema mismatch.");
        }

        foreach (var evidence in profile.Modules)
        {
            ValidateEvidence(evidence);
        }

        if (profile.Modules.Select(m => (m.OperatorId, m.ModuleId)).Distinct().Count() != profile.Modules.Count)
        {
            throw new InvalidDataException("Duplicate account module observations.");
        }

        return profile;
    }

    private void Save(AccountIdentity account, Profile profile)
    {
        var path = ProfilePath(account);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporary, JsonSerializer.Serialize(profile, new JsonSerializerOptions { WriteIndented = true }));
            if (File.Exists(path))
            {
                File.Replace(temporary, path, path + ".bak");
            }
            else
            {
                File.Move(temporary, path);
            }
        }
        finally
        {
            if (File.Exists(temporary))
            {
                File.Delete(temporary);
            }
        }
    }
}

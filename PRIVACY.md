<!--
SPDX-FileCopyrightText: Lumaquince Contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Privacy and public distribution

The public release is intended to distribute the emulator, required redistributable components, matching modified source, build information and applicable licenses. Personal development records and personal gameplay data are not release content.

## Package policy

Exclude the following from a public application or source archive:

- Games, keys, firmware, game executable dumps and extracted game assets.
- User profiles, saves, account/network data, game libraries and controller configurations copied from a person.
- Game-derived shader/pipeline caches, screenshots, raw frame captures and resource dumps.
- Personal logs, crash dumps, validation receipts and benchmark recordings containing private paths or session metadata.
- Personal Git remotes, local branches, commit-author/email identities introduced by this fork, chat identifiers, workstation names and absolute development paths.
- Debug databases or binaries carrying private build paths unless their metadata has been deliberately sanitized and reviewed.
- Access tokens, credentials, private URLs and donation/payment addresses copied from a personal or unrelated project.

Existing upstream public authorship, copyright notices, SPDX headers and third-party license information must remain. Privacy cleanup does not erase other authors' attribution.

New fork-specific public credit is “Lumaquince Contributors.” No personal machine identity or local development-history identifier is needed to use the release.

## What this document establishes

This is the distribution policy and review guidance. It is not a claim that every file or every byte of a rebuilt binary has passed a metadata audit. The packaged [VALIDATION.md](VALIDATION.md) records the actual inventory, content and binary-metadata review results.

Update checking, web service and Discord presence are disabled in the release configuration. This is not a blanket claim that the entire inherited application is telemetry-free or that every optional network component has received a privacy audit. See [VALIDATION.md](VALIDATION.md) for actual build-option checks.

## Before sharing a bug report

Read logs before posting them. Replace personal names, usernames, home paths, game-library paths, account identifiers, hostnames, private URLs and unrelated sessions with consistent placeholders. Preserve enough relative structure and error text to explain the problem.

Prefer a minimal text excerpt with the exact application version, settings and reproduction steps. GPU model and driver version are normally sufficient for a graphics report; serial numbers and unique system identifiers are not needed.

Screenshots can reveal overlays, account names, file paths and private messages. Crash dumps and GPU captures can contain memory, game code, textures and keys. Keep those local unless a specific private transfer has been deliberately agreed.

Do not upload an entire user folder, save, shader cache, game executable or diagnostic capture to a public issue just to demonstrate that a problem exists.

## Existing data

Lumaquince uses its own application-data directory, normally %APPDATA%\Lumaquince on Windows. Some inherited configuration keys and internal names still refer to Eden, Eglantine or Lichii. Back up saves before importing data or testing another version.

Public packages should start without a copied personal profile. Any game/system files or settings created after launch belong to the user running the application.

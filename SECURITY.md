# Security and disclosure

Do not include credentials in issues, screenshots, playlists or diagnostics. Report a suspected exposure without including the secret value. Credentials already exposed publicly must be revoked at the provider; removing them from the current source tree alone is insufficient.

The plugin stores Plex access using Windows DPAPI bound to the current user. Playback URLs contain random local capabilities. Plex authentication headers are not logged. Diagnostics use fixed error codes and numeric counters. Local audit reports, SDK files, configuration and build directories are excluded from Git.

Saved-playlist references use a separate CurrentUser-DPAPI registry beside the connection profile. It records exact local aliases and server/track/part identities without copying tokens into exported playlists. Replay checks the selected server's identity and current authorization again; it does not extend old HTTP-grant validity. Lost references are not reconstructed from display titles. Keep the registry private and retain it with saved playlists. Corruption, alias reassignment, server mismatch and quotas are errors. Entries from before reference recording need to be added again from the library once.

## Audited scope

On November 5, 2026, Gitleaks 8.30.1 scanned the entire available Git history of 57 commits, the published source tree, pending changes and Debug/Release logs from Actions run 42061725581. These scans reported no findings. A synthetic GitHub token fixture was detected without printing its value; a clean comparison file produced no finding. Historical files were also checked for known private server addresses and individual Plex connection hosts, with no matches.

The published CI package contains only the DLL, installation instructions and license notices. Its text files and ANSI/UTF-16 strings extracted from the DLL were scanned without findings. Configuration persistence writes only DPAPI output to disk; its JSON representation remains in memory.

The November 6 documentation/buffer review also scanned 65 available commits, pending changes and ANSI/UTF-16 strings from the current buffer DLL, with no findings. Relative documentation links, Actions syntax and package checks passed. These checks do not publish local scan reports or access settings.

The subsequent pre-push history scan covers 66 commits without findings. Both build logs and the downloaded development package from Actions run 42189390597 were checked as well, including ANSI/UTF-16 strings extracted from its independently built x86 DLL. These scans report no findings; package contents are restricted to the five documented distribution files.

The saved-playlist review scans 72 available commits and an explicit audit copy of 131 tracked or pending source files plus ANSI/UTF-16 strings from the regular Release DLL. Gitleaks reports no findings; a separate known-private-address check reports zero matching files. The radio fixture receives no Plex or Authorization headers. Protected test profiles, reference registries and exported test playlists remain ignored and are excluded from the package.

Version 0.2.0 release checks cover 75 commits, pending release changes, source files and the versioned local DLL strings. The successful release workflow additionally scans full history. Its complete logs, downloaded ZIP contents and ANSI/UTF-16 strings from the independently built published DLL report no findings. The downloaded checksum matches the ZIP, its inventory is exactly the five distribution files, and its instructions and license notices match the release source. No local access settings or test playlists are distributed.

A scanner cannot recognize every possible confidential value. These results are not an absolute guarantee against all secret formats. CI complements checks before each push: a checksum-pinned scanner checks the entire history with redacted output and fails on findings. Finding reports are not published as artifacts.

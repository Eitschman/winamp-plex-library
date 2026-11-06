# Security and disclosure

Do not include credentials in issues, screenshots, playlists or diagnostics. Report a suspected exposure without including the secret value. Credentials already exposed publicly must be revoked at the provider; removing them from the current source tree alone is insufficient.

The plugin stores Plex access using Windows DPAPI bound to the current user. Playback URLs contain random local capabilities. Plex authentication headers are not logged. Diagnostics use fixed error codes and numeric counters. Local audit reports, SDK files, configuration and build directories are excluded from Git.

## Audited scope

On November 5, 2026, Gitleaks 8.30.1 scanned the entire available Git history of 57 commits, the published source tree, pending changes and Debug/Release logs from Actions run 42061725581. These scans reported no findings. A synthetic GitHub token fixture was detected without printing its value; a clean comparison file produced no finding. Historical files were also checked for known private server addresses and individual Plex connection hosts, with no matches.

The published CI package contains only the DLL, installation instructions and license notices. Its text files and ANSI/UTF-16 strings extracted from the DLL were scanned without findings. Configuration persistence writes only DPAPI output to disk; its JSON representation remains in memory.

The November 6 documentation/buffer review also scanned 65 available commits, pending changes and ANSI/UTF-16 strings from the current buffer DLL, with no findings. Relative documentation links, Actions syntax and package checks passed. These checks do not publish local scan reports or access settings.

A scanner cannot recognize every possible confidential value. These results are not an absolute guarantee against all secret formats. CI complements checks before each push: a checksum-pinned scanner checks the entire history with redacted output and fails on findings. Finding reports are not published as artifacts.

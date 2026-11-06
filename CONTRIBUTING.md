# Contributing

The target is Windows 10/11 with Winamp 5.9.2 Build 10042, x86. Use English for public documentation, commit messages and technical identifiers. Preserve existing source comments and keep each change focused on a specific problem.

## Build and validation

Obtain the original SDK headers using the [build instructions](docs/build.md). Configure Visual Studio 2022 with `-A Win32`, build Debug and Release, and run CTest in both configurations. GitHub Actions performs these checks using synthetic data. Passing CI does not replace tests in Winamp against a real Plex Media Server. Integration evidence and unverified cases are listed in [docs/testing.md](docs/testing.md).

Inspect visible changes in Classic and a Modern skin. Networking, playback and lifecycle changes require relevant regression tests. Run native tests only in a separate Winamp copy and stop playback afterwards. Test scripts do not authorize changes to a normal installation. Keep automated playback muted and the test instance minimized, except briefly for necessary visual checks.

## Pull requests and bug reports

Describe the problem, resulting behavior and validation briefly. Identify untested variants explicitly. Bug reports should include Winamp version, Windows version, skin, audio format and reproduction steps. Never upload tokens, private server addresses, DPAPI files, private API responses or screenshots containing personal information.

Keep SDK headers and Winamp binaries local. Review dependency licenses before adding dependencies. Project code uses [MIT](LICENSE); third-party terms are documented in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

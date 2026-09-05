# Release Process

GitHub Actions builds and tests every push to `main` and every pull request. A tag matching
`v*.*.*` additionally publishes a GitHub Release containing the NSIS installer, a portable ZIP,
and `SHA256SUMS.txt`.

The updater defaults to `SpriteOvO/AirPodsDesktop`. GitHub Actions overrides the owner and repository
with the repository running the workflow, so a fork build checks that fork for test releases. For a
local fork build, replace the placeholders with that fork's values when configuring CMake:

```powershell
cmake -S . -B Build `
  -DAPD_GITHUB_OWNER=YOUR_GITHUB_OWNER `
  -DAPD_GITHUB_REPOSITORY=YOUR_REPOSITORY
```

`APD_RELEASE_TAG_PREFIX` (default `v`) states whether that repository's release tags carry a `v`.
It only affects the version link in the Settings window; the updater's release lookup tries both
forms, so a repository whose history mixes them still resolves. Releases before v0.5.0 are tagged
without the prefix, and the workflow's tag check accepts either.

## Prepare a Release

1. Update the same semantic version in `CMakeLists.txt` and `vcpkg.json`.
2. Build and test x64 `RelWithDebInfo` locally.
3. Merge the version change into `main`.
4. Create and push a matching annotated tag:

   ```powershell
   git switch main
   git pull --ff-only
   git tag -a v0.5.0 -m "Release v0.5.0"
   git push origin v0.5.0
   ```

The workflow rejects a tag whose version differs from either project file. It publishes the signed
`win64` installer first, then copies that exact installer to a `win32-bridge` asset. Existing Win32
clients select the bridge name, while Qt 6 clients select the `win64` name. The bridge installer
checks for AMD64 Windows 10 build 17763 or newer before replacing the legacy installation.

## Verify a Release

Confirm that the workflow completed successfully, then download the installer and compare its
SHA-256 value with `SHA256SUMS.txt`:

```powershell
Get-FileHash .\AirPodsDesktop-0.5.0-win64.exe -Algorithm SHA256
Get-FileHash .\AirPodsDesktop-0.5.0-win32-bridge.exe -Algorithm SHA256
```

Keep the release as a draft only when manual acceptance testing is required; the automated workflow
publishes releases immediately.

The two installer hashes must match because the bridge is an alternate release name for the same
signed NSIS payload. Test an upgrade from the latest public Win32 release before publishing.

## Required CI and Code-Signing Secrets

Official branch and release builds require these Qt online-installer credentials:

- `QT_EMAIL`: Qt account email with access to the Qt 6.8.4 MSVC 2022 package.
- `QT_PW`: Password for that Qt account.

Tagged releases also require both code-signing secrets below. The workflow signs before creating
the bridge alias and checksums:

- `WINDOWS_SIGNING_CERTIFICATE_BASE64`: Base64-encoded PFX certificate.
- `WINDOWS_SIGNING_CERTIFICATE_PASSWORD`: Password for the PFX certificate.

The workflow fails a tagged release if either signing secret is absent. The temporary certificate
is deleted after `signtool` signs and verifies the installer.

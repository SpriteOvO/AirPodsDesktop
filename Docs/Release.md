# Release Process

GitHub Actions builds and tests every push to `main` and every pull request. A tag matching
`v*.*.*` additionally creates a draft GitHub Release containing the NSIS installer and a portable
ZIP. Review and test the draft assets before publishing the release manually.

The updater defaults to `SpriteOvO/AirPodsDesktop`. GitHub Actions overrides the owner and repository
with the repository running the workflow, so a fork build checks that fork for test releases. For a
local fork build, replace the placeholders with that fork's values when configuring CMake:

```powershell
cmake -S . -B Build `
  -DAPD_GITHUB_OWNER=YOUR_GITHUB_OWNER `
  -DAPD_GITHUB_REPOSITORY=YOUR_REPOSITORY
```

The configured repository only controls update and build links. User issue reports always point to
the upstream `SpriteOvO/AirPodsDesktop` issue tracker.

`APD_RELEASE_TAG_PREFIX` (default `v`) states whether that repository's release tags carry a `v`.
It only affects the version link in the Settings window; the updater's release lookup tries both
forms, so a repository whose history mixes them still resolves. Releases before v0.5.0 are tagged
without the prefix, and the workflow's tag check accepts either.

## Prepare a Release

1. Update the same semantic version in `CMakeLists.txt` and `vcpkg.json`.
2. Prepare the bilingual release notes locally in the ignored
   `Docs/ReleaseNotes/<version>.md` file.
3. Build and test x64 `RelWithDebInfo` locally.
4. Merge the version change into `main`.
5. Create and push a matching annotated tag:

   ```powershell
   git switch main
   git pull --ff-only
   git tag -a v0.5.0 -m "Release v0.5.0"
   git push origin v0.5.0
   ```

The workflow rejects a tag whose version differs from either project file. It packages the `win64`
installer, signs it when credentials are configured, then copies that exact installer to a
`win32-bridge` asset. Existing Win32 clients select the bridge name, while Qt 6 clients select the
`win64` name. The bridge installer checks for AMD64 Windows 10 build 17763 or newer before replacing
the legacy installation.

## Verify a Release

Confirm that the workflow completed successfully, then download both installer assets and verify
that their SHA-256 values match:

```powershell
$win64Hash = (Get-FileHash .\AirPodsDesktop-0.6.0-win64.exe -Algorithm SHA256).Hash
$bridgeHash = (Get-FileHash .\AirPodsDesktop-0.6.0-win32-bridge.exe -Algorithm SHA256).Hash
if ($win64Hash -ne $bridgeHash) { throw 'Installer hashes do not match.' }
```

The automated workflow always creates a draft. Keep it unpublished until the release notes, packaged
files, checksums, installer upgrade, and portable application have passed manual acceptance testing.
After downloading the draft assets, replace the local release-note checksum placeholders with values
calculated from those assets, then apply the reviewed body with `gh release edit --notes-file`. No
separate checksum asset is uploaded.

The two installer hashes must match because the bridge is an alternate release name for the same
NSIS payload, whether signed or unsigned. Test an upgrade from the latest public Win32 release
before publishing.

## Optional Code-Signing Secrets

Qt 6.8.3 is installed from the public package feed and does not require Qt account credentials.
When both code-signing secrets below are configured, the workflow signs the installer before creating
the bridge alias. Calculate checksums from the completed draft assets:

- `WINDOWS_SIGNING_CERTIFICATE_BASE64`: Base64-encoded PFX certificate.
- `WINDOWS_SIGNING_CERTIFICATE_PASSWORD`: Password for the PFX certificate.

When neither secret is configured, the workflow still creates a draft release, but both installer
assets are unsigned and may trigger a Windows SmartScreen warning. If only one secret is configured,
the workflow fails to prevent a partially configured release. The temporary certificate is deleted
after `signtool` signs and verifies the installer.

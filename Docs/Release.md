# Release Process

GitHub Actions builds and tests every push to `main` and every pull request. A tag matching
`v*.*.*` additionally signs the installer when credentials are configured and uploads the NSIS
installer, its `win32-bridge` alias, and a portable ZIP as a workflow artifact kept for 30 days.

CI does not create the GitHub Release. The maintainer creates the draft locally with `gh`, so the
release is authored by the maintainer's account instead of `github-actions[bot]`. Review and test
the draft assets before publishing the release manually.

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

## Download the Release Assets

Run these from the repository root after pushing the tag. `$repo` is the repository the tag was
pushed to; `Build/` is ignored by Git, so the assets never end up in a commit.

```powershell
$version = '0.6.2'
$repo = 'SpriteOvO/AirPodsDesktop'
$runId = gh run list --repo $repo --workflow windows.yml --branch "v$version" --event push `
  --limit 1 --json databaseId --jq '.[0].databaseId'
gh run watch $runId --repo $repo --exit-status
$sha = git rev-list -n 1 "v$version"
$assets = "Build/Release-v$version"
gh run download $runId --repo $repo --name "AirPodsDesktop-$version-$sha" --dir $assets
```

Select the run by `--branch "v$version"`. The push to `main` that preceded the tag builds the same
commit and uploads an artifact with the same name, but only the tag run signs the installer. If
`$runId` is empty, the tag run has not started yet; run `gh run list` again. The artifact expires
after 30 days; after that, re-run the tag workflow to produce a new one.

## Verify the Assets

Confirm that both installer assets are the same file:

```powershell
$win64Hash = (Get-FileHash "$assets/AirPodsDesktop-$version-win64.exe" -Algorithm SHA256).Hash
$bridgeHash = (Get-FileHash "$assets/AirPodsDesktop-$version-win32-bridge.exe" -Algorithm SHA256).Hash
if ($win64Hash -ne $bridgeHash) { throw 'Installer hashes do not match.' }
```

The two installer hashes must match because the bridge is an alternate release name for the same
NSIS payload, whether signed or unsigned.

Replace the checksum placeholders in `Docs/ReleaseNotes/$version.md` with values calculated from
these downloaded files; GitHub stores uploaded assets byte-for-byte, so the published checksums
match. No separate checksum asset is uploaded:

```powershell
Get-FileHash "$assets/*" -Algorithm SHA256 | Format-Table Hash, Path -AutoSize
```

## Create the Draft Release

```powershell
gh release create "v$version" `
  "$assets/AirPodsDesktop-$version-win64.exe" `
  "$assets/AirPodsDesktop-$version-win64-portable.zip" `
  "$assets/AirPodsDesktop-$version-win32-bridge.exe" `
  --repo $repo --draft --verify-tag `
  --title "AirPodsDesktop v$version" `
  --notes-file "Docs/ReleaseNotes/$version.md"
```

Keep the draft unpublished until the release notes, packaged files, checksums, installer upgrade,
and portable application have passed manual acceptance testing. Test an upgrade from the latest
public Win32 release before publishing. To revise the notes, edit the local file and apply it with
`gh release edit "v$version" --repo $repo --notes-file "Docs/ReleaseNotes/$version.md"`.

## Optional Code-Signing Secrets

Qt 6.8.3 is installed from the public package feed and does not require Qt account credentials.
When both code-signing secrets below are configured, the workflow signs the installer before creating
the bridge alias:

- `WINDOWS_SIGNING_CERTIFICATE_BASE64`: Base64-encoded PFX certificate.
- `WINDOWS_SIGNING_CERTIFICATE_PASSWORD`: Password for the PFX certificate.

When neither secret is configured, the workflow still uploads the release artifact, but both
installer assets are unsigned and may trigger a Windows SmartScreen warning. If only one secret is
configured, the workflow fails to prevent a partially configured release. The temporary certificate
is deleted after `signtool` signs and verifies the installer.

# Release Process

GitHub Actions builds and tests every push to `main` and every pull request. A tag matching
`v*.*.*` additionally publishes a GitHub Release containing the NSIS installer, a portable ZIP,
and `SHA256SUMS.txt`.

## Prepare a Release

1. Update the same semantic version in `CMakeLists.txt` and `vcpkg.json`.
2. Build and test Win32 `RelWithDebInfo` locally.
3. Merge the version change into `main`.
4. Create and push a matching annotated tag:

   ```powershell
   git switch main
   git pull --ff-only
   git tag -a v0.4.3 -m "Release v0.4.3"
   git push origin v0.4.3
   ```

The workflow rejects a tag whose version differs from either project file. Do not manually upload
or rename the installer: the updater selects an `.exe` asset containing `win32` in its name.

## Verify a Release

Confirm that the workflow completed successfully, then download the installer and compare its
SHA-256 value with `SHA256SUMS.txt`:

```powershell
Get-FileHash .\AirPodsDesktop-0.4.3-win32.exe -Algorithm SHA256
```

Keep the release as a draft only when manual acceptance testing is required; the automated workflow
publishes releases immediately.

## Optional Windows Code Signing

Add both repository secrets below to sign tagged installers before checksum generation:

- `WINDOWS_SIGNING_CERTIFICATE_BASE64`: Base64-encoded PFX certificate.
- `WINDOWS_SIGNING_CERTIFICATE_PASSWORD`: Password for the PFX certificate.

The workflow publishes an unsigned installer when neither secret exists, but fails when only one is
configured. The temporary certificate is deleted after `signtool` signs and verifies the installer.

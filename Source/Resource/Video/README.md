# AirPods animation assets

The AirPods 4, AirPods 4 (ANC), and AirPods Pro 3 animations were converted
from Apple's `com.apple.MobileAsset.SharingDeviceAssets` catalog. The source
asset identifiers are:

- `AirPods1,4-v2` — AirPods 4
- `AirPods1,5-v2` — AirPods 4 (ANC)
- `AirPodsPro1,3-v2` — AirPods Pro 3

The original `ProxCard_loop-charged.mov` files are HEVC videos premultiplied
against a black background. Run `Tools/convert_ios_animation.py` to reconstruct
the antialiased edge alpha, reverse the black matte onto white, and convert the
result to the AVI format used by this project.

Apple's catalog is available at:

<https://mesu.apple.com/assets/com_apple_MobileAsset_SharingDeviceAssets/com_apple_MobileAsset_SharingDeviceAssets.xml>

The assets may be subject to Apple's intellectual-property terms. Confirm that
you have the necessary redistribution rights before publishing binaries or
source archives containing them.

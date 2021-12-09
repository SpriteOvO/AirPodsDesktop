# Contributing Guidelines

Before you start contributing, make sure you already understand how Git works. You can learn it from [the GitHub documentation](https://docs.github.com/en/get-started/quickstart/hello-world).

## 🌍 Translation Guide

1. First you need to determine the **locale** of the target language you are going to translate into.

   A **locale** for a language looks like this: `en`, `zh`, `ja`, etc. If a language is somewhat different in some regions and you need to translate it differently, then its **locale** looks like this: `en_US`, `zh_CN`, `zh_TW`, etc.

   You can check all supported locales by launching *AirPodsDesktop* from the terminal with argument `--print-all-locales`, or `--print-all-locales=Complete` to check all full locale names (with regions).

   Examples:

   ```bash
   cd path/to/AirPodsDesktop
   ./AirPodsDesktop --print-all-locales
   ./AirPodsDesktop --print-all-locales=Complete
   ```

2. You will need the [*Qt Linguist*](https://doc.qt.io/qt-5/linguist-translators.html) tool to complete the translation work. If you didn't have it, please download and install [Qt 5.15.2](https://www.qt.io/download-qt-installer).

3. This step is to get the `.ts` translation file for a new target locale. If you just want to improve an existing translation, you can skip this step.

   You can [open an issue](https://github.com/SpriteOvO/AirPodsDesktop/issues/new/choose) to request a `.ts` translation file directly. Or continue with this step to get the `.ts` translation file yourself (requires you to build *AirPodsDesktop* yourself).

   1. Follow the [Build Instructions](/Docs/Build.md) to complete the first build.

   2. Adds the target **locale** to the variable `APD_TRANSLATION_LOCALES` in [CMakeLists.txt](/CMakeLists.txt).

   3. Rebuild *AirPodsDesktop* once and you will see the `.ts` translation file for your target **locale** in directory `/Source/Resource/Translation`.

4. Open the `.ts` translation file with *Qt Linguist* tool and complete the translation.

   If you don't know how to use the tool, please read [Qt Linguist Manual: Translators](https://doc.qt.io/qt-5/linguist-translators.html).

5. After completing the translation work, [submit a PR](https://github.com/SpriteOvO/AirPodsDesktop/compare) for merging.

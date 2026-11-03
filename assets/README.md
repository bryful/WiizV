# WiizV app icon

`wiizv-icon.png` is the approved WiizV cartridge icon, with a yellow V.
It was derived from the user's Wiiz icon with OpenAI's built-in image generation
tool on 2026-10-25. The earlier proposals are retained separately.

Run `powershell -File tools/generate-icons.ps1` to regenerate `wiizv.ico`
(16, 24, 32, 48, 64, 128, 256 pixels) and Android launcher PNGs
(mdpi through xxxhdpi) using Windows System.Drawing.

Windows embeds resource 101 and applies it to the window and taskbar.
Android references the launcher PNGs through its manifest.

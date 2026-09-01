# Security policy

WinterDash is a small open-source project maintained by one person. Thanks for helping keep it safe.

## Reporting a vulnerability

Please **don't open a public issue** for a security problem. Use GitHub's **private vulnerability reporting** on this
repository — the **Security** tab → **Report a vulnerability**. I'll respond as soon as I reasonably can.

## Scope

WinterDash reads a charger's public Bluetooth broadcast and serves a local web dashboard on your own network. The
most relevant reports involve the device's web / onboarding surface, the firmware-update path, or how it stores your
data.

## Good to know (by design, not a vulnerability)

- A **used device keeps your Wi-Fi password and the charger's Bluetooth key in flash**, unencrypted — recoverable
  with physical access. **Factory-reset before selling or passing a device on** (see the wiki's *Safety & disclaimers*
  page).
- Official prebuilt releases ship a **public, documented** OTA update password so anyone can update without
  compiling; build from source for a private one.
- The Home Assistant API has no key by default.

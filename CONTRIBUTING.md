# Contributing to WinterDash

Thanks for your interest! WinterDash is a small hobby project, but issues, ideas, and pull requests are welcome.

## Ways to help

- **Report a bug** — open an [issue](https://github.com/witekin/winterdash/issues) with what you did, what you
  expected, and what happened. Include your board, the firmware version (System/About screen), and the charger model
  if it's charger-related.
- **Ask a question or share an idea** — use [Discussions](https://github.com/witekin/winterdash/discussions).
- **Request a board or charger** — WinterDash is tested on the hardware the maintainer owns. If you have a different
  Victron Blue Smart model or an untested ESP32 board and want it supported, open a discussion. **Lending or sending
  hardware** is the fastest way to get a port done — say so there.
- **Improve the docs** — the user-facing docs are the project [wiki](https://github.com/witekin/winterdash/wiki).

## Sending a pull request

1. **Build it first.** See **[Build from source](https://github.com/witekin/winterdash/wiki/Build-from-source)** for
   the pinned-toolchain setup and how to compile + flash a board.
2. **Keep changes focused** — one topic per PR; describe what and why.
3. **Match the surrounding code** — comments and files are in English; follow the style of the code you're editing.
4. **Test on real hardware** where you can, and say which board(s) you verified on.
5. If you touch a web page under `web/`, **regenerate the baked header** (`esphome/tools/gen_dashboard_h.py` /
   `gen_captive_index.py`) and commit it alongside the source, so the build stays reproducible.

## License

WinterDash is licensed under the **[GNU GPL v3](LICENSE)**. By contributing, you agree that your contributions are
licensed under the same terms. Third-party notices are in [CREDITS.md](CREDITS.md).

# AI usage disclosure

The `bitra` branch of this fork (realme GT Neo 2 bring-up: device tree, ABL DTB shim, debug console patches and
future drivers) was developed with substantial assistance from an AI coding assistant (Claude Code by Anthropic),
directed by the repository owner: research, code and patch authoring, build debugging and documentation.

- **Experimental and only partly tested on hardware; provided as-is, without warranty.** Booting custom kernels can
  lose data; the tested path is RAM-only (`fastboot boot`).
- AI-assisted commits carry a `Co-Authored-By: Claude` trailer. Code and comments may be wrong or out of date; verify.
- The AI did not flash any device; any flashing is done manually by the owner.
- Code written here that touches subsystem code is intended for upstreaming only after human review.

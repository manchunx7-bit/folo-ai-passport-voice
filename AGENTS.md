# AI Passport Voice: Agent entry

For installation or configuration requests, read [the setup playbook](docs/AGENT_SETUP.zh-CN.md) first, then the user's platform guide. Use the local machine's OS and installed files to select commands. Do not assume a released EXE contains tools added to current source.

This repository contains one application, not flashable full firmware. Preserve existing configuration. Keep local progress, configuration backups and diagnostic reports under ignored `.setup/`; never commit user configuration, recordings or credentials. Do not inject keys or record/send audio during diagnostics. A successful doctor report does not establish actual typing.

For code changes, run relevant unit tests and `tools/check_scope.py`. Review and update `PUBLIC_FILES.json` for new public files. See [development](docs/DEVELOPMENT.zh-CN.md) for host tests; do not build a full device image from this repository.

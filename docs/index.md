---
title: plugin.automap
description: Local development and Project launch.
status: reference
---

# plugin.automap

Applies rule-based tile-map transformations through
`gams:automap/automap@1.0.0`. The `apply(rules, input, target?)` operation returns
a transformed map or an error. Maps contain layers and string-pair properties;
each layer specifies its width and flat tile list. The Host supplies WASI
imports, including randomness. The canonical contract is
`plugins/automap.comp/wit/package.wit`.

Develop with `nix develop --command make test`; output is
`dist/plugin.automap.wasm`. Build and integrity checks do not establish rule
behavior or Host integration coverage.

## Local Project and documentation

```sh
nix develop --command make open
make serve-docs                 # http://localhost:8080; override DOCS_PORT
make validate-docs
make docs                       # portable site under build/site/
```

`make open` builds the component and opens this repository's `gams.json` with
explicit `GAMS_APP_CWD`. If absent, `tools/gams.app` is downloaded from the latest
GAMS macOS Apple Silicon release, with its accompanying notices retained.
An existing download is never updated implicitly; Gatekeeper is not bypassed.
No neighboring checkout is required. First launch needs network access for the
Project's pinned published dependencies.

The local Project loads the built plugin and the published Files View to browse
Project files. It is not a plugin-specific editor or an API/runtime test.
The wiki uses pinned `justgook/wiki` v1.4.0, cached under ignored `build/`.
GitHub Pages builds with `make docs` on every push to `release`, independently of
component releases. Set Pages source to **GitHub Actions** in repository settings.

The component API and release safeguards remain documented in the repository README.

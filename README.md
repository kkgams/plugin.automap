# plugin.automap

GAMS WASM Project Unit for rule-based tile-map transformations.
The component exports `gams:automap/automap@1.0.0` through the `automap-plugin`
world. Distribution and WIT interface versions are independent.

## API

The contract is in `plugins/automap.comp/wit/package.wit`:

```text
apply(rules, input, target?) -> result<tile-map, string>
```

Maps contain layers and string-pair properties. Each layer has a width, a flat
list of tile values, and its own properties. `apply` accepts a rules map, an
input map, and an optional target map, and returns a transformed map or an error.

The Host supplies the world's WASI imports, including randomness. Plugin Manager
resolves component imports; distribution filenames do not rename WIT identities.

## Build and test

All source/build inputs are owned by this repository; no sibling checkout is
required. Use the pinned Nix environment:

```sh
nix develop --command make build
nix develop --command make test
```

Output: `dist/plugin.automap.wasm`. Install it in an external GAMS Project as
`plugins/automap.comp.wasm`; configure that Project separately.

The flake and lockfiles pin the toolchain. Network access may be required to
fetch tools. `.envrc` supports `direnv allow`.

`make test` runs offline integrity/publication regression tests, builds the
component, validates its WASM, extracts its WIT, and checks the exact input
inventory. It does **not** exercise the Automap API in a standalone WASM runtime.
Build success does not establish rule behavior or Host integration coverage.

## Licensing and releases

GAMS-authored contributions are Apache-2.0. Third-party code retains its own
terms; see `LICENSING.md`, `THIRD-PARTY-REVIEW.md`, and `LICENSES/`.
Source inventory and notice evidence are checked before candidate packaging.
Changed inputs require separately authorized evidence refreshes; source snapshot
verification remains strict and is not refreshed by release tooling.

Distribution is through GitHub Releases, not npm or OCI. Pushing a matching
`v<version.txt>` tag authorizes publication from that commit; no approval digest
variables or required publication branch are used. Manual workflow dispatch
verifies without publishing. Build/tests, canonical repository and tag/version
identity, legal texts and embedded notices, evidence/source inventory, artifact
checksums, candidate integrity, and no-overwrite checks must pass. Never reuse
or move a published or failed tag. An existing release blocks creation.

Enable immutable releases in the repository settings before publication. The
automatic GitHub token is sufficient; no administrative settings API or extra
policy token is used. Public release verification requires `immutable: true`.

The publication workflow rehearses a private draft's bytes before publication
and checks the anonymous public bytes afterward. Keep LICENSE, NOTICE, evidence,
review documents, and applicable LICENSES with distributed artifacts. See the
repository's release workflow and scripts for the enforced gates.

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

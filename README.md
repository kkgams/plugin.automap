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
Changed inputs require refreshed evidence and review of the resulting digests;
checksum consistency alone is not legal or publication approval.

Distribution is through GitHub Releases, not npm or OCI. Before publishing:

1. Review the current source, third-party evidence, and final linked artifact.
   Hosted build/candidate review and runtime validation remain outstanding until
   independently recorded; preparation or local tests do not clear them.
2. Set repository-scoped `LICENSE_SHA256` and `NOTICE_SHA256` Actions variables
   to the exact reviewed texts. Local rehearsal uses
   `APPROVED_LICENSE_SHA256` and `APPROVED_NOTICE_SHA256`.
3. Push the reviewed `release` branch and inspect its hosted candidate. Without
   approved digests, branch checks do not distribute a candidate. Manual release
   workflow dispatch verifies but does not publish.
4. Tag that reviewed release-branch commit as `v<version.txt>`. Never reuse or
   move a published or failed tag. Publication requires immutable-release policy,
   canonical repository/version identity, exact artifacts and checksums, embedded
   notices, and complete readable legal assets. An existing release blocks creation.

The publication workflow rehearses a private draft's bytes before publication
and checks the anonymous public bytes afterward. Keep LICENSE, NOTICE, evidence,
review documents, and applicable LICENSES with distributed artifacts. See the
repository's release workflow and scripts for the enforced gates.

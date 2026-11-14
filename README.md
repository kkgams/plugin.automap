# plugin.automap

Rule-based tile-map transformations for [GAMS](https://github.com/kkgams/gams).

| Unit | Source | Original work | License |
| --- | --- | --- | --- |
| Automap component | [component.c](plugins/automap.comp/component.c), [WIT](plugins/automap.comp/wit/package.wit) | GAMS contributors | [Apache-2.0](LICENSE), [dependency terms](LICENSES/), [NOTICE](NOTICE) |

## Development

Use the pinned toolchain with `nix develop --command make <target>`.

| Command | Purpose |
| --- | --- |
| `make build` | Compile the C/WASI component with embedded complete terms. |
| `make package` | Build `build.nosync/plugin.automap.wasm`, `LICENSES.zip` and `SHA256SUMS`. |
| `make open` | Build and open the local Project in the released macOS Apple Silicon Host. |
| `make docs` | Build the pinned wiki in `build.nosync/site/`. |
| `make serve-docs` | Serve docs; independent `DOCS_PORT` defaults to 8080. |
| `make validate-docs` | Validate wiki content. |
| `make clean` | Remove build.nosync and identified managed downloads; refuse tracked paths and symlinks, preserve supplied tools/Units. |

The local Files View browses the Project; it is not an Automap editor. First launch downloads pinned supporting Units. An existing Host is never updated implicitly. No Automap runtime/domain tests exist; compilation validates WASM, not rule behavior or Host integration.

## API

`gams:automap/automap@1.0.0`, world `automap-plugin`, exports `apply(rules, input, target?) -> result<tile-map, string>`. Maps contain layers and string-pair properties. Each layer has a width, flat tile values and properties. The Host supplies WASI imports, including randomness. See the WIT contract for complete types.

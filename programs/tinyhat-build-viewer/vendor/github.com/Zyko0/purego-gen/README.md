# purego-gen

purego-gen is a tool to generate pure Go function wrappers to syscalls thanks to [purego](https://github.com/ebitengine/purego) and [jen](https://github.com/dave/jennifer).

This is a big work in progress, and also an experiment.

## Description

This tool uses https://github.com/ebitengine/purego to generate wrappers to C functions using `purego.SyscallN`.

## Installation

`go install github.com/Zyko0/purego-gen/cmd/purego-gen`

## Usage

The input file must declare all go functions definitions and the necesssary types.
There are a few custom directives starting with `//puregogen:` such as:
- `//puregogen:library path=opencl path:windows=opencl32.dll alias=cl`
  - `path:<os>` specifies the path to the library based on the operating system
    - `path` can also be used
  - `alias` is the library alias for variables naming, if none specified it might be inferred from the library name.
- `//puregogen:function symbol=clCreateDevice`
  - `symbol` allows to set the symbol for the function below it if it is not named after the symbol already

There is a partial example about opencl in the [examples](./examples) directory, [examples/functions.go](./examples/functions.go) is the input file.
And the [examples/functions_impl.go](./examples/functions_impl.go) is generated via `go run cmd/purego-gen --input ./examples/functions.go`

```
Usage of purego-gen:
  --dry
        Outputs the generated code to stdout instead of a file
  --embed-loaders
        Generate a single file by linking unexported loading methods from ebitengine/purego
  --input string
        The input .go file to parse
  --extra string
        An optional list of comma separated .go files to parse for definitions
  --no-warnings
        Prevent printing warnings to sderr
  --platforms string
        A list of comma separated platforms supported for library loading (defaults to: "windows,linux,darwin,freebsd")
```

## TBD
- [ ] Support the `--embed-loaders` flag (or the non flag actually, to output multiple `_os.go` files for libraries loading)
- [ ] Support additional types
  - [ ] floats
  - [ ] structs
  - [ ] functions returned

Note: floats and structs are supported using `purego.RegisterLibFunc` as a fallback.
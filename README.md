# MOPL#

**MOPL#** is a lightweight, custom programming language and interpreter designed around a simple, readable syntax.

The repository contains the MOPL# interpreter, graphics backend, examples, documentation, and VS Code language support.

## Quick Start

### Clone

```bash
git clone https://github.com/WISHER999/mopl-install.git
cd mopl-install
```

### Build

```bash
./build.sh
```

### Run

Run a MOPL# program with:

```bash
./mopl your_program.mopl
```

For example:

```bash
./mopl examples/hello_app.mopl
```

## Example

A simple MOPL# program:

```text
Terminal "Hello, world!"
```

MOPL# is designed to keep programs readable without requiring lots of punctuation or boilerplate.

## Language Features

MOPL# currently includes features such as:

* Terminal output
* User input
* Variables and typed values
* Arithmetic operations
* Boolean values
* Comparisons
* Conditional execution
* Cycles
* Function/logic definitions
* Function calls and returns
* Tags
* Built-in functions
* Graphics
* Native macOS GUI support

## Graphics

MOPL# includes a native graphics backend on macOS.

Graphics windows can be created through `Gfx.init`, which provides an explicit window title and size.

The graphics system supports the native window/event infrastructure used by the interpreter.

Example:

```text
Gfx.init "MOPL Window" 800 600
```

Graphics functionality is being expanded as the language develops.

## VS Code Support

The repository includes a local VS Code extension under:

```text
vscode-extension/
```

It provides:

* `.mopl` file detection
* MOPL# syntax highlighting
* MOPL# run support
* Editor integration

### Install the extension locally

Open the `vscode-extension` directory in VS Code and install it as a local extension.

After installing, files with the `.mopl` extension will use the MOPL# language support.

## Examples

Example programs are located in:

```text
examples/
```

These are useful for learning the language and testing the interpreter.

## Documentation

Additional language documentation and tutorials are included in the repository.

See:

```text
TUTORIAL.md
```

and:

```text
documentation.html
```

## Project Structure

```text
mopl-install/
├── examples/
├── vscode-extension/
│   └── syntaxes/
│       └── mopl.tmLanguage.json
├── mopl_backend.c
├── graphics_backend.m
├── mopl_graphics.h
├── build.sh
├── TUTORIAL.md
├── documentation.html
└── README.md
```

## Development

MOPL# is actively developed. The language, interpreter, graphics system, and editor support may continue to evolve.

Build the interpreter after making changes:

```bash
./build.sh
```

Then test it using one of the example programs.

## License

See the repository for licensing information.

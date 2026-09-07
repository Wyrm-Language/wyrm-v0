# Wyrm Object Extensions

## Introduction

Wyrm provides critical generic services for programs:

  - A macro scripting language for writing extensions
  - An object model suitable for retained mode GUI _or_ resource tracking
    within a GUI toolkit.
  - A interoperability layer for multi language and platform projects.

Key goals:

  - Extreme interop with multiple programming languages. Wyrm should have
    first-class support for: C++, Python, Zig, Go.
  - Minimal memory requirements; suitable for embedding in deeply embedded
    projects or desktop.
  - Facility for compiling scripts into code.

## Inspiration and Competition

Major Projects providing similar services:

  - Qt: QObject, moc, and signal/slot design demonstrated by both the
    QWidget interface and QML interface.
  - GLib: the underlying GObject model and event subsystem
  - Lua: a scripting language with very similar goals
  - elisp: Emacs architecture and design

Historic Inspiration

  - [Hypercard](https://hypercard.org/) - largely because of.. 
  - [Dylan](https://opendylan.org/) – Functional programming with easier syntax

## Data Model

### Structure

A machine is the top level structure holding old active wyrm elements. The
machine includes a list of objects and the contexts.

A context a thread of engine execution. A context holds a lock, during
execution.

A fiber is an execution stack frame contained within a context. 

### Object Storage and Typing

A wyrm value consists of an enumerated type tag and a register value. The
register value should align to a single machine register on most
architectures. The numerated type fills another machine word. The
`wy_value` struct contains both.

Primitive and Fundamental types as defined in the language-spec are
special case implemented within the VM. Each fundamental type includes a
entry in the register value enumeration.

Important types by type tag:

  - **Error Type** - default initialized for default constructed / zerod
    wy_value. The register value is a pointer to error information. A
    default initialized / NULL register value is treated as a unique
    indicator for a "Not Set" error.
  - **Pair Type** - low level value utilized for sexpr tree building. The
    `nil` constant is a typed pair with uninitialized / NULL register
    value. As opposed to `(NotSet . NotSet)` - a default initialized
    heap allocated pair.

Important Constants:
  - `NotSet` { NULL, NULL }
  - `nil` { PAIR, NULL }

System Invariants:
  - A pointer based register may only contain NULL in case of PAIR or ERROR types.
  - The type tag and register value are visible only as an atomically assigned pair to the system.

## Prototype

A prototype object defines a stack frame associated with a lexical scope.
Prototypes define the set of slots to be pushed to the stack frame prior
to the start of execution.

Each prototype provides a list of slots. Stored within the slot definition
are flags indicating slot type and metainformation, a value as set as the
default or real value of the slot, and a symbol name. There are 3 primary
types of  slots:

  - Local variables; stack variables initialized on the stack
  - Boxed variables: variables that are bound within a closure
  - Static variables: a variable stored within the prototype itself



## Research Notes

### Limits

Benchmarks on large projects:

  - Linux Kernel: 43 mloc, 100k files
  - Chrome: 50 mloc, 150k files

Constraints from other programming languages:

  - Java: 65535 total methods for a class
  - Lua: 200 locals, 255 virt registers, 200 upvalues, 2^18-1 constants, 1M callstack entries

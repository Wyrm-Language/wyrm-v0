
## Semantic Explanation of Grammar

The full grammar is formally defined in [grammar.md](grammar.md).
This section defines informal details.

### Top-Level

A wyrm module is a list of statements. Every statement produces
a result. Any standalone expression is the value of the expression.

### Statements and Blocks

Wyrm follows Python's general offside rules with an additional allowance
for brace style code. The preferred style is that of Python, braces are
intended to allow one-liners and compressed scripts. 

### Comments

A comment begins with '#' and goes to the end of a line.

### Literals / Atoms

The value of any literal as a statement is the literal.

#### Identifiers

Identifiers are used for variable names, functions, classes, modules. A
standalone identifier will evaluate to its underyling value.

_Example_

    Pi  # Evaluate to Pi constant after import
    i   # evaluate to value of i

#### Numbers

Floating point and integers are supported. Numeric literals follow the
same general rules as Python.

_Examples_
   
    12345        # Boring luggage combination
    12_345       # _ allowed to separate in integer
    0b1010_0000  # Bitmake written in binary wtih separate character
    123e45       # Floating point

#### Booleans

Literal for boolean.

_Example_

    true # True value
    false # False value

#### Nil

`nil` is the literal for a null object reference.

_Example_

    nil

#### Symbol

A symbol is a name or symbol in Wyrm code expressed as a literal. A
symbol's name is either an xid-shaped identifier or one of a fixed
set of operator spellings (see `_OPERATOR_SYMBOLS` in tokenizer.wy).
Nothing else is a legal symbol.

_Examples_

    'name # symbol name
    '+
    '**

_Invalid_

    '(foo)   # grouping characters not permitted
    'a, b    # comma not permitted; terminates the symbol
    'foo bar # whitespace terminates the symbol after 'foo
    'cond?   # '?' is not a legal symbol character; lexes as 'cond
             # followed by an unexpected-token error on '?'

An implementation may limit significant characters in a symbol. An
implementation must support at least 31 codepoints of significance;
input beyond that length is accepted but excess codepoints are not
guaranteed to distinguish the symbol from another with the same
significant prefix.

#### Strings

String literals are generally interned. Wyrm strings are considered
immutable.

Normal strings are simple double-quoted values:

    "this is a string"

Multiline strings may be specified using triple double quote:

    """This is a string
    this is still part of the string."""

Raw strings may be defined using the R prefix with specific token:

    R"(Arbitrary Text)"
    R"extra(We can now have (" )extra"

Normal strings and multiline strings allow escaping as follows:

    \\ - escaped '\'
    \" - escaped double quote
    \n - escaped LF
    \r - escaped CR
    \t - escaped TAB
    \b - escaped backspace
    \f - escaped formfeed
    \v - vertical tab
    \x<<HexLiteral>> - hex specified character
    \u<<Literal>> - unicode code point

#### Characters

A character literal is Clojure-style: a backslash followed by either a
single character, or one of a handful of named characters. It evaluates to
that character's numeric (u32) value - the same value string indexing
already produces, e.g. `\a == "asdf"[0]`. More than one character
indicates a named special character.

    \a
    \newline

Special characters and C++ escapes:
    space       ' '
    tab         '\t'
    return      '\r'
    newline     '\n'
    backspace   '\b'
    formfeed    '\f'
    null        '\0'

### Built-in Collections

The value of any collection as a statement is the collection.

#### Tuples

Tuples denoted by the comma operator (least precedence):

    1, 2, 3, 4

Single element tuple use parens to force the group:

    (1,)

Empty tuple may be specified by open-close parens '()'. Tuples
are constant.

#### List / Array

A list is a sequence of wyrm objects with constant time indexing. A
list is mutable - individual elements may be assigned.

    [1, 2, 3, 4]

#### Pair List

A pair list is a sequence of pairs. Wyrm provides the same general
shorthand syntax as Scheme for defining lists, but substitutes
brackets for parens. The pair list is a low level primitive and
is critical to the representation of the underlying AST. A pair
list starts with a `$[` and concludes with a `]`. Otherwise, it
follows identical rules to defining a normal list.

    $[]                # empty list, as in scheme '()
    $['a]              # single element, as in scheme cons('a, '())
    $[1, 2, 3]         # proper pair list

To create an improper list, the 'pair' constructor can be used:

    f := pair('a, 'b)     # SCHEME - '(a . b)

#### Tables or Dictionaries

A dictionary definition uses {}.

    { "Name": 15 }

Empty dictionary:

    {}

Note: braces also define a block start token.

### Type Expression

A type expression is a specific syntax for specifying types in function
definitions, generics, or type checks.

A type identifier alone may be used as a constraint:

    int
    MyClass

Type identifiers are allowed to have parameters (generic support); the
parameter is a comma-separated list of type identifiers. Additionally,
a parameter may itself be a list of parameters:

    list[int]
    callable[[int, float], int]

Type constraints may represent summation types as well:

    int | MyClass

Runtime enforcement of typing is limited; the primary enforcement mechanism
is at compilation time.

### Operators

Numerical operators follow same rules as C/C++/Python. `**` is
right-associative and binds tighter than the unary operators on its
left operand but not its right (see power_expr in parser.wy):

    a ** 2     # Exponents
    2 ** -3    # unary minus binds inside the right operand
    2 ** 3 ** 2  # right-associative: 2 ** (3 ** 2)
    a * b * c  # Multiplication, LTR
    a + b + c  # Addition, LTR
    a - b - c  # Subtraction, LTR
    a % b % c  # Modulus, LTR
    a / b / c  # Division, LTR

Bitwise operators:

    a & b
    a | b
    a ^ b

Boolean operators / set operators:

    a or b
    a and b
    not a
    a in b

Type checking, (expression) is (type constraint) -

    a is int            # Simple Type check
    b is int | float    # Check against sum type

Comparisons:

    a <= b
    a >= b
    a < b
    a > b
    a == b
    a != b

Lookup Operator:

    arr[0]
    dictionary[key]


### Variables

Variables are declared using the 'var' keyword:

    var foo: int = 5   # Canonical declaration with type constraint
    var foo = 5        # Declaration with inferred type

The `:=` operator is shorthand for an inferred-type declaration:

    foo := 5           # Equivalent to: var foo = 5

A variable may be forward declared. A forward-declared variable is
bound to the Unset error value, regardless of its type constraint,
until first assignment:

    var foo: int      # foo is Unset (error)
    var foo: dict     # foo is Unset (error)

Assignment uses the `=` operator and requires a declared variable:

    foo = 5

Plain `=` multivalue assignment requires all targets to be declared:

    a, b = b, a        # swap

Assigning to an undeclared name is a compile-time error. Declaring a
name already declared in the same scope is an error. Declaring a name
visible from an enclosing scope is permitted and shadows it for the
duration of the inner scope.

Multiple variables may be declared from a multivalue expression. With
`:=`, all target names must be previously undefined:

    a, b := f()        # both freshly declared

Multivalue assignment is also legal with the var form:

    var a: int, b: str = f()

Wyrm offers a 'set if error' operator. Evaluation is short-circuited
if the variable's current value is not an error; otherwise the right
side is evaluated and assigned:

    var foo: int       # foo is Unset
    foo ?= 5           # foo held an error, becomes 5
    foo ?= 9           # foo is 5; right side not evaluated

The static keyword declares a variable tied to the lexical scope
itself instead of the current dynamic scope. This may be used to
create class variables and function variables. A static variable is
bound exactly once, when the enclosing class or function is created.
Without an initializer it is bound to Unset:

    fn call_count():
        static foo: int
        foo ?= 0
        foo = foo + 1
        return foo
    # call_count::foo is Unset

Or it may be declared with an initializer, evaluated once at
creation time:

    fn call_count():
        static foo: int = 0
        foo = foo + 1
        return foo
    # call_count::foo is 0

A static variable belongs to the scope that owns it: a function or class
is a namespace, and its statics are addressable from outside via the
`::` operator (e.g. `call_count::foo`).

The static keyword may also be used in a class definition to define
a variable associated with the class:

    class MyClass:
         static foo: int = 0

A class body is evaluated once, when the class definition itself is
executed (typically at module import). **Any function calls in static
initializers happen at that time, not per instance construction**.

### Modules and Imports

Import is simple:

    import mod
    import mod::baz::bar

When importing a qualified path, both the name and the qualified
path are placed into the current lexical scope. After the statement
`import mod::baz::bar` the following names are valid: `bar`, `mod`,
`mod::baz`.

The 'as' keyword allows aliasing an import name:

    import mod::baz as bt

In this example, `mod`, `mod::baz`, and `bt` are valid symbols. Aliases
are honored in all statements _within the namespace they are defined_.
An alias may be utilized for an import:

    import std as _std         # std not created, _std valid
    import _std::io as _stdio  # _std::io valid and _stdio valid
    import _stdio::File        # File is valid, _stdio remains valid

Once a module is imported, the name scope operator can pull in elements:

    import mod
    mod::function()

Multiple elements may be imported using parens:

    import std::io::(File, StreamReader, StreamWriter)

Aliases may be applied to each element:

    import std::io::(File as IOFile, StreamReader as Sr, StreamWriter)

The wildcard operator allows importing a full namespace. The wildcard operator
is not compatible with 'as' keyword:

    import std::io::*

The wildcard operator may be utilized with 'except' to exclude specific names
from an import, and multiple items may be excluded:

    import std::io::* except File  # File must be referenced via std::io::File, all other symbols in namespace
    import std::io::* except (File, StreamReader)

Constants and statics may be imported using the 'static' keyword. Static
imports disallow closures, class construction, and runtime message invocations.

    import static std::io

### Special Blocks

The do keyword allows creation of a scope, the equivalent to defining a lambda
function and immediately calling it. Used in an expression, the value of the
do statement is the last executed line:

    complex_answer := do:
        step_1()
        step_2()
        ...
        step_n()
        10

    # complex_answer == 10

### Basic Functions

Basic functions should look exceedingly familiar to Python users. Most all
the same rules apply – including no function overloading in parameters.

The basic syntax for a function is:

    fn [type...] name(parameters...) -> [result type constraint] block...

Most elements are optional, a minimal function definition with no parameters:

    fn hello():
        pass

Functions return a value with the return keyword statement:

    fn message():
        return "Hello World"

Return type may be specified:

    fn message() -> str:
        return "Hello World"

If no explicit 'return' is used, the value of the last statement is used:

    fn message() -> str:
        "Hello World"

A function may return multiple values:

    fn message() -> int, str, str:
        return 1984, "Text Here", "Text Here 2"

Parameters may be specified:

    fn message(name) -> str:
        return "Hello " + name

    fn message(greeting: str, name: str) -> str:
        return greeting + name
    
Arguments may have default values:

    fn message(name: str, greeting: str = "Hello") -> str:
        return greeting + name

Variable length arguments may be collected by the '\*' operator:
    
    fn message(\*arguments) -> str:
        greeting, name := arguments
        return greeting + name

And '\*\*' may be used to collect keyword argument into a dict:

    fn message(**kwargs) -> str:
        return kwargs["greeting"] + kwargs["name"]

Specifying one or more types creates a message. A message defines
'this' as the value of the type list (a tuple for multiple types,
or an instance for one). 

    fn [int, str] message(what: str) -> str: # message A
        ...
    
    fn [int] message(what: str) -> str: # message B
        ...

    (5, "ModA") ! message("Hello") # calls A
    5 ! message("Bye") # calls B

Invoking a function is familiar - parens and argument list:

    message(arg1, arg2, ... argn)

You may specify an argument pack (iterable):

    message(*args)

You may specify keyword argument pack (dictionary):

    message(**kwargs)

Or a combination of all:

    message(arg1, *args_n, **kwargs)

### Control Flow

Basic control flow statements - if, while, and for.

    if condition:
        statements
    elif condition_2:
        statements
    elif condition_n:
        statements
    else:
        statements

While statement

    while condition:
        statement
        if condition:
            continue
        if condition_2:
            break

For statement:

    for name in iterable:
        statement
        if condition:
            break
    else:
        statement_if_no_break

The 'in' statement must be an iterable. See special methods for
the contract.

The loop variable is a declaration: each iteration binds a fresh
variable scoped to the loop body. A closure capturing the loop
variable captures that iteration's binding. Within the `else`
clause, the loop variable remains bound to the value from the
final iteration, or Unset if the body never executed. After the
loop statement completes, the loop variable is out of scope; an
outer variable of the same name is shadowed for the duration of
the loop and unaffected by it.

Like other statements, `if`/`while`/`for` produce the value of the
last statement executed in whichever branch or iteration actually
ran. If a branch is skipped entirely - an `if` with no matching
`elif`/`else`, or a `while`/`for` whose body never executes - the
value is `nil`.

    fn t1(choice: bool) -> int:
        if choice:
            5
        else:
            10

    # t1(true) -> 5
    # t1(false) -> 10

    fn t2(choice: bool) -> int:
        if choice:
            5

    # t2(true) -> 5
    # t2(false) -> nil          (no else; condition was false)

`break` may carry a value, which becomes the loop's value in place
of whatever statement last executed:

    fn first_even(items) -> int:
        for x in items:
            if x % 2 == 0:
                break x
        else:
            nil

    # returns the first even item, or nil if the loop completes
    # (or is empty) without finding one

Try statement. If the type of the expression is an error, return immediately:

    file := try open('badfile.txt')

The catch statement may be used instead of try to set a value in case of error:

    file := open('badfile.txt') catch open('goodfile.txt')

The try statement may specify the resultant return value using return.

    value := lookup_table['value'] catch return 0

Defer block. The contents of the block are executed when the dynamic
scope of the containing block is complete.

    v := resource()
    defer:
        v ! release()

Defer with return condition. The block is armed during the dynamic scope
of the calling block and will trigger if any block within the calling block's
dynamic scope forces a return with either 'return' or 'try' statements.

    v := resource()
    defer on error:       # equivalent to defer { if ( return_value is error ) ... }
        v ! release()

### Messages

A message is a dynamically bound method on one or more objects. Messages may be
dispatched utilizing the message operator `!`.

In simple cases, a message may be utilized just as a method call:

    arr_len := array_object!length();

A message may be executed on a tuple for multiple dispatch:

    (canvas, shape) ! draw();

A message may be bound to a primitive object.

### Basic Classes

Classes are used to define structure suitable for dynamic dispatch. Objects
work in a similar fashion to CLOS or Dylan. A class is a collection of variables
associated with an inheritance tree.

A basic class defines a data structure and inherited super class. Only
single inheritance is supported. The class name may then be used to
specify dispatch of methods. A simple class is created by defining
the data structure:

    class person:
        slot name: str

    class family_member(person):
        slot relation: str

    class coordinate2d {
        slot x: float;
        slot y: float;
    }

Classes may have methods defined internally:

    class person:
        slot first_name: str = "John"
        slot last_name: str = "Doe"

        fn get_full_name() -> str:
            return last_name + " " + first_name

In the lexical scope of a class, internal memory defined by a
slot definition is accessible using the slot name directly. In
the above example 'last_name' and 'first_name' reference the
internal backing.

Note that the function 'get_full_name' has access to the internal
storage for first_name and last_name. A class method may also be
defined externally:

    fn [person] get_full_name() -> str:
        return this.last_name + " " + this.first_name

Multiple dispatch possible by giving multiple classes:

    fn [person, job] get_decorated_name() -> str:
        person_inst, job_inst := this
        return job_inst.title + " " + person_inst.last_name

Class attributes and variables can be accessed with the `.` operator.
The `.` operator looks up the given name and creates an attribute
ref. Assigning to the attribute ref results in setting a property.

    person.first_name = "Sam"

Properties and messages occupy separate namespaces. The `.` operator
resolves only in the property namespace: reading a name with no
matching property is an error, even if a message of the same name
exists. A property and a message may share a name without conflict;
`.` resolves the property and `!` resolves the message. Messages are
never reachable through attribute reference.

Methods defined in a class are invoked using the message operator.

    person ! get_full_name()

The `!` operator creates a closure. The closure may be elided
in cases where the method is called directly, or it may
be stored:

    name_func := person!get_full_name
    name_func()

The `super` allows classes to call 'up' the inheritance tree in single
dispatch or chain to the next most general function in multiple
dispatch cases.

    class coordinate3d(coordinate2d) {
        slot z: float;
    }

    fn [coordinate3d] length_squared():
        return super() + (z**2)

Objects are constructed by calling the class as a function. The return type
of the construct is always the summation of the class type with error.

    person_instance := person()

The value of the new type is either the object type constructed or error.
Essentially:

    fn person_init() -> person | error { return /* constructed person */; }

A class constructor may be specified within the class. The constructor defines
the default initialized variables for the object and then a block of code
that executes immediately after. The constructor must be named 'init'. Slots
without defaults are bound to the Unset error value, consistent with
forward-declared variables; reading such a slot before assignment yields
Unset, and the `?=` operator applies to slots in the same way.

    class vector:
        slot x: float
        slot y: float
        slot len: float
    
        fn init(x: float, y: float):
            this.x = x
            this.y = y
            this.len = (x ** 2 + y ** 2) ** 0.5

Errors / RAII - returning an error in init overrides the 'new' result:

    class demo:
        slot result: int

        fn init(num: int, den: int):
            this.result = try num / den

    var x: demo | str = demo(5, 0) catch "div0"


#### Slots

The slot keyword defines a data element with the class. Definition of a slot
creates automatic getter/setter functions. The default value for a slot may
be specified:

    class person:
        slot name: str = "John Doe"

A virtual slot is created by the addition of a code block. This nested
code block may define lexically scoped new names, but 'getter' and
'setter' messages are treated specially:

    class person:
        ...
        slot birth_timestamp: int
        ...
        slot age:
            fn getter(): now() - this.birth_timestamp
            fn setter(age: int): this.birth_timestamp = now() - age

Internally these message are defined as such:

    fn [person] age::getter() { ... } # Note: invalid syntax
    fn [person] age::setter() { ... }

The only valid functions to be defined within this scope are `getter`
and `setter`.

### Coroutines

Coroutines generally follow the exact same syntax of functions. A basic
coroutine assumes any/any for input and output.

    co simple():
        yield 1

Output may be specified:

    co count_to_5() -> int:
        yield 1
        yield 2
        yield 3
        yield 4
        yield 5

Coroutines may also accept values:

    co mirror_5x(initial) -> float | sym:
        a := yield initial
        b := yield a
        c := yield b
        d := yield c
        e := yield d
        yield e

The input type may be specified:

    co div2_1x(<- int) -> float:
        a := yield 0.0
        yield a / 2.0

An example with other parameters:

    co mirror_5x_with_add(<- float, initial: float, addend: float) -> float | sym:
         a := yield initial
         a = yield a + addend
         a = yield a + addend
         a = yield a + addend
         a = yield a + addend
         yield a + addend

Coroutines also support delegation via 'yield from'. Yield from delegates
all yield results until the subgenerator returns.

    co mirror_10x():
        yield from mirror_5x()

The 'next' builtin method requests the next value from the coroutine. The
send builtin method sends a value to the coroutine. A coroutine must be
started with 'next' before sending.

    cofun := mirror_5x()

    next(cofun) # returns 'initial, mirror_5x is paused
    next(cofun) # sends nil to mirror_5x, a=nil, yields nil
    send(cofun, 5) # sends 5 to mirror_5x, b=5, yields 5

Coroutines may be invoked as a message:

    class summation:
        slot total: int = 0

    co [summation] term_adder(<- int | nil) -> int:
        term := 0
        while term is int:
            term = yield this.total
            this.total = this.total + term

    # usage example
    adder := summation() ! term_adder()
    next(adder)         # value is 0
    send(adder, 5)      # value is 5
    send(adder, 10)     # value is 15
    send(adder, nil)    # value is error of stop iteration

A coroutine may stop execution at any point, this is done via a return statement:

    co do_1x():
        yield 1
        return 5
        yield 2 # not reached

The return statement from a coroutine is stored in the 'value' attribute. An
active coroutine will return an error when accessing:

    cofun := do_1x()
    a := next(cofun) # a = 1
    b := cofun.value catch "expected"  # b = "expected"
    c := next(cofun) # c = StopIteration error
    d := cofun.value # d = 5

### Decorators

A decorator leverages the homoiconic nature of the language for a light-weight
macro system. A decorator may prefix any statement - not just an expression -
and its argument list is optional (see decorator in parser.wy).

Example:

    @memoize()
    fn do_operation() { ... }

    @memoize() var i := $TEMPLATE;

## Types and Type System

### Fundamental Types

Fundamental types are core to the operation of wyrm. They are used and
provide core foundations for wyrm operations. These types listed here
do not include internal or implementation specific types.

Wyrm provides low level fundamental types. 'Primitive' types are non-mutable
fundamental values where two uniquely created items will satisfy an `is`
check. Primitive types:

  - **nil**: a nil value, use 'nil' global to instantiate
  - **bool**: a boolean true/false, use 'true', 'false' to instantiate, bool() call casts
  - **float**: single precision floating point, use literal to instantiate, float() call casts
  - **int**: minimum 32bit integer, use literal to instantiate, int() call casts
  - **uint**: minimum 32bit unsigned integer, use literal to instantiate, uint() call casts
  - **sym**: an interned symbol table entry, use literal to instantiate

Fundamental types expand to include more complicated collections and variables
requiring dynamic memory allocation. Some of these types are not available within
the wyrm source code:

  - **bytes**: a heap-allocated, mutable, resizable array of `u8` bytes; get/set indexing,
    append/resize/slice, str conversion, and little-endian numeric pack/unpack (see
    `doc/stdlib.md`'s `### bytes` section for the full message set)
  - **str**: a string
  - **tuple**: an immutable value sequence
  - **list**: a mutable sequence with constant-time indexing
  - **dict**: dictionary
  - **pair**: a linked list pair
  - **array**: a packed single-type array (reserved; no literal form)
  - **fiber**: thread of execution
  - **closure**: a closure
  - **coroutine**: instantiated coroutine
  - **message**: a message set
  - **class**: a class definition
  - **object**: a class instance

Class types inherit from object. These may be further subclassed as desired.

  - **error**: an error object
  - **file**: a file object

Inherited / Extended predefined class types:

  - **Unset: error** - the value of a declared but unassigned variable
  - **NotFound: error** - the value was not found
  - **OutOfMemory: error** - out of memory error
  - **StopIteration: error** - stop iteration error
  - **RuntimeError: error** - generic runtime error
  - **OSError: error** - OS error with errno

## Language Limits


Identifiers:

  - Leading 31 unicode characters of an identifier SHALL be significant. Additional character behavior is implementation defined.
  - Identifiers SHALL be binary equivalent UTF-8 encoded strings
  - A lexical scope MAY be limited to 65535 identifiers.

Messages:

  - An implementation SHALL support a minimum of 4 types for multiple dispatch.

## Semantics

### Decorators

Decorators allow reprocessing of a statement during compilation. Given
a decorator statement, the compiler calls the defined decorator function
and replaces the expression with the return result of the function. The
decorator function is a normal wyrm message defined on an AST element.

Example:

    fn [ast::BaseTree] identity() -> BaseTree:
        return this

    @identity() println("Hello")  # Evaluates to println("Hello")

    fn [ast::BaseTree] add_value(v: int) -> BaseTree:
        return ast::BinOp(\+, this, v)
    
    @add_value(5) 3 # line replaced with: 3 + 5

The primary use case would be rewriting functions:

    @change_body()
    fn modify_func():
        ...


## Core Features

### Classes

Classes are permitted to perform basic operator overloading. The following
operators may be overloaded: 

    __bool__ - change truthiness of a variable
    __add__ - add two variables
    __sub__ - subtract two variables
    __mul__ - multiply two variables
    __div__ - divide two variables
    __mod__ - modulo two variables
    __pow__ - exponentiation two variables
    __eq__ - equality comparison
    __ne__ - inequality comparison
    __lt__ - less than comparison
    __gt__ - greater than comparison
    __lte__ - less than or equal comparison
    __gte__ - greater than or equal
    __iter__ - obtain an iterable
    __call__ - operate as closure and called
    __message__ - find closure for this message
    __hash__ - return integer hash representation of this object


## Idiomatic Recommendations

### Import classes instead of module

Wyrm core library prefers leveraging objects. Prefer importing single classes directly:

    import std::io::File

    f = File("path")

The module import path may still be used:

    import std::io
    f = std::io::File("path")

While legal, using wildcard module is not recommended:

    import std::io::*
    f = File("path")

### Error Handling

Annotate functions that may return error using union error:

    fn may_fail() -> int | error:
        ...

Leverage defer on error to handle cleanup:

    resource := resource()
    defer on error:
        resource ! cleanup()

    try setup(resource)
    return resource

Consider cleaning up and terminating the error if it makes sense:

    resource := resource()
    defer on error | nil:
        resource ! cleanup()

    try setup(resource)
    check_if_should_return(resource) catch return nil
    return resource

Use the ?= operator to catch and default values. This can be leveraged
and eventually used with a try statement for a series of attempts:

    f := open('try_location_1.txt')
    f ?= open('try_location_2.txt')
    f ?= try open('try_final_location.txt')

Leverage catch to detect actual errors if truthy false is a valid result:

    f := lookup['value'] catch 0

Error may be inherited to create new error types. Using error as a method
(basic constructor) will result in a new error of the base type.

    fn make_error(bad: bool) -> int | error:
        if bad:
            return error("this is an error")
        return 0

You may also create new error types by subclassing error:

    class DetectedHardwareFailure(error) {}

    fn hardware_failed() -> int | error:
        return DetectedHardwareFailure()


## Language Extensions

### Native Code (specific to Wyrm Implementation and C modules)

**This is an internal feature.**

Wyrm is intended to be a 'self-hosted' language using 'C' as its internal
assembly language. The special built-in module 'native' specifies the module
is intended for compilation:

    import native

Importing native allows a module to create and use C functions. The import module
notifies the interpreter that the module is intended as a compiled wyrm extension.
Attempting to import a module at runtime will result in an error.

The block function defines a native block. It accepts a symbol specifying the
generation portion, a list of input symbols, a list of output symbols, and a
string parameter of content:

    native::block('HEADER, [], [], R"C(

    #include <stdio.h>
    #include <stdlib.h>

    )C")

Block parameters *MUST* be literals. The block function call is detected by the
compiler and used to feed directly into the generated code.

The following portions of a file are available:

    HEADER - includes, headers, definitions
    TYPES - type definitions
    CONSTANTS - constant definitions
    PROTOS - function prototypes
    FUNCTIONS - function definitions

Within a generated function, the block operator may be used to define a chunk of
C code. This chunk of C code will be inserted directly in the generated function.
The code chunk is placed within its own dedicated scope. The list of input variables
is read, the chunk is placed, and the output variables are written.

    fn quadratic_formula(a, b, c) -> float, float:
        var x: float
        var y: float
        native::block('HEADER, $['a, 'b, 'c], $['x, 'y], R"C(
            x = (-b + sqrt(b*b - 4*a*c)) / (2*a)
            y = (-b - sqrt(b*b - 4*a*c)) / (2*a)
        )C")
        return x, y

Generated C block:

    wy_error w_mymodule_quadratic_formula(wy_state* state)
    {
        /* magic start */
        /* magic block start */
        {
           float x;
           float y;
   
           float a = /* magic */;
           float b = /* magic */;
           float c = /* magic */;
   
           x = (-b + sqrt(b*b - 4*a*c)) / (2*a)
           y = (-b - sqrt(b*b - 4*a*c)) / (2*a)
   
           /* magic */ = x;
           /* magic */ = y;
         }
        /* magic block end */
    }

Type Mapping:

 - int -> wy_word
 - float -> float
 - bool -> bool
 - str (input only) -> wy_string*
 - object -> wy_value

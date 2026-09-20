# Wyrm Language Grammar

This grammar file is generated directly from the packrate/PEG style parser
and lexxer included in the wyrm source tree. As wyrm is still at prototype
stage, the grammar is expected to drift. However, at this point, the majority
of primitives with basic execution should be considered stable.

## Lexical Conventions

## Basics

Wyrm files shall be encoded in UTF-8.
### Keywords

Keywords are considered own tokens:
    **and, break, catch, class, co, continue, defer, do, elif,
    else, emit, false, fn, for, from, if, import, in, is,
    nil, not, or, pass, return, signal, slot
    static, task, thread, true, try, var, while, with, yield**

### Newlines

Newlines are substantial. Supported line termination is CR, CRLF, LF.
Whitespace and comments may appear between any two tokens and are
ignored with the exception of leading whitespace for the offside
rule.

### Comments

_Example_: `# This is a comment`

```ebnf
comment ::= "#" , { any_char - newline } , newline ;
```

### Line Continuations

A line ending with a \ outside of a is a continuation. A continuation
allows the string to span multiple lines, and is treated as an escape
that is deleted from the string.

### Offside rule tokens
The offside rule is applied whenver no open grouping or
string operator is active at the beginning of a line.
Whitespace at the beginning of a line **must match**.

An 'indent' is determined by a line including extra
whitespace prior to active code. An indent level is
defined by the the whitespace added.

A 'dedent' is determined by return to a previous
indentation level. A DEDENT token is emitted for each
INDENT until the whitespace matches.

```ebnf
NEWLINE         ::= (* virtual statement-separator token, emitted by lexer *) ;
INDENT          ::= (* virtual token: rise in layout column *) ;
DEDENT          ::= (* virtual token: fall in layout column *) ;
```
### Numbers

_Example_:

  * `0xfeed`
  * `1_234_567`
  * `1e-9`
  * `0xfeed_beef`
  * `1.2345_687e-9`
  * `4.`

(* Numbers are unsigned at the lexical level; a leading "-" or "+" is
   unary_expr's job, not the lexer's. *)
```ebnf
 int_literal ::= digit , { digit | "_" }
                 | "0x" , hex_digit , { hex_digit | "_" }
                 | "0b" , bin_digit , { bin_digit | "_" } ;
 float_literal ::= digit , { digit | "_" } , "." , [ digit , { digit | "_" } ] ,
                   [ ( "e" | "E" ) , [ "+" | "-" ] , digit , { digit } ]
                   | digit , { digit | "_" } ,
                   ( "e" | "E" ) , [ "+" | "-" ] , digit , { digit } ;
```

#### Digits

```ebnf
digits ::= digit , { digit | "_" } ;
digit ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
```

#### Hexadecimal Digits

```ebnf

hexdigits     ::= hexdigit , { hexdigit | "_" } ;
hex_digit     ::= digit | "a" | "b" | "c" | "d" | "e" | "f"
                        | "A" | "B" | "C" | "D" | "E" | "F" ;
```
#### Binary Digits

```ebnf
    bindigits     ::= bindigit , { bindigit | "_" } ;
```

#### Exponent_

```ebnf
exponent      ::= ( "e" | "E" ) , [ "+" | "-" ] , digits ;
```

### Identifiers and Symbols

Identifiers follow normal Unicode 'xid' and 'xid-continuation' character
rules with the additional allowance of '$' - '$' may appear anywhere in
the identifier, including as the entire identifier by itself; it is not
required to be followed by a letter.

A symbol literal is either an xid-shaped name (note: NOT the wyrm-widened
identifier rule above - '$' is not part of a symbol name) or one of a
fixed whitelist of operator spellings (see _OPERATOR_SYMBOLS below).
Nothing else is a legal symbol; e.g. `'@` is a lex error, not a symbol.

```ebnf
  identifier      ::= ( xid_start | "$" ) , { xid_continue | "$" } ;
  symbol_literal  ::= "'" , ( identifier | operator_symbol ) ;
                 (* At least 31 significant characters recognized.
                    A symbol's name may be a reserved word ('fn, 'return,
                    'if): they are node kinds in the canonical s-expression
                    format, so the literal is lexed whole rather than as
                    "'" followed by a name. *)
```
```ebnf
  bool_literal    ::= "true" | "false" ;
```

_Example_: `true`, `false`

```ebnf
  char_literal    ::= "\" , ( ( any_char - ( control_char | " " ) ) | identifier ) ;
```

_Example_: `\a`, `\newline`

## Program Structure

```ebnf
program ::= { [stmt_sep] , top_level_stmt } ;
top_level_stmt ::= import_stmt | stmt_line ;
```

# Blocks and statements
```ebnf
block           ::= ":" , NEWLINE , INDENT , stmt_list , DEDENT
                   | ":" , stmt_line, [stmt_sep]
                   | "{" , stmt_list , "}" ;
```
### Statements

Statements include all expressions with the addition of statements that
introduce name definitions or scope operations.

```ebnf
  stmt_line       ::= { decorator } ,
                       ( var_stmt
                       | assignment_stmt
                       | expression
                       | static_stmt
                       | import_stmt
                       | defer_stmt
                       | fn_def
                       | co_def
                       | class_def
                       | slot_def ) ;
  stmt_list ::= stmt_line , { [stmt_sep] , stmt_line } , [stmt_sep] ;
  stmt_sep ::= NEWLINE | ";" | EOF ;
```
#### Decorators

Decorators provide macros to a statement line. Example:

  - `@make_cached fn a_function() {`
  - `@accept dsl_language_statement`

```ebnf
  decorator       ::= "@" , identifier , [ "(" , [ arg_list ] , ")" ] ;
```
```ebnf
  defer_stmt      ::= "defer" , [ "on" , type_constraint ] , block ;
```

_Example_:

```wyrm
v := resource()
defer:
    v ! release()

v := resource()
defer on error:        # runs only if the calling block force-returned an error
    v ! release()
```

```ebnf
  if_stmt         ::= "if" , expression , block ,
                       { "elif" , expression , block } ,
                       [ "else" , block ] ;
```

_Example_:

```wyrm
if condition:
    statements
elif condition_2:
    statements
else:
    statements
```

```ebnf
  while_stmt      ::= "while" , expression , block ;
```

_Example_:

```wyrm
while condition:
    statement
    if condition_2:
        break
```

```ebnf
  for_stmt        ::= "for" , identifier , "in" , expression , block ,
                       [ "else" , block ] ;
```

_Example_:

```wyrm
for name in iterable:
    statement
    if condition:
        break
else:
    statement_if_no_break
```

```ebnf
  return_stmt     ::= "return" , [ expression ] ;
```
```ebnf
  yield_stmt      ::= "yield" , "from" , expression
                     | "yield" , [ expression ] ;
```
```ebnf
  break_stmt      ::= "break" ;
```
```ebnf
  continue_stmt   ::= "continue" ;
```
```ebnf
  pass_stmt       ::= "pass" ;
```
```ebnf
  var_stmt        ::= "var" , var_target , { "," , var_target } , [ "=" , tuple_expression ] ;
  var_target      ::= identifier , [ ":" , type_expression ] ;
```

_Example_:

```wyrm
var foo: int = 5           # canonical declaration with type constraint
var foo = 5                # declaration with inferred type
var x: int, y: int = 6, 5  # multiple names, bound from a tuple init
foo := 5                   # shorthand for the line above
```

_S-Expression_

```scheme
(define var_target type_expression expression) ; for 1 binding
(define_values ((var_0 type_0)(var_1 type_1)) expression) ; for n binding
```

```ebnf
  assignment_stmt ::= target , { "," , target } , "=" , tuple_expression
                     | target , "?=" , tuple_expression ;
```

_Example_: `a, b = b, a` (swap; both names must already be declared)

_Example_: `k ?= 4`

```scheme
(set var value) ; set var to value
(set_values (var1 var2) value) ; set multiple values
```

```ebnf
  target          ::= ( identifier | postfix_expr , "." , identifier ) , { "[" , expression , "]" } ;
```

```ebnf
  assign_op       ::= "=" | "?=" | ":=" ;
```
```ebnf
  static_stmt     ::= "static" , identifier , [ ":" , type_constraint ] , [ "=" , expression ] ;
```

_Example_:

```wyrm
fn call_count():
    static foo: int = 0
    foo = foo + 1
    return foo
```

## Type Expression

```ebnf
  type_atom       ::= qualified_name | "nil" ;
  type_constraint ::= type_atom , { "|" , type_atom } ;
```
## Modules and Imports
```ebnf
    import_stmt     ::= "import" , [ "static" ] , import_body ;
```

_Example_:

```wyrm
import mod::baz::bar
import mod::baz as bt
import std::io::(File, StreamReader, StreamWriter)
import std::io::* except (File, StreamReader)
import static std::io
```

```ebnf
    import_body     ::= qualified_name , "::" , "*" , "except" , except_names
                     | qualified_name , "::" , "*"
                     | qualified_name , "::" , "(" , import_item , { "," , import_item } , ")"
                     | qualified_name , [ "as" , identifier ] ;
```
```ebnf
  import_item     ::= identifier , [ "as" , identifier ] ;
```
```ebnf
  except_names    ::= identifier
                     | "(" , identifier , { "," , identifier } , ")" ;
```
```ebnf
  fn_def          ::= "fn" , [ class_target ] , identifier , "(" , [ param_list ] , ")" ,
                       [ "->" , type_constraint ] , block ;
```

_Example_:

```wyrm
fn message(greeting: str, name: str) -> str:
    return greeting + name

fn [int, str] message(what: str) -> str:   # multi-dispatch on (int, str)
    ...
```

```ebnf
  class_target    ::= "[" , identifier , { "," , identifier } , "]" ;   (* multi-dispatch receivers *)
```
```ebnf
  param_list      ::= param , { "," , param } ;
```
```ebnf
  param           ::= var_positional | var_keyword | plain_param ;
```
```ebnf
  plain_param     ::= identifier , [ ":" , type_constraint ] , [ "=" , expression ] ;
```
```ebnf
  var_positional  ::= "*" , identifier ;
```
```ebnf
  var_keyword     ::= "**" , identifier ;
```
```ebnf
  co_def          ::= "co" , [ class_target ] , identifier , "(" , [ co_param_list ] , ")" ,
                       [ "->" , type_constraint ] , block ;
```
```ebnf
  co_param_list   ::= "<-" , type_constraint , [ "," , param_list ]   (* send-type, not a param *)
                     | param_list ;
```
## Classes
```ebnf
  class_def       ::= "class" , identifier , [ "(" , [ arg_list ] , ")" ] , block ;
```

_Example_:

```wyrm
class person:
    slot name: str

class family_member(person):
    slot relation: str
```

```ebnf
  slot_def        ::= "slot" , identifier , [ ":" , type_constraint ] ,
                       [ "=" , expression ] , [ "with" , slot_options ] ;
```

_Example_:

```wyrm
slot name: str = "John Doe"

slot name: str = "John Doe" with:
    setter = fn (value) { this.name = value; }
    getter = undefined;
```

```ebnf
  slot_options    ::= ":" , NEWLINE , INDENT , { slot_option , stmt_sep } , DEDENT
                     | "{" , { slot_option , [ ";" ] } , "}" ;
```
```ebnf
  slot_option     ::= ( "setter" | "getter" ) , "=" , ( expression | "undefined" ) ;
```
## Expressions
```ebnf
  tuple_expression ::= list_expression ;        (* comma = tuple *)
```
```ebnf
  try_expr ::= ( "try", catch_expr ) | catch_expr;
```

_Example_: `file := try open('badfile.txt')` (an error return propagates immediately)

```ebnf
  catch_expr ::= logical_expr , [ "catch" , expression ] ;
```

_Example_: `file := open('badfile.txt') catch open('goodfile.txt')`

```ebnf
  or_expr ::= logical_expr , { "or", logical_expr }
```
```ebnf
  and_expr ::= or_expr , { "and" , or_expr }
```
```ebnf
  not_expr ::= "not" , not_expr | comparison
```
```ebnf
  comparison      ::= binary_expr , "is" , [ "not" ] , type_constraint
                     | binary_expr , [ "not" ] , "in" , binary_expr
                     | binary_expr , [ comp_op , binary_expr ] ;
  comp_op         ::= "<=" | ">=" | "<=>" | "<" | ">" | "==" | "!=" ;
```
```ebnf
  binary_expr     ::= unary_expr , { binary_op , unary_expr } ;
  binary_op       ::= "|" | "^" | "&" | "<<" | ">>" | "+" | "-" | "*" | "/" | "%" ;
```
```ebnf
  unary_expr      ::=
                     | ( "-" | "+" | "~" | "not" ) , unary_expr
                     | power_expr ;
```
```ebnf
  power_expr      ::= postfix_expr , [ "**" , unary_expr ] ;       (* right-assoc *)
```
```ebnf
  postfix_expr    ::= primary , { postfix_op } ;
  postfix_op      ::= call_op | index_op | attr_op | message_op ;
```
```ebnf
  call_op         ::= "(" , [ arg_list ] , ")" ;
  index_op        ::= "[" , expression , "]" ;
  attr_op         ::= "." , identifier ;
  message_op      ::= "!" , identifier , [ "(" , [ arg_list ] , ")" ] ;
```
```ebnf
  arg_list        ::= argument , { "," , argument } ;
  argument        ::= expression
                     | identifier , "=" , expression          (* keyword argument *)
                     | "*" , expression                       (* spread positional *)
                     | "**" , expression ;                    (* spread keyword *)
```
```ebnf
  primary         ::= literal
                     | qualified_name
                     | "(" , tuple_expression , ")"
                     | "(" , ")"                              (* empty tuple *)
                     | array_literal
                     | pair_literal
                     | dict_literal
                     | "return" , [ expression ]
                     | "yield" , "from" , expression
                     | "yield" , [ expression ]
                     | "break"
                     | "continue"
                     | "pass"
                     | if_stmt
                     | while_stmt
                     | for_stmt
                     | lambda_expr
                     | co_lambda_expr
                     | anonymous_class
                     | do_expr ;
```
```ebnf
  decorated_expr  ::= decorator , { decorator } , expression ;
```

_Example_: `v := @accept 'TOK_XID` (see decorator in parser.wy)

```ebnf
  literal         ::= nil_literal
                     | number_literal
                     | bool_literal
                     | string_literal
                     | symbol_literal
                     | char_literal
                     | "..." ;
```
```ebnf
  array_literal   ::= "[" , [ list_expression ] , "]" ;
```

_Example_: `[1, 2, 3, 4]`

```ebnf
  pair_literal    ::= "$[" , [ list_expression ] , "]" ;             (* cons-list primitive *)
```

_Example_: `$[]` (empty), `$['a]` (single element), `$[1, 2, 3]` (proper pair list)

```ebnf
  list_expression ::= expression , { "," , expression } , [ "," ] ;
```
```ebnf
  dict_literal    ::= "{" , [ dict_pair , { "," , dict_pair } ] , "}" ;
  dict_pair       ::= expression , ":" , expression ;
```

_Example_: `{ "Name": 15 }`

#### Anonymous Closures and Types

These provide a way to create an anonymous class or function. Note that
anonymous classes *must* provide their messages internally as message
definitions require the lexical class name.

```ebnf
  lambda_expr     ::= "fn" , "(" , [ param_list ] , ")" , [ "->" , type_constraint ] , block ;
```
```ebnf
  co_lambda_expr  ::= "co" , "(" , [ co_param_list ] , ")" , [ "->" , type_constraint ] , block ;
```
```ebnf
anonymous_class ::= "class" , [ "(" , [ qualified_name ] , ")" ] , block ;
```

_Example_:
```wyrm
AnonPerson := class() { slot name: str; slot age: int; }

# Equivalent
class NamedPerson:
    slot name: str
    slot age: int
```
```ebnf
  do_expr         ::= "do" , block ;
```

_Example_:

```wyrm
complex_answer := do:
    step_1()
    step_2()
    10
# complex_answer == 10
```


# Point Blank .wmi DSL Grammar Specification

> **Note**: This document reflects the current DSL implementation in [`ConfigParser.cpp`](/src/config/ConfigParser.cpp) and [`ConfigParser.hpp`](/include/pointblank/config/ConfigParser.hpp).

## EBNF Grammar

```ebnf
(* Top-level structure - supports multiple blocks and imports anywhere *)
config_file       = { import_directive | block | if_statement }, ;

(* Preprocessor directives - can appear anywhere in the file *)
(* #import  - User extensions from ~/.config/pblank/extensions/user/ *)
(* #include - Built-in extensions from ~/.config/pblank/extensions/pb/ *)
import_directive  = "#import", identifier
                  | "#include", identifier ;

(* Blocks and statements *)
block             = identifier, ":", "{", { statement }, "}", [ ";" ] ;
statement         = assignment
                  | if_statement
                  | block
                  | exec_directive ;

(* Assignment - supports both identifier and string literal as name *)
(* This allows keybind syntax like "SUPER, Q": "killactive" *)
assignment        = ( identifier | string_literal ), ":", expression, [ ";" ] ;

if_statement      = "if", "(", expression, ")", "{", { statement }, "}"
                  , [ "else", "{", { statement }, "}" ], [ ";" ] ;

exec_directive    = "exec", ":", string_literal ;

(* Expressions - in order of precedence *)
expression        = logical_or ;

logical_or        = logical_and, { "||", logical_and } ;
logical_and       = equality, { "&&", equality } ;
equality          = comparison, { ( "==" | "!=" ), comparison } ;
comparison        = term, { ( "<" | ">" | "<=" | ">=" ), term } ;
term              = factor, { ( "+" | "-" ), factor } ;
factor            = unary, { ( "*" | "/" ), unary } ;
unary             = [ ( "!" | "-" ) ], primary ;

primary           = integer_literal
                  | float_literal
                  | string_literal
                  | boolean_literal
                  | identifier
                  | member_access
                  | array_literal
                  | "(", expression, ")" ;

member_access     = identifier, { ".", identifier } ;

(* Array literals *)
array_literal     = "[", [ expression, { ",", expression } ], "]" ;

(* Literals *)
integer_literal   = digit, { digit } ;
float_literal     = digit, { digit }, ".", digit, { digit } ;
string_literal    = '"', { character }, '"' ;
boolean_literal   = "true" | "false" ;
identifier        = letter, { letter | digit | "_" } ;

(* Keywords *)
keyword           = "let" | "if" | "else" | "exec" | "true" | "false" ;

(* Character classes *)
letter            = "a" | "b" | ... | "z" | "A" | "B" | ... | "Z" ;
digit             = "0" | "1" | ... | "9" ;
character         = ? any character except '"' ? ;
```

## Lexical Rules

### Comments
```
// Single-line comment (C++ style)
/* Multi-line comment */
```

### Whitespace
Spaces, tabs, newlines are ignored except as token separators.

### Preprocessor Directives
- Can appear anywhere in the file (not just at the beginning)
- `#import <name>` - Import user extension (loads `.so` from `~/.config/pblank/extensions/user/`)
- `#include <name>` - Import Point Blank built-in extension (loads from `~/.config/pblank/extensions/pb/`)

### String Literals
- Double-quoted strings: `"example"`
- Escape sequences supported: `\n`, `\t`, `\"`, `\\`

### Identifiers
- Start with letter or underscore
- Followed by letters, digits, or underscores
- Case-sensitive
- Cannot be keywords

### Keywords (Reserved)
```
let, if, else, exec, true, false
```

### Operators
```
Arithmetic: +, -, *, /
Comparison: ==, !=, <, >, <=, >=
Logical: &&, ||, !
Assignment: =, :
```

### Array Literals
```wmi
let arr = [1, 2, 3];
let strings = ["a", "b", "c"];
let mixed = [1, "two", true];
```

Arrays can contain any expression type and are parsed by the [`primary()`](/src/config/ConfigParser.cpp:887) method.

## Semantic Rules

### Type System
The DSL supports four primitive types:
- `int` - Integer numbers (e.g., `42`, `-17`)
- `float` - Floating-point numbers (e.g., `0.8`, `3.14`)
- `string` - Text strings (e.g., `"Firefox"`, `"SUPER"`)
- `bool` - Boolean values (`true`, `false`)

### Variable Declarations
```wmi
let opacity = 0.8;
let max_workspace = 12;
let terminal = "alacritty";
```

> **Implementation Status**: The `let` keyword is recognized by the lexer ([`ConfigParser.cpp:270`](/src/config/ConfigParser.cpp)) but variable declarations are not yet fully implemented in the parser. This feature is planned for future release.

### Member Access (Property Chaining)
```wmi
if (window.class == "Firefox") {
    opacity: 0.9
};
```

Built-in objects:
- `window` - Current window context
  - `window.class` - Window class (string)
  - `window.title` - Window title (string)
  - `window.workspace` - Current workspace (int)

### Conditional Logic
```wmi
if (window.class == "Firefox") {
    opacity: 1.0
} else {
    opacity: 0.8
};
```

### Keybind Syntax
```wmi
binds: {
    "SUPER, Q": "killactive"
    "SUPER, F": "fullscreen"
    "SUPER, SHIFT, Q": exec: "alacritty"
};
```

Implemented in [`ConfigParser::assignment()`](../src/config/ConfigParser.cpp:647).

Keybind format: `"MODIFIERS, KEY": action` or `"MODIFIERS, KEY": exec: "command"`

Valid modifiers:
- `SUPER` - Windows/Super key
- `ALT` - Alt key
- `CTRL` - Control key
- `SHIFT` - Shift key (can use `L_SHIFT` or `R_SHIFT` for specific side)

### Block Structure
Blocks create configuration namespaces:

```wmi
pointblank: {
    window_rules: {
        opacity: 0.8
    };
    workspaces: {
        max_workspace: 12
    };
};
```

Each block ends with optional semicolon.

### Execution Directive
```wmi
exec: "command with args"
```

Used for executing shell commands from keybinds.

## Parser Implementation Notes

### Implementation Files
- **Header**: [`include/pointblank/config/ConfigParser.hpp`](../include/pointblank/config/ConfigParser.hpp)
- **Source**: [`src/config/ConfigParser.cpp`](../src/config/ConfigParser.cpp)

### Recursive Descent Strategy
1. **Lexer** produces token stream
2. **Parser** consumes tokens using recursive functions matching grammar rules
3. **AST** built using `std::variant` nodes for type safety

### Error Recovery
- **Panic Mode**: On error, synchronize to next statement boundary (`;`, `}`)
- **Productions**: Each parsing function returns `std::unique_ptr<Node>` or `nullptr` on error
- **Error Collection**: All errors collected in vector, parsing continues

### AST Node Hierarchy
```cpp
Expression -> std::variant<
    IntLiteral,      // int value
    FloatLiteral,    // double value
    StringLiteral,   // std::string value
    BoolLiteral,     // bool value
    Identifier,      // std::string name
    BinaryOp,        // left, op, right
    UnaryOp,         // op, operand
    MemberAccess,    // object, member
    ArrayLiteral     // elements vector
>

Statement -> std::variant<
    Assignment,     // name, value
    IfStatement,    // condition, then_branch, else_branch
    Block,          // name, statements
    ExecDirective   // command
>
```

> **Note**: The `let` keyword is recognized by the lexer (`TokenType::Let`) but variable declarations are not yet fully implemented in the parser. The AST does not include a `VariableDeclaration` node type.

### Type Checking
Performed during interpretation:
- Binary ops require compatible types
- Comparisons between same types
- Member access validates object existence
- Assignment type coercion (int -> float allowed)

## Example Configuration

```wmi
#import animation
#import blur

// Point Blank main configuration
pointblank: {
    window_rules: {
        opacity: 0.8
        blur: true
    };
    
    workspaces: {
        max_workspace: 12
    };
    
    binds: {
        "SUPER, Q": "killactive"
        "SUPER, F": "fullscreen"
        "SUPER, SHIFT, Q": exec: "alacritty"
        "SUPER, 1": "workspace 1"
        "SUPER, 2": "workspace 2"
    };
    
    animations: {
        enabled: true
        curve: "ease-in-out"
    };
};

// Conditional window rules
if (window.class == "Firefox") {
    window_rules: {
        opacity: 1.0
        blur: false
    };
};
```

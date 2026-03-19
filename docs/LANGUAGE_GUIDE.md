# 📘 CM Language Guide (v2-SPEC)

Welcome to the definitive guide for the **CM Language**. This document covers everything from basic syntax to advanced features.

## 🏁 Basics

### Variables
CM distinguishes between **immutable** (`let`) and **mutable** (`mut`) variables.

```cm
let x = 10;      // Immutable: cannot be changed
mut y = 20;      // Mutable: can be reassigned
y = y + 5;       // Correct: y is now 25
```

### Types
CM is statically typed but supports powerful type inference. Common types include:
- `int`, `float`, `string`, `bool`, `void`
- `array<T>`, `slice<T>`, `map<K, V>`
- `?T` (Option types), `Result<T, E>` (Error types)

## 🎛️ Control Flow

### If-Else
CM uses standard conditional branching.

```cm
if (z > 10) {
    println("z is greater than 10");
} else {
    println("z is small");
}
```

### While Loops
Iterate based on a condition. (Note: `for` loops in v2 translate to `while` internally).

```cm
mut i = 0;
while (i < 10) {
    println(i);
    i = i + 1;
}
```

### Match (Pattern Matching)
Powerful branching based on a variable's value. Supports a catch-all `_` arm.

```cm
match (z) {
    10 => println("Found 10"),
    20 => println("Found 20"),
    _  => println("Found something else"),
}
```

## 🏗️ Structure

### Functions
Declare reusable logic blocks with `fn`.

```cm
fn add(a: int, b: int) -> int {
    return a + b;
}
```

### Structs and Impls
Group data and define methods.

```cm
struct Point {
    x: int;
    y: int;
}

impl Point {
    fn print_coords(p: Point) {
        println("Point is at:");
        println(p.x);
        println(p.y);
    }
}
```

## 🪄 Special Features

### The `println` Macro
The `println` function is **type-agnostic**. It automatically identifies whether its argument is an `int`, `float`, `string`, or reference-counted object and prints it correctly.

```cm
println("Hello!"); // Prints string
println(42);       // Prints int
println(3.14);     // Prints float
```

### C-Polyglot Blocks
Embed raw C code directly for low-level optimizations.

```cm
c {
    printf("Directly from C!\n");
}
```

---
*✨ CM Language: The ergonomic power of C.*

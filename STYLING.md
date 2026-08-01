# Styling guide

Follow these styling rules when contributing to the project.

## Table of contents

1. [Indentation](#indentation)
    1. [Brace Placement](#brace-placement)
    2. [Switch Statements](#switch-statements)
2. [Header Files](#header-files)
3. [Naming](#naming)
    1. [Function, Class, and Struct names](#function-class-and-struct-names)
    2. [Variables](#variables)
    3. [Class Methods](#class-methods)
    4. [Class Members](#class-members)

## Indentation

Use four spaces for indentation; do not use tabs.

### Brace Placement

Smedley uses a [K&R-like brace style](https://en.wikipedia.org/wiki/Indentation_style#K&R_style).
Put function, class, and struct braces on the next line. Put control-flow braces
on the same line as the statement.

```cpp
class ExamplePlugin : public smedley::Plugin
{
public:
    void OnLoad()
    {
        if (true) {
            logger().Info("Hello, world!");
        }
    }
};
```

### Switch Statements

Do not indent case clauses within a switch. Avoid fallthrough whenever possible.

```cpp
switch (ch) {
case 'a':
    // ...
    break;
case 't':
    // ...
    break;
default:
}
```

## Header Files

Begin all header files with `#pragma once`.

## Naming

### Function, class, and struct names

Use PascalCase for function, class, and struct names:

```cpp
void ThisIsAFunction()
{
    // ...
}

class ExampleClass
{
    int ExampleMethod();
};
```

### Variables

Variable names may use lowercase letters and `snake_case`.

### Class methods

Class methods typically use PascalCase. Property-like methods, meaning getters
with no arguments and setters with one argument, use lowercase `snake_case` like
variable names.

```cpp
class HelloWorld
{
    int _foo;
public:
    inline int foo() { return _foo; }
    inline void foo(int val) { _foo = val; }
};
```

### Class members

Prefix private and protected member variables with an underscore. Style public
members as normal variable names.

```cpp
class HelloWorld
{
protected:
    int _a;
    char *_b;
public:
    long long c;
};
```

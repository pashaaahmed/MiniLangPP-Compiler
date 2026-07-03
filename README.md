# MiniLangPP-Compiler
MiniLang++ Compiler Front End — a full compiler pipeline in C++17 covering lexical analysis, recursive descent parsing, semantic analysis, and three address code generation. Includes constant folding and typed array support.


# MiniLang++ Compiler Front End
A complete compiler front end for MiniLang++, a simplified imperative programming language, implemented entirely in C++17.
Overview
This project takes MiniLang++ source code through four sequential compilation phases and prints the output of each stage:
Source Code → Lexer → Parser → Semantic Analyzer → TAC Generator
Features

Lexical Analyzer: Tokenizes source code into KEYWORD, IDENTIFIER, NUMBER, OPERATOR, and DELIMITER tokens, skips comments, and reports invalid characters with exact line and column numbers
Recursive Descent Parser: Builds a full Abstract Syntax Tree and recovers from syntax errors to continue reporting additional issues
Semantic Analyzer: Enforces type checking, scope rules, and function call correctness using a scoped symbol table, with uninitialized variable detection
Three Address Code Generator: Emits intermediate code with temporary variables, labels, and conditional jumps for control flow

# Language Support

Types: int, float, bool
Arithmetic operators: + - * / %
Logical operators: && || !
Relational operators: < > <= >= == !=
Control structures: if-else, while
Typed function declarations and return types
Typed arrays with indexed read and write access
Block level scoping

# Advanced Features

Constant folding: literal expressions like 2 + 3 * 4 are collapsed to 14 at compile time
Uninitialized variable usage detection before assignment
Array index type checking

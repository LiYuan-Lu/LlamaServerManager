# Agent Guidelines

## C++ code style

- Formatting is defined by `.clang-format` at the project root (WebKit style).
  Run `clang-format -i` on every C++ file you create or modify before finishing.
- **Always use braces `{}` for control statement bodies, even single-line ones.**
  This applies to `if`, `else`, `else if`, `for`, `while` and `do`, including
  code inside `#ifdef` blocks and lambdas.

  ```cpp
  // Wrong
  if (handle)
      CloseHandle(handle);
  if (!error.isEmpty()) setState("FAILED", error);

  // Right
  if (handle) {
      CloseHandle(handle);
  }
  if (!error.isEmpty()) {
      setState("FAILED", error);
  }
  ```

- Do not rely on clang-format's `InsertBraces` alone: it skips statements near
  `#ifdef` blocks and can produce broken code inside one-line lambdas. Write the
  braces yourself.
- Keep line endings as CRLF (see `.gitattributes`).

## Commit messages

Use Conventional Commits: `type(scope): description`
(e.g. `feat`, `fix`, `style`, `refactor`, `test`, `docs`, `chore`).

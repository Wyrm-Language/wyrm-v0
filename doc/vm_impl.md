# Virtual Machine Implementation

## Code Organization / Standards

Basic Stuff:
  - generally 1 header 1 source per software piece
  - all functions namespaced with wy_ followed by the module (wy_fiber, wy_context, etc...)
  - all preprocessor macros use WY_ namespace in all caps (WY_)

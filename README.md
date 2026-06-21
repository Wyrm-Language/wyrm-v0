# wyrm-core
Core implementation of wyrm

# Project Layout

- include: Installable header files
  - wyrm.h: Default include file for consumers (`#include <wyrm.h>`)
  - wyrm: Namespaced headers
    - sys: System definitions, compiled into wyrm
    - platform: Runtime selectable platform abstractions

- src: Project implementation
  - test: Common unit tests
  - port: System porting layer, one must be selected
    - hosted: generic/standard C11 hosted implementation
  - platform.name: Module that can be enabled/disabled

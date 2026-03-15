.. _verbosity:

Verbosity / Debug output
========================

Some internal components of tttrlib can print additional diagnostic information
(e.g., header parsing progress) to the standard error stream when verbosity is
enabled. You can toggle this at runtime using the TTTRLIB_VERBOSE environment
variable.

Enable verbosity
----------------

- Linux/macOS (Bash):

  export TTTRLIB_VERBOSE=1

- Windows CMD:

  set TTTRLIB_VERBOSE=1

- Windows PowerShell:

  $env:TTTRLIB_VERBOSE = "1"

Truthy/falsey values
--------------------

Any non-empty value enables verbosity, except the following case-insensitive
falsey values: ``0``, ``false``, ``no``, ``off``.

Examples that ENABLE verbosity: ``1``, ``true``, ``yes``, ``on``, ``debug``.

To DISABLE verbosity, either unset the variable or set it to an empty string or
one of the falsey values above.

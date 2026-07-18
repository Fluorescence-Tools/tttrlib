.. _configuration:

Configuration
=============

tttrlib can be configured through environment variables to control various aspects of its behavior, such as verbosity, compression, and randomization.

Environment Variables
---------------------

TTTRLIB_VERBOSE
~~~~~~~~~~~~~~~

Enable detailed debug logging and diagnostic output to stderr.

- **Default:** ``0`` (disabled)
- **Type:** Boolean (truthy/falsy)
- **Description:** Controls verbose output from internal components (e.g., header parsing progress)

**Truthy values** (enable verbosity): ``1``, ``true``, ``yes``, ``on``, ``debug``

**Falsy values** (disable verbosity): ``0``, ``false``, ``no``, ``off``, or empty string

**Examples:**

- Linux/macOS (Bash):

  .. code-block:: bash

     export TTTRLIB_VERBOSE=1

- Windows CMD:

  .. code-block:: bash

     set TTTRLIB_VERBOSE=1

- Windows PowerShell:

  .. code-block:: bash

     $env:TTTRLIB_VERBOSE = "1"

TTTR_COMPRESS_ON_READ
~~~~~~~~~~~~~~~~~~~~~

Controls whether TTTR data is compressed when read from disk.

- **Default:** enabled
- **Type:** Boolean (truthy/falsy)
- **Description:** Compression improves memory efficiency for large datasets, but may impact performance for small or random access patterns

**Falsy values** (disable compression): ``0``, ``false``, ``off`` (case-insensitive)

**Note:** Non-sequential selections automatically avoid compression for correctness, regardless of this setting.

TTTR_RND_SEED
~~~~~~~~~~~~~

Random seed for microtime linearization in ``apply_channel_luts()``.

- **Default:** ``0``
- **Type:** Integer
- **Description:** Controls randomization in microtime linearization for reproducible results

Set to an integer value for reproducible linearization results when applying channel LUTs. If not set or invalid, defaults to 0 (deterministic behavior).

**Examples:**

- Linux/macOS (Bash):

  .. code-block:: bash

     export TTTR_RND_SEED=42

- Windows CMD:

  .. code-block:: bash

     set TTTR_RND_SEED=42

- Windows PowerShell:

  .. code-block:: bash

     $env:TTTR_RND_SEED = "42"

# ptolib in this tree

`ptolib.h` and `pto_tui.hpp` come from https://github.com/tpeulen/ptolib
-- v0.3.2 (91fa6fb3de582b0c13fb3ac4d2a2ba23f10d4b41), as verbatim copies.

Do not edit them here. Fix upstream, then re-run `scripts/vendor.sh` from the
ptolib checkout pointing at this directory. The test suite compares these
files against `../ptolib` when that checkout is present, and refuses a
symlink: what is committed must build without the checkout.

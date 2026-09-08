# ptolib in this tree

`ptolib.h` and `pto_tui.hpp` come from https://github.com/tpeulen/ptolib
-- v0.3.1 (6b0d17c9f9be833e1a0dabbdd533a2fd5ca18cfb), as verbatim copies.

Do not edit them here. Fix upstream, then re-run `scripts/vendor.sh` from the
ptolib checkout pointing at this directory. The test suite compares these
files against `../ptolib` when that checkout is present, and refuses a
symlink: what is committed must build without the checkout.

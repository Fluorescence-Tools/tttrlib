# SPDX-License-Identifier: BSD-3-Clause
#
# Pythonic surface for Axis, injected as methods by SWIG (%extend).


@property
def edges(self):
    """The n+1 bin edges, so a caller can label the axis it asked for."""
    return self.get_edges()


@property
def centers(self):
    """Bin centres -- what you plot against."""
    return self.get_centers()


@property
def widths(self):
    """Bin widths. Not constant for a log, pow or variable axis, which is
    exactly when a caller needs them: a density is counts / width."""
    return self.get_widths()


@property
def traits(self):
    """The axis options, as a dict."""
    o = self.options()
    return {
        "underflow": o.underflow,
        "overflow": o.overflow,
        "circular": o.circular,
        "growth": o.growth,
    }


def __len__(self):
    return self.size()


def __repr__(self):
    # SWIG maps the enum to a plain int, so the name has to come from a table.
    kind = _AXIS_KIND_NAMES.get(int(self.kind()), "axis")
    label = self.label()
    tail = ", label=%r" % label if label else ""
    if kind in ("category", "boolean"):
        return "Axis.%s(%d bins%s)" % (kind, self.size(), tail)
    return "Axis.%s(%d, %g, %g%s)" % (kind, self.size(), self.lo(),
                                      self.hi(), tail)

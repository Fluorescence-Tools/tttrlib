# SPDX-License-Identifier: BSD-3-Clause
#
# localization -- single-molecule localization by PSF fitting.
#
# The cleanest leaf in the tree: the header reaches for the optimizer and
# nothing else, and the implementation's only other dependencies are Eigen and
# autodiff. It is also half the reason Eigen is a project-wide REQUIRED today --
# two files need it and every target pays for it. Declaring it here is what will
# let that stop being true, once `nn` (the other half) is out as well.
tttrlib_add_module(
    NAME     localization
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/ImageLocalization.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  opt
    EXTERNAL_DEPS tttrlib::eigen tttrlib::autodiff
    SWIG_INTERFACES Localization.i
    TEST_DIR test/python
)

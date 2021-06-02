Getting Started
===============

The purpose of this guide is to illustrate some of the main features that
``tttrlib`` provides. It assumes a very basic working knowledge of
fluorescence spectroscopy (decay analysis, correlation spectroscopy,
etc.). Please refer to our :ref:`installation instructions
<installation-instructions>` for installing ``tttrlib``.

``tttrlib`` is an open source library that supports a diverse set of
experimental TTTR data. It also provides various tools for processing TTTR
data, data preprocessing.

Opening TTTR files and accesing data
------------------------------------

``tttrlib`` provides a simple interface to TTTR data contained in TTTR files.
The data in TTTR files is presented via so called :term:`TTTR` objects. These
objects can be created on their on or by reading files.

Here is a simple example where we fit a
:class:`~sklearn.ensemble.RandomForestClassifier` to some very basic data::

  >>> import tttrlib
  >>> tttr1 = TTTR()
  >>> tttr2 = TTTR('filename.ptu')
  >>> microtimes = tttr2.microtimes

TTTR objects can be used to compute correlation curves, fluorescence decays,
for photon distribution analysis, or to compute fluorescence images in 
confocal laser scanning microscopy.

Next steps
----------

We have briefly covered how TTTR files are read and data contained in 
these files are accessed. Please refer to our :ref:`user_guide` for details 
on all the tools that we provide. You can also find an exhaustive list of 
the public API in the
:ref:`api_ref`.

You can also look at our numerous :ref:`examples <general_examples>` that
illustrate the use of ``tttrlib`` in many different contexts.

The :ref:`tutorials <tutorial_menu>` also contain additional learning
resources.

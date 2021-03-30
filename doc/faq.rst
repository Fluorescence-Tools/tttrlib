.. _faq:

===========================
Frequently Asked Questions
===========================

.. currentmodule:: tttrlib

Here we try to give some answers to questions that regularly pop up.

What is the project name (a lot of people get it wrong)?
--------------------------------------------------------
tttrlib that stands for time-tagged time-resolved (TTTR) library.
TTTR data are predominantly used in fluorescence spectroscopy and other
techniques that require a high time resolution, e.g., LIDAR (Light Detection
and Ranging).

What's the best way to get help on tttrlib usage?
-------------------------------------------------

**For tttrlib usage questions**, first check the existing documentation. In
case you encounter an error, contact the authors.

Please make sure to include a minimal reproduction code snippet (ideally shorter
than 10 lines) that highlights your problem on a toy dataset (for instance from
the data sets provided in the ``tttrlib`` repository or randomly generated with
functions of ``numpy.random`` with a fixed random seed). Please remove any line of
code that is not necessary to reproduce your problem.

The problem should be reproducible by simply copy-pasting your code snippet in a Python
shell with scikit-learn installed. Do not forget to include the import statements.

More guidance to write good reproduction code snippets can be found at:

https://stackoverflow.com/help/mcve

If your problem raises an exception that you do not understand (even after googling it),
please make sure to include the full traceback that you obtain when running the
reproduction script.

For bug reports or feature requests, please make use of the
`issue tracker on GitHub <https://github.com/fluorescence-tools/tttrlib/issues>`_.

How can I load my own datasets into a format usable by tttrlib?
--------------------------------------------------------------------

Generally, tttrlib is intended as a library for all kinds of TTTR data.
If you data is not supported, contact the authors and provide the authors
with a dataset that can be integrated into a public repository.


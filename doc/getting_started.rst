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

Fitting and predicting: estimator basics
----------------------------------------

``Scikit-learn`` provides dozens of built-in machine learning algorithms and
models, called :term:`estimators`. Each estimator can be fitted to some data
using its :term:`fit` method.

Here is a simple example where we fit a
:class:`~sklearn.ensemble.RandomForestClassifier` to some very basic data::

  >>> from sklearn.ensemble import RandomForestClassifier
  >>> clf = RandomForestClassifier(random_state=0)
  >>> X = [[ 1,  2,  3],  # 2 samples, 3 features
  ...      [11, 12, 13]]
  >>> y = [0, 1]  # classes of each sample
  >>> clf.fit(X, y)
  RandomForestClassifier(random_state=0)

The :term:`fit` method generally accepts 2 inputs:

- The samples matrix (or design matrix) :term:`X`. The size of ``X``
  is typically ``(n_samples, n_features)``, which means that samples are
  represented as rows and features are represented as columns.
- The target values :term:`y` which are real numbers for regression tasks, or
  integers for classification (or any other discrete set of values). For
  unsupervized learning tasks, ``y`` does not need to be specified. ``y`` is
  usually 1d array where the ``i`` th entry corresponds to the target of the
  ``i`` th sample (row) of ``X``.

Both ``X`` and ``y`` are usually expected to be numpy arrays or equivalent
:term:`array-like` data types, though some estimators work with other
formats such as sparse matrices.

Once the estimator is fitted, it can be used for predicting target values of
new data. You don't need to re-train the estimator::

  >>> clf.predict(X)  # predict classes of the training data
  array([0, 1])
  >>> clf.predict([[4, 5, 6], [14, 15, 16]])  # predict classes of new data
  array([0, 1])


STOP

Next steps
----------

We have briefly covered estimator fitting and predicting, pre-processing
steps, pipelines, cross-validation tools and automatic hyper-parameter
searches. This guide should give you an overview of some of the main
features of the library, but there is much more to ``scikit-learn``!

Please refer to our :ref:`user_guide` for details on all the tools that we
provide. You can also find an exhaustive list of the public API in the
:ref:`api_ref`.

You can also look at our numerous :ref:`examples <general_examples>` that
illustrate the use of ``scikit-learn`` in many different contexts.

The :ref:`tutorials <tutorial_menu>` also contain additional learning
resources.

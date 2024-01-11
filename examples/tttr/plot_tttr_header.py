"""
===========
TTTR-Header
===========

Introduction
============
TTTR files can contain additional meta-data. In tttrlib the meta data can be
accessed and edited via the TTTRHeader class. Some TTTR file formats suchh as the
PTU format provide the option for arbitrary meta data. In other TTTR formats have
defined / fixed headers. Some of the information that can be found in a TTTR file
is listed in `Header tags sheet <https://docs.google.com/spreadsheets/d/1_xt3Yx3ucWLXfb14qbOEgX27CjcKeVyClbu7GdgSF8I/edit?usp=sharingL>`_.

Note, the list of Header tags is by any means not comprehensive. If you have information
on the used header tags. Please contact the authors of tttrlib to contribute
to the list.


Accessing header data
=====================
Most TTTR container contain meta-data that can be accessed through ``tttrlib``.
For that, a ``TTTR`` object provides a header attribute. The header attribute is
of the type :class:`.Header`.
"""

import tttrlib

data = tttrlib.TTTR('../../tttr-data/bh/pq/bh_spc132.spc', 'SPC-130')
# the header can be accesses by the method get_header or as an property
header = data.header


#%%
# The most important attributes of the header are the :py:attr:`micro_time_resolution`
# and :py:attr:`macro_time_resolution`. Becker&Hickl Spc132 files files contain
# only a limited amount of information in the first record (32 bit).
#
# PicoQuant PTU and HT3 files provide more extensive information in their header
# that can be accessed via the :py:attr:`json` attribute of a header object. The
# :py:attr:`json` attribute of a header object is a JSON string. Setting the :py:attr:`json`
# attribute modifies the header / meta data of a TTTR object.

import json
data = tttrlib.TTTR('./test/data/PQ/PTU/pq_ptu_hh_t3.ptu', 'PTU')

#%%
# the header can be accesses by the method get_header or as a property

header = data.header
header_json = header.json
header_dict = json.loads(header_json)
print(header_dict)

#%%
# Time calibration data
# =====================
# Essential for the analysis of TTTR data is the time calibration (time resolution)
# of the macro and the micro times in addition to the number of possible micro time
# channels. In many functions the micro and macro time calibration are transparently
# handeled, meaning there is no need to worry. The ``macro_time`` and the
# ``micro_time`` TTTR attributes correspond the to raw uncalibrated data.
#
# The macro and micro time resolution is accessed as follows.

macro_time_resolution = data.header.macro_time_resolution
micro_time_resolution = data.header.micro_time_resolution

#%%
# The number of micro time channels can be accessed as displayed below.
# The header can be accesses by the method get_header or as an property.

header = data.get_header()
macro_time_resolution = data.header.macro_time_resolution
# macro_time_resolution = 12.5 ns
micro_time_resolution = data.header.micro_time_resolution
# micro_time_resolution = 4 ps
data.header.number_of_micro_time_channels
# will return 8129
data.get_number_of_micro_time_channels()
# will return 3125


#%%
# Note, the effective number of micro time channels, i.e., the number of micro time
# channels can be smaller than the actual number of micro time channels. For
# instance at a micro time channel resolution of 4 ps and macro time resolution
# of 12.5 ns effectively only 3125 micro time channels will be filled with
# photons.
#
# Creating and writing TTTRHeader
# ===============================
# Each TTTR object has an attribute that is an instance of the TTTRHeader class.
#     This instances makes the meta-data contained in the TTTR file accessible. TTTRHeader
# objects can also be created independently of TTTR object.

header = tttrlib.TTTRHeader()


#%%
# Modifying meta-data
# ===================
# The data containted in a TTTRHeader instance can be accesses as JSON string. The
# `JSON <https://www.json.org>`_ string.

header = tttrlib.TTTRHeader()

#%%
# The JSON string must contain a `tags` list. The `tags` list is a list of dictionarys
# in which each dictionary corresponds to a meta-data field in the header. For instance,

print(header.json) # '{\n "tags": []\n}'

#%%
# Different meta data fields are listed in
# `Header tags sheet <https://docs.google.com/spreadsheets/d/1_xt3Yx3ucWLXfb14qbOEgX27CjcKeVyClbu7GdgSF8I/edit?usp=sharingL>`_.

#
# This file is part of the GROMACS molecular simulation package.
#
# Copyright (c) 2019, by the GROMACS development team, led by
# Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
# and including many others, as listed in the AUTHORS file in the
# top-level source directory and at http://www.gromacs.org.
#
# GROMACS is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public License
# as published by the Free Software Foundation; either version 2.1
# of the License, or (at your option) any later version.
#
# GROMACS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with GROMACS; if not, see
# http://www.gnu.org/licenses, or write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
#
# If you want to redistribute modifications to GROMACS, please
# consider that scientific software is very special. Version
# control is crucial - bugs must be traceable. We will be happy to
# consider code for inclusion in the official distribution, but
# derived work must not be called official GROMACS. Details are found
# in the README & COPYING files - if they are missing, get the
# official version at http://www.gromacs.org.
#
# To help us fund GROMACS development, we humbly ask that you cite
# the research papers on the package. Check out http://www.gromacs.org.

# Python setuptools script to build and install the gmxapi Python interface
# from a GROMACS installation directory.

# Usage note: things go smoothly when we stick to the setup.py convention of
# having a package source directory with the same name as the package at the
# same level as the setup.py script and only expect `pip install .` in the
# setup.py directory. If we play with the layout more, it is hard to keep all
# of the `pip` and `setup.py` cases working as expected. This is annoying
# because running the Python interpreter immediately from the same directory
# can find the uninstalled source instead of the installed package. We can
# ease this pain by building an sdist in the enclosing CMake build scope
# and encouraging users to `pip install the_sdist.archive`. Otherwise, we
# just have to document that we only support full build-install of the Python
# package from the directory containing setup.py, which may clutter that
# directory with some artifacts.

import os

from skbuild import setup
import cmake

gmx_toolchain = os.getenv('GROMACS_TOOLCHAIN')
gmxapi_DIR = os.getenv('gmxapi_DIR')

def _find_first_gromacs_suffix(directory):
    dir_contents = os.listdir(directory)
    for entry in dir_contents:
        if entry.startswith('gromacs'):
            return entry.strip('gromacs')

if gmx_toolchain is None:
    # Try to guess from standard GMXRC environment variables.
    if gmxapi_DIR is None:
        gmxapi_DIR = os.getenv('GROMACS_DIR')
    if gmxapi_DIR is not None:
        if os.path.exists(gmxapi_DIR) and os.path.isdir(gmxapi_DIR):
            share_cmake = os.path.join(gmxapi_DIR, 'share', 'cmake')
            suffix = _find_first_gromacs_suffix(share_cmake)
            if suffix is not None:
                gmx_toolchain = os.path.join(share_cmake,
                                             'gromacs' + suffix,
                                             'gromacs-toolchain' + suffix + '.cmake')

class GmxapiInstallError(Exception):
    """Error processing setup.py for gmxapi Python package."""

if gmx_toolchain is None:
    raise GmxapiInstallError('Could not configure for GROMACS installation. Provide GROMACS_TOOLCHAIN.')
else:
    gmx_toolchain = os.path.abspath(gmx_toolchain)

if gmxapi_DIR is None:
    # Example: given /usr/local/gromacs/share/cmake/gromacs/gromacs-toolchain.cmake,
    # we would want /usr/local/gromacs.
    # Note that we could point more directly to the gmxapi-config.cmake but,
    # so far, we have relied on CMake automatically looking into
    # <package>_DIR/share/cmake/<package>/ for such a file.
    # We would need a slightly different behavior for packages that link against
    # libgromacs directly, as sample_restraint currently does.
    gmxapi_DIR = os.path.abspath(
        os.path.join(os.path.dirname(gmx_toolchain), '..', '..', '..'))

if gmxapi_DIR != os.path.commonpath([gmxapi_DIR, gmx_toolchain]):
    raise GmxapiInstallError('GROMACS toolchain file {} is not in gmxapi_DIR {}'.format(
        gmx_toolchain,
        gmxapi_DIR
    ))

# TODO: Use package-specific hinting variable.
# We want to be sure that we find a <package>-config.cmake associated with the
# toolchains file, but we want to preempt most of the normal CMake
# [search procedure](https://cmake.org/cmake/help/latest/command/find_package.html#id5),
# which could lead to hard-to-diagnose build problems.
# Note that <package>_ROOT is not standard until CMake 3.12
# Reference https://cmake.org/cmake/help/latest/policy/CMP0074.html#policy:CMP0074
_cmake_major, _cmake_minor = cmake.__version__.split('.')[0:2]
if int(_cmake_major) >= 3 and int(_cmake_minor) >= 12:
    cmake_gmxapi_hint = '-Dgmxapi_ROOT={}'
else:
    cmake_gmxapi_hint = '-DCMAKE_PREFIX_PATH={}'
cmake_gmxapi_hint = cmake_gmxapi_hint.format(gmxapi_DIR)

setup(
    name='gmxapi',

    # TODO: (pending infrastructure and further discussion) Replace with CMake variables from GMXAPI version.
    version='0.1.0.dev3',
    python_requires='>=3.5, <4',
    setup_requires=['cmake>=3.9.6',
                    'setuptools>=28',
                    'scikit-build>=0.7'],

    packages=['gmxapi', 'gmxapi.simulation'],

    # TODO: It may be necessary to put these or other OSX directives in the toolchain file.
    cmake_args=['-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.9',
                '-DCMAKE_OSX_ARCHITECTURES:STRING=x86_64',
                '-DCMAKE_TOOLCHAIN_FILE={}'.format(gmx_toolchain),
                cmake_gmxapi_hint
                ],

    author='M. Eric Irrgang',
    author_email='info@gmxapi.org',
    description='gmxapi Python interface for GROMACS',
    license='LGPL',
    url='http://gmxapi.org/',

    # The installed package will contain compiled C++ extensions that cannot be loaded
    # directly from a zip file.
    zip_safe=False
)
.. Note that this must be a single rst file in order for Sphinx
   to build into into a single plain-text file to place in the
   installation tarball.

.. _install guide:

******************
Installation guide
******************

.. highlight:: bash

Introduction to building |Gromacs|
----------------------------------

These instructions pertain to building |Gromacs|
|version|. You might also want to check the `up-to-date installation instructions`_.

Quick and dirty installation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
1. Get the latest version of your C and C++ compilers.
2. Check that you have CMake version |CMAKE_MINIMUM_REQUIRED_VERSION| or later.
3. Get and unpack the latest version of the |Gromacs| tarball.
4. Make a separate build directory and change to it.
5. Run ``cmake`` with the path to the source as an argument
6. Run ``make``, ``make check``, and ``make install``
7. Source ``GMXRC`` to get access to |Gromacs|

Or, as a sequence of commands to execute:

.. parsed-literal::

    tar xfz gromacs-|version|.tar.gz
    cd gromacs-|version|
    mkdir build
    cd build
    cmake .. -DGMX_BUILD_OWN_FFTW=ON -DREGRESSIONTEST_DOWNLOAD=ON
    make
    make check
    sudo make install
    source /usr/local/gromacs/bin/GMXRC

This will download and build first the prerequisite FFT library
followed by |Gromacs|. If you already have FFTW installed, you can
remove that argument to ``cmake``. Overall, this build of |Gromacs|
will be correct and reasonably fast on the machine upon which
``cmake`` ran. On another machine, it may not run, or may not run
fast. If you want to get the maximum value for your hardware with
|Gromacs|, you will have to read further. Sadly, the interactions of
hardware, libraries, and compilers are only going to continue to get
more complex.

Quick and dirty cluster installation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

On a cluster where users are expected to be running across multiple
nodes using MPI, make one installation similar to the above, and
another using ``-DGMX_MPI=on``.
The latter will install binaries and libraries named using
a default suffix of ``_mpi`` ie ``gmx_mpi``. Hence it is safe
and common practice to install this into the same location where
the non-MPI build is installed.

Typical installation
^^^^^^^^^^^^^^^^^^^^

As above, and with further details below, but you should consider
using the following `CMake options`_ with the
appropriate value instead of ``xxx`` :

* ``-DCMAKE_C_COMPILER=xxx`` equal to the name of the C99 `Compiler`_ you wish to use (or the environment variable ``CC``)
* ``-DCMAKE_CXX_COMPILER=xxx`` equal to the name of the C++17 `compiler`_ you wish to use (or the environment variable ``CXX``)
* ``-DGMX_MPI=on`` to build using `MPI support`_
* ``-DGMX_GPU=CUDA`` to build with NVIDIA CUDA support enabled.
* ``-DGMX_GPU=OpenCL`` to build with OpenCL_ support enabled.
* ``-DGMX_GPU=SYCL`` to build with SYCL_ support enabled (using `Intel oneAPI DPC++`_ by default).
* ``-DGMX_SYCL_HIPSYCL=on`` to build with SYCL_ support using hipSYCL_ (requires ``-DGMX_GPU=SYCL``).
* ``-DGMX_SIMD=xxx`` to specify the level of `SIMD support`_ of the node on which |Gromacs| will run
* ``-DGMX_DOUBLE=on`` to build |Gromacs| in double precision (slower, and not normally useful)
* ``-DCMAKE_PREFIX_PATH=xxx`` to add a non-standard location for CMake to `search for libraries, headers or programs`_
* ``-DCMAKE_INSTALL_PREFIX=xxx`` to install |Gromacs| to a `non-standard location`_ (default ``/usr/local/gromacs``)
* ``-DBUILD_SHARED_LIBS=off`` to turn off the building of shared libraries to help with `static linking`_
* ``-DGMX_FFT_LIBRARY=xxx`` to select whether to use ``fftw3``, ``mkl`` or ``fftpack`` libraries for `FFT support`_
* ``-DCMAKE_BUILD_TYPE=Debug`` to build |Gromacs| in debug mode

Building older versions
^^^^^^^^^^^^^^^^^^^^^^^

Installation instructions for old |Gromacs| versions can be found at
the |Gromacs| `documentation page
<http://manual.gromacs.org/documentation>`_.

Prerequisites
-------------

Platform
^^^^^^^^

|Gromacs| can be compiled for many operating systems and
architectures.  These include any distribution of Linux, Mac OS X or
Windows, and architectures including x86, AMD64/x86-64, several
PowerPC including POWER8, ARM v8, and SPARC VIII.

Compiler
^^^^^^^^

|Gromacs| can be compiled on any platform with ANSI C99 and C++17
compilers, and their respective standard C/C++ libraries. Good
performance on an OS and architecture requires choosing a good
compiler. We recommend gcc, because it is free, widely available and
frequently provides the best performance.

You should strive to use the most recent version of your
compiler. Since we require full C++17 support the minimum
compiler versions supported by the GROMACS team are

* GNU (gcc/libstdc++) 7
* LLVM (clang/libc++) 7
* Microsoft (MSVC) 2019

Other compilers may work (Cray, Pathscale, older clang) but do
not offer competitive performance. We recommend against PGI because
the performance with C++ is very bad.

The Intel classic compiler (icc/icpc) is no longer supported in
|Gromacs|. Use Intel's newer clang-based compiler from oneAPI, or
gcc.

The xlc compiler is not supported and version 16.1 does not compile on
POWER architectures for |Gromacs|\ -\ |version|. We recommend to use
the gcc compiler instead, as it is being extensively tested.

You may also need the most recent version of other compiler toolchain
components beside the compiler itself (e.g. assembler or linker);
these are often shipped by your OS distribution's binutils package.

C++17 support requires adequate support in both the compiler and the
C++ library. The gcc and MSVC compilers include their own standard
libraries and require no further configuration. If your vendor's
compiler also manages the standard library library via compiler flags,
these will be honored. For configuration of other compilers, read on.

On Linux, the clang compilers typically use for their C++ library
the libstdc++ which comes with g++. For |Gromacs|, we require
the compiler to support libstc++ version 7.1 or higher. To select a
particular libstdc++ library, provide the path to g++ with
``-DGMX_GPLUSPLUS_PATH=/path/to/g++``.

To build with clang and llvm's libcxx standard library, use
``-DCMAKE_CXX_FLAGS=-stdlib=libc++``.

If you are running on Mac OS X, the best option is gcc. The Apple
clang compiler provided by MacPorts will work, but does not support
OpenMP, so will probably not provide best performance.

For all non-x86 platforms, your best option is typically to use gcc or
the vendor's default or recommended compiler, and check for
specialized information below.

For updated versions of gcc to add to your Linux OS, see

* Ubuntu: `Ubuntu toolchain ppa page`_
* RHEL/CentOS: `EPEL page`_ or the RedHat Developer Toolset

Compiling with parallelization options
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

For maximum performance you will need to examine how you will use
|Gromacs| and what hardware you plan to run on. Often OpenMP_
parallelism is an advantage for |Gromacs|, but support for this is
generally built into your compiler and detected automatically.

.. _gmx-gpu-support:

GPU support
~~~~~~~~~~~

|Gromacs| has excellent support for NVIDIA GPUs supported via CUDA.
On Linux, NVIDIA CUDA_ toolkit with minimum version |REQUIRED_CUDA_VERSION|
is required, and the latest version is strongly encouraged. NVIDIA GPUs with at
least NVIDIA compute capability |REQUIRED_CUDA_COMPUTE_CAPABILITY| are
required. You are strongly recommended to
get the latest CUDA version and driver that supports your hardware, but
beware of possible performance regressions in newer CUDA versions on
older hardware.
While some CUDA compilers (nvcc) might not
officially support recent versions of gcc as the back-end compiler, we
still recommend that you at least use a gcc version recent enough to
get the best SIMD support for your CPU, since |Gromacs| always runs some
code on the CPU. It is most reliable to use the same C++ compiler
version for |Gromacs| code as used as the host compiler for nvcc.

To make it possible to use other accelerators, |Gromacs| also includes
OpenCL_ support. The minimum OpenCL version required is
|REQUIRED_OPENCL_MIN_VERSION| and only 64-bit implementations are supported.
The current OpenCL implementation is recommended for
use with GCN-based AMD GPUs, and on Linux we recommend the ROCm runtime.
Intel integrated GPUs are supported with the Neo drivers.
OpenCL is also supported with NVIDIA GPUs, but using
the latest NVIDIA driver (which includes the NVIDIA OpenCL runtime) is
recommended. Also note that there are performance limitations (inherent
to the NVIDIA OpenCL runtime).
It is not possible to support both Intel and other vendors' GPUs with OpenCL.
A 64-bit implementation of OpenCL is required and therefore OpenCL is only
supported on 64-bit platforms.

Please note that OpenCL backend does not support the following GPUs:

* NVIDIA Volta (CC 7.0, e.g., Tesla V100 or GTX 1630) or newer,
* AMD RDNA1/2/3 (Navi 1/2X,3X, e.g., RX 5500 or RX6900).

Since |Gromacs| 2021, the support for SYCL_ is added.
The current SYCL implementation can be compiled either with `Intel oneAPI DPC++`_
compiler for Intel GPUs, or with hipSYCL_ compiler and ROCm runtime for
AMD GFX9 and CDNA GPUs. Using other devices supported by these compilers is
possible, but not recommended.

It is not possible to configure several GPU backends in the same build
of |Gromacs|.


.. _mpi-support:

MPI support
~~~~~~~~~~~

|Gromacs| can run in parallel on multiple cores of a single
workstation using its built-in thread-MPI. No user action is required
in order to enable this.

If you wish to run in parallel on multiple machines across a network,
you will need to have an MPI library installed that supports the MPI
2.0 standard. That's true for any MPI library version released since
about 2009, but the |Gromacs| team recommends the latest version (for
best performance) of either your vendor's library, OpenMPI_ or MPICH_.

To compile with MPI set your compiler to the normal (non-MPI) compiler
and add ``-DGMX_MPI=on`` to the cmake options. It is possible to set
the compiler to the MPI compiler wrapper but it is neither necessary
nor recommended.

GPU-aware MPI support
~~~~~~~~~~~~~~~~~~~~~~

In simulations using multiple GPUs, an MPI implementation with GPU support
allows communication to be performed directly between the
distinct GPU memory spaces without staging through CPU memory, often
resulting in higher bandwidth and lower latency communication. The only
current support for this in |Gromacs| is with a CUDA build targeting
Nvidia GPUs using "CUDA-aware" MPI libraries.  For
more details, see `Introduction to CUDA-aware MPI
<https://developer.nvidia.com/blog/introduction-cuda-aware-mpi/>`_.

To use CUDA-aware MPI for direct GPU communication we recommend
using the latest OpenMPI version (>=4.1.0) with the latest UCX version
(>=1.10), since most GROMACS internal testing on CUDA-aware support has 
been performed using these versions. OpenMPI with CUDA-aware support can 
be built following the procedure in `these OpenMPI build instructions
<https://www.open-mpi.org/faq/?category=buildcuda>`_.

With ``GMX_MPI=ON``, |Gromacs| attempts to automatically detect CUDA support
in the underlying MPI library at compile time, and enables direct GPU 
communication when this is detected.  However, there are some cases when
GROMACS may fail to detect existing CUDA-aware support, in which case
it can be manually enabled by setting environment variable ``GMX_FORCE_GPU_AWARE_MPI=1``
at runtime (although such cases still lack substantial
testing, so we urge the user to carefully check correctness of results
against those using default build options, and report any issues).

CMake
^^^^^

|Gromacs| builds with the CMake build system, requiring at least
version |CMAKE_MINIMUM_REQUIRED_VERSION|. You can check whether
CMake is installed, and what version it is, with ``cmake
--version``. If you need to install CMake, then first check whether
your platform's package management system provides a suitable version,
or visit the `CMake installation page`_ for pre-compiled binaries,
source code and installation instructions. The |Gromacs| team
recommends you install the most recent version of CMake you can.

.. _FFT support:

Fast Fourier Transform library
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Many simulations in |Gromacs| make extensive use of fast Fourier
transforms, and a software library to perform these is always
required. We recommend FFTW_ (version 3 or higher only) or Intel
MKL_. The choice of library can be set with ``cmake
-DGMX_FFT_LIBRARY=<name>``, where ``<name>`` is one of ``fftw3``,
``mkl``, or ``fftpack``. FFTPACK is bundled with |Gromacs| as a
fallback, and is acceptable if simulation performance is not a
priority. When choosing MKL, |Gromacs| will also use MKL for BLAS and
LAPACK (see `linear algebra libraries`_). Generally, there is no
advantage in using MKL with |Gromacs|, and FFTW is often faster.
With PME GPU offload support using CUDA, a GPU-based FFT library
is required. The CUDA-based GPU FFT library cuFFT is part of the
CUDA toolkit (required for all CUDA builds) and therefore no additional
software component is needed when building with CUDA GPU acceleration.

Using FFTW
~~~~~~~~~~

FFTW_ is likely to be available for your platform via its package
management system, but there can be compatibility and significant
performance issues associated with these packages. In particular,
|Gromacs| simulations are normally run in "mixed" floating-point
precision, which is suited for the use of single precision in
FFTW. The default FFTW package is normally in double
precision, and good compiler options to use for FFTW when linked to
|Gromacs| may not have been used. Accordingly, the |Gromacs| team
recommends either

* that you permit the |Gromacs| installation to download and
  build FFTW from source automatically for you (use
  ``cmake -DGMX_BUILD_OWN_FFTW=ON``), or
* that you build FFTW from the source code.

If you build FFTW from source yourself, get the most recent version
and follow the `FFTW installation guide`_. Choose the precision for
FFTW (i.e. single/float vs. double) to match whether you will later
use mixed or double precision for |Gromacs|. There is no need to
compile FFTW with threading or MPI support, but it does no harm. On
x86 hardware, compile with *both* ``--enable-sse2`` and
``--enable-avx`` for FFTW-3.3.4 and earlier. From FFTW-3.3.5, you
should also add ``--enable-avx2`` also. On Intel processors supporting
512-wide AVX, including KNL, add ``--enable-avx512`` also.
FFTW will create a fat library with codelets for all different instruction sets,
and pick the fastest supported one at runtime.
On ARM architectures with SIMD support and IBM Power8 and later, you
definitely want version 3.3.5 or later,
and to compile it with ``--enable-neon`` and ``--enable-vsx``, respectively, for
SIMD support. If you are using a Cray, there is a special modified
(commercial) version of FFTs using the FFTW interface which can be
slightly faster.

Using MKL
~~~~~~~~~

Use MKL bundled with Intel compilers by setting up the compiler
environment, e.g., through ``source /path/to/compilervars.sh intel64``
or similar before running CMake including setting
``-DGMX_FFT_LIBRARY=mkl``.

If you need to customize this further, use

::

    cmake -DGMX_FFT_LIBRARY=mkl \
          -DMKL_LIBRARIES="/full/path/to/libone.so;/full/path/to/libtwo.so" \
          -DMKL_INCLUDE_DIR="/full/path/to/mkl/include"

The full list and order(!) of libraries you require are found in Intel's MKL documentation for your system.

Using ARM Performance Libraries
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ARM Performance Libraries provides FFT transforms implementation for ARM
architectures.
Preliminary support is provided for ARMPL in |Gromacs| through its FFTW-compatible API.
Assuming that the ARM HPC toolchain environment including the ARMPL paths
are set up (e.g. through loading the appropriate modules like
``module load Module-Prefix/arm-hpc-compiler-X.Y/armpl/X.Y``) use the following cmake
options:

::

    cmake -DGMX_FFT_LIBRARY=fftw3 \
          -DFFTWF_LIBRARY="${ARMPL_DIR}/lib/libarmpl_lp64.so" \
          -DFFTWF_INCLUDE_DIR=${ARMPL_DIR}/include


Other optional build components
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* Run-time detection of hardware capabilities can be improved by
  linking with hwloc. By default this is turned off since it might
  not be supported everywhere, but if you have hwloc installed it
  should work by just setting ``-DGMX_HWLOC=ON``
* Hardware-optimized BLAS and LAPACK libraries are useful
  for a few of the |Gromacs| utilities focused on normal modes and
  matrix manipulation, but they do not provide any benefits for normal
  simulations. Configuring these is discussed at
  `linear algebra libraries`_.
* The built-in |Gromacs| trajectory viewer ``gmx view`` requires X11 and
  Motif/Lesstif libraries and header files. You may prefer to use
  third-party software for visualization, such as VMD_ or PyMol_.
* An external TNG library for trajectory-file handling can be used
  by setting ``-DGMX_EXTERNAL_TNG=yes``, but TNG
  |GMX_TNG_MINIMUM_REQUIRED_VERSION| is bundled in the |Gromacs|
  source already.
* The lmfit library for Levenberg-Marquardt curve fitting is used in
  |Gromacs|. Only lmfit |GMX_LMFIT_REQUIRED_VERSION| is supported.  A
  reduced version of that library is bundled in the |Gromacs|
  distribution, and the default build uses it. That default may be
  explicitly enabled with ``-DGMX_USE_LMFIT=internal``. To use an
  external lmfit library, set ``-DGMX_USE_LMFIT=external``, and adjust
  ``CMAKE_PREFIX_PATH`` as needed.  lmfit support can be disabled with
  ``-DGMX_USE_LMFIT=none``.
* zlib is used by TNG for compressing some kinds of trajectory data
* Building the |Gromacs| documentation is optional, and requires
  ImageMagick, pdflatex, bibtex, doxygen, python 3.6, sphinx
  |EXPECTED_SPHINX_VERSION|, and pygments.
* The |Gromacs| utility programs often write data files in formats
  suitable for the Grace plotting tool, but it is straightforward to
  use these files in other plotting programs, too.
* Set ``-DGMX_PYTHON_PACKAGE=ON`` when configuring |Gromacs| with CMake to
  enable additional CMake targets for the gmxapi Python package and
  sample_restraint package from the main |Gromacs| CMake build. This supports
  additional testing and documentation generation.

Doing a build of |Gromacs|
--------------------------

This section will cover a general build of |Gromacs| with CMake_, but it
is not an exhaustive discussion of how to use CMake. There are many
resources available on the web, which we suggest you search for when
you encounter problems not covered here. The material below applies
specifically to builds on Unix-like systems, including Linux, and Mac
OS X. For other platforms, see the specialist instructions below.

.. _configure-cmake:

Configuring with CMake
^^^^^^^^^^^^^^^^^^^^^^

CMake will run many tests on your system and do its best to work out
how to build |Gromacs| for you. If your build machine is the same as
your target machine, then you can be sure that the defaults and
detection will be pretty good. However, if you want to control aspects
of the build, or you are compiling on a cluster head node for back-end
nodes with a different architecture, there are a few things you
should consider specifying.

The best way to use CMake to configure |Gromacs| is to do an
"out-of-source" build, by making another directory from which you will
run CMake. This can be outside the source directory, or a subdirectory
of it. It also means you can never corrupt your source code by trying
to build it! So, the only required argument on the CMake command line
is the name of the directory containing the ``CMakeLists.txt`` file of
the code you want to build. For example, download the source tarball
and use

.. parsed-literal::

    tar xfz gromacs-|version|.tgz
    cd gromacs-|version|
    mkdir build-gromacs
    cd build-gromacs
    cmake ..

You will see ``cmake`` report a sequence of results of tests and
detections done by the |Gromacs| build system. These are written to the
``cmake`` cache, kept in ``CMakeCache.txt``. You can edit this file by
hand, but this is not recommended because you could make a mistake.
You should not attempt to move or copy this file to do another build,
because file paths are hard-coded within it. If you mess things up,
just delete this file and start again with ``cmake``.

If there is a serious problem detected at this stage, then you will see
a fatal error and some suggestions for how to overcome it. If you are
not sure how to deal with that, please start by searching on the web
(most computer problems already have known solutions!) and then
consult the gmx-users mailing list. There are also informational
warnings that you might like to take on board or not. Piping the
output of ``cmake`` through ``less`` or ``tee`` can be
useful, too.

Once ``cmake`` returns, you can see all the settings that were chosen
and information about them by using e.g. the curses interface

::

    ccmake ..

You can actually use ``ccmake`` (available on most Unix platforms)
directly in the first step, but then
most of the status messages will merely blink in the lower part
of the terminal rather than be written to standard output. Most platforms
including Linux, Windows, and Mac OS X even have native graphical user interfaces for
``cmake``, and it can create project files for almost any build environment
you want (including Visual Studio or Xcode).
Check out `running CMake`_ for
general advice on what you are seeing and how to navigate and change
things. The settings you might normally want to change are already
presented. You may make changes, then re-configure (using ``c``), so that it
gets a chance to make changes that depend on yours and perform more
checking. It may take several configuration passes to reach the desired
configuration, in particular if you need to resolve errors.

When you have reached the desired configuration with ``ccmake``, the
build system can be generated by pressing ``g``.  This requires that the previous
configuration pass did not reveal any additional settings (if it did, you need
to configure once more with ``c``).  With ``cmake``, the build system is generated
after each pass that does not produce errors.

You cannot attempt to change compilers after the initial run of
``cmake``. If you need to change, clean up, and start again.

.. _non-standard location:

Where to install |Gromacs|
~~~~~~~~~~~~~~~~~~~~~~~~~~

|Gromacs| is installed in the directory to which
``CMAKE_INSTALL_PREFIX`` points. It may not be the source directory or
the build directory.  You require write permissions to this
directory. Thus, without super-user privileges,
``CMAKE_INSTALL_PREFIX`` will have to be within your home directory.
Even if you do have super-user privileges, you should use them only
for the installation phase, and never for configuring, building, or
running |Gromacs|!

.. _cmake options:

Using CMake command-line options
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Once you become comfortable with setting and changing options, you may
know in advance how you will configure |Gromacs|. If so, you can speed
things up by invoking ``cmake`` and passing the various options at once
on the command line. This can be done by setting cache variable at the
cmake invocation using ``-DOPTION=VALUE``. Note that some
environment variables are also taken into account, in particular
variables like ``CC`` and ``CXX``.

For example, the following command line

::

    cmake .. -DGMX_GPU=CUDA -DGMX_MPI=ON -DCMAKE_INSTALL_PREFIX=/home/marydoe/programs

can be used to build with CUDA GPUs, MPI and install in a custom
location. You can even save that in a shell script to make it even
easier next time. You can also do this kind of thing with ``ccmake``,
but you should avoid this, because the options set with ``-D`` will not
be able to be changed interactively in that run of ``ccmake``.

.. _gmx-simd-support:

SIMD support
~~~~~~~~~~~~

|Gromacs| has extensive support for detecting and using the SIMD
capabilities of many modern HPC CPU architectures. If you are building
|Gromacs| on the same hardware you will run it on, then you don't need
to read more about this, unless you are getting configuration warnings
you do not understand. By default, the |Gromacs| build system will
detect the SIMD instruction set supported by the CPU architecture (on
which the configuring is done), and thus pick the best
available SIMD parallelization supported by |Gromacs|. The build system
will also check that the compiler and linker used also support the
selected SIMD instruction set and issue a fatal error if they
do not.

Valid values are listed below, and the applicable value with the
largest number in the list is generally the one you should choose.
In most cases, choosing an inappropriate higher number will lead
to compiling a binary that will not run. However, on a number of
processor architectures choosing the highest supported value can
lead to performance loss, e.g. on Intel Skylake-X/SP and AMD Zen.

1. ``None`` For use only on an architecture either lacking SIMD,
   or to which |Gromacs| has not yet been ported and none of the
   options below are applicable.
2. ``SSE2`` This SIMD instruction set was introduced in Intel
   processors in 2001, and AMD in 2003. Essentially all x86
   machines in existence have this, so it might be a good choice if
   you need to support dinosaur x86 computers too.
3. ``SSE4.1`` Present in all Intel core processors since 2007,
   but notably not in AMD Magny-Cours. Still, almost all recent
   processors support this, so this can also be considered a good
   baseline if you are content with slow simulations and prefer
   portability between reasonably modern processors.
4. ``AVX_128_FMA`` AMD Bulldozer, Piledriver (and later Family 15h) processors
   have this but it is NOT supported on any AMD processors since Zen1.
5. ``AVX_256`` Intel processors since Sandy Bridge (2011). While this
   code will work on the  AMD Bulldozer and Piledriver processors, it is significantly less
   efficient than the ``AVX_128_FMA`` choice above - do not be fooled
   to assume that 256 is better than 128 in this case.
6. ``AVX2_128`` AMD Zen/Zen2 and Hygon Dhyana microarchitecture processors;
   it will enable AVX2 with 3-way fused multiply-add instructions.
   While these microarchitectures do support 256-bit AVX2 instructions,
   hence ``AVX2_256`` is also supported, 128-bit will generally be faster,
   in particular when the non-bonded tasks run on the CPU -- hence
   the default ``AVX2_128``. With GPU offload however ``AVX2_256``
   can be faster on Zen processors.
7. ``AVX2_256`` Present on Intel Haswell (and later) processors (2013),
   and it will also enable Intel 3-way fused multiply-add instructions.
8. ``AVX_512`` Skylake-X desktop and Skylake-SP Xeon processors (2017);
   it will generally be fastest on the higher-end desktop and server
   processors with two 512-bit fused multiply-add units (e.g. Core i9
   and Xeon Gold). However, certain desktop and server models
   (e.g. Xeon Bronze and Silver) come with only one AVX512 FMA unit
   and therefore on these processors ``AVX2_256`` is faster
   (compile- and runtime checks try to inform about such cases).
   Additionally, with GPU accelerated runs ``AVX2_256`` can also be
   faster on high-end Skylake CPUs with both 512-bit FMA units enabled.
9. ``AVX_512_KNL`` Knights Landing Xeon Phi processors
10. ``IBM_VSX`` Power7, Power8, Power9 and later have this.
11. ``ARM_NEON_ASIMD`` 64-bit ARMv8 and later.
12. ``ARM_SVE`` 64-bit ARMv8 and later with the Scalable Vector Extensions (SVE).
    The SVE vector length is fixed at CMake configure time. The default vector
    length is automatically detected, and this can be changed via the
    ``GMX_SIMD_ARM_SVE_LENGTH`` CMake variable.
    Minimum required compiler versions are GNU >= 10, LLVM >=13, or ARM >= 21.1. 
    For maximum performance we strongly suggest the latest gcc compilers,
    or at least LLVM 14 (when released) or ARM 22.0 (when released). 
    Lower performance has been observed with LLVM 13 and Arm compiler 21.1.

The CMake configure system will check that the compiler you have
chosen can target the architecture you have chosen. mdrun will check
further at runtime, so if in doubt, choose the lowest number you
think might work, and see what mdrun says. The configure system also
works around many known issues in many versions of common HPC
compilers.

A further ``GMX_SIMD=Reference`` option exists, which is a special
SIMD-like implementation written in plain C that developers can use
when developing support in |Gromacs| for new SIMD architectures. It is
not designed for use in production simulations, but if you are using
an architecture with SIMD support to which |Gromacs| has not yet been
ported, you may wish to try this option instead of the default
``GMX_SIMD=None``, as it can often out-perform this when the
auto-vectorization in your compiler does a good job. And post on the
|Gromacs| mailing lists, because |Gromacs| can probably be ported for new
SIMD architectures in a few days.

CMake advanced options
~~~~~~~~~~~~~~~~~~~~~~

The options that are displayed in the default view of ``ccmake`` are
ones that we think a reasonable number of users might want to consider
changing. There are a lot more options available, which you can see by
toggling the advanced mode in ``ccmake`` on and off with ``t``. Even
there, most of the variables that you might want to change have a
``CMAKE_`` or ``GMX_`` prefix. There are also some options that will be
visible or not according to whether their preconditions are satisfied.

.. _search for libraries, headers or programs:

Helping CMake find the right libraries, headers, or programs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If libraries are installed in non-default locations their location can
be specified using the following variables:

* ``CMAKE_INCLUDE_PATH`` for header files
* ``CMAKE_LIBRARY_PATH`` for libraries
* ``CMAKE_PREFIX_PATH`` for header, libraries and binaries
  (e.g. ``/usr/local``).

The respective ``include``, ``lib``, or ``bin`` is
appended to the path. For each of these variables, a list of paths can
be specified (on Unix, separated with ":"). These can be set as
environment variables like:

::

    CMAKE_PREFIX_PATH=/opt/fftw:/opt/cuda cmake ..

(assuming ``bash`` shell). Alternatively, these variables are also
``cmake`` options, so they can be set like
``-DCMAKE_PREFIX_PATH=/opt/fftw:/opt/cuda``.

The ``CC`` and ``CXX`` environment variables are also useful
for indicating to ``cmake`` which compilers to use. Similarly,
``CFLAGS``/``CXXFLAGS`` can be used to pass compiler
options, but note that these will be appended to those set by
|Gromacs| for your build platform and build type. You can customize
some of this with advanced CMake options such as ``CMAKE_C_FLAGS``
and its relatives.

See also the page on `CMake environment variables`_.

.. _CUDA GPU acceleration:

CUDA GPU acceleration
~~~~~~~~~~~~~~~~~~~~~

If you have the CUDA_ Toolkit installed, you can use ``cmake`` with:

::

    cmake .. -DGMX_GPU=CUDA -DCUDA_TOOLKIT_ROOT_DIR=/usr/local/cuda

(or whichever path has your installation). In some cases, you might
need to specify manually which of your C++ compilers should be used,
e.g. with the advanced option ``CUDA_HOST_COMPILER``.

By default, code will be generated for the most common CUDA architectures.
However, to reduce build time and binary size we do not generate code for
every single possible architecture, which in rare cases (say, Tegra systems)
can result in the default build not being able to use some GPUs.
If this happens, or if you want to remove some architectures to reduce
binary size and build time, you can alter the target CUDA architectures.
This can be done either with the ``GMX_CUDA_TARGET_SM`` or
``GMX_CUDA_TARGET_COMPUTE`` CMake variables, which take a semicolon delimited
string with the two digit suffixes of CUDA (virtual) architectures names, for
instance "35;50;51;52;53;60". For details, see the "Options for steering GPU
code generation" section of the nvcc man / help or Chapter 6. of the nvcc
manual.

The GPU acceleration has been tested on AMD64/x86-64 platforms with
Linux, Mac OS X and Windows operating systems, but Linux is the
best-tested and supported of these. Linux running on POWER 8 and ARM v8
CPUs also works well.

Experimental support is available for compiling CUDA code, both for host and
device, using clang (version 6.0 or later).
A CUDA toolkit is still required but it is used only for GPU device code
generation and to link against the CUDA runtime library.
The clang CUDA support simplifies compilation and provides benefits for development
(e.g. allows the use code sanitizers in CUDA host-code).
Additionally, using clang for both CPU and GPU compilation can be beneficial
to avoid compatibility issues between the GNU toolchain and the CUDA toolkit.
clang for CUDA can be triggered using the ``GMX_CLANG_CUDA=ON`` CMake option.
Target architectures can be selected with  ``GMX_CUDA_TARGET_SM``,
virtual architecture code is always embedded for all requested architectures
(hence GMX_CUDA_TARGET_COMPUTE is ignored).
Note that this is mainly a developer-oriented feature and it is not recommended
for production use as the performance can be significantly lower than that
of code compiled with nvcc (and it has also received less testing).
However, note that since clang 5.0 the performance gap is only moderate
(at the time of writing, about 20% slower GPU kernels), so this version
could be considered in non performance-critical use-cases.


OpenCL GPU acceleration
~~~~~~~~~~~~~~~~~~~~~~~

The primary targets of the |Gromacs| OpenCL support is accelerating
simulations on AMD and Intel hardware. For AMD, we target both
discrete GPUs and APUs (integrated CPU+GPU chips), and for Intel we
target the integrated GPUs found on modern workstation and mobile
hardware. The |Gromacs| OpenCL on NVIDIA GPUs works, but performance
and other limitations make it less practical (for details see the user guide).

To build |Gromacs| with OpenCL_ support enabled, two components are
required: the OpenCL_ headers and the wrapper library that acts
as a client driver loader (so-called ICD loader).
The additional, runtime-only dependency is the vendor-specific GPU driver
for the device targeted. This also contains the OpenCL_ compiler.
As the GPU compute kernels are compiled  on-demand at run time,
this vendor-specific compiler and driver is not needed for building |Gromacs|.
The former, compile-time dependencies are standard components,
hence stock versions can be obtained from most Linux distribution
repositories (e.g. ``opencl-headers`` and ``ocl-icd-libopencl1`` on Debian/Ubuntu).
Only the compatibility with the required OpenCL_ version |REQUIRED_OPENCL_MIN_VERSION|
needs to be ensured.
Alternatively, the headers and library can also be obtained from vendor SDKs,
which must be installed in a path found in ``CMAKE_PREFIX_PATH`` (or via the environment
variables ``AMDAPPSDKROOT`` or ``CUDA_PATH``).

To trigger an OpenCL_ build the following CMake flags must be set

::

    cmake .. -DGMX_GPU=OpenCL

To build with support for Intel integrated GPUs, it is required
to add ``-DGMX_GPU_NB_CLUSTER_SIZE=4`` to the cmake command line,
so that the GPU kernels match the characteristics of the hardware.
The `Neo driver <https://github.com/intel/compute-runtime/releases>`_
is recommended.

On Mac OS, an AMD GPU can be used only with OS version 10.10.4 and
higher; earlier OS versions are known to run incorrectly.

By default, any clFFT library on the system will be used with
|Gromacs|, but if none is found then the code will fall back on a
version bundled with |Gromacs|. To require |Gromacs| to link with an
external library, use

::

    cmake .. -DGMX_GPU=OpenCL -DclFFT_ROOT_DIR=/path/to/your/clFFT -DGMX_EXTERNAL_CLFFT=TRUE

SYCL GPU acceleration
~~~~~~~~~~~~~~~~~~~~~

SYCL_ is a modern portable heterogeneous acceleration API, with multiple
implementations targeting different hardware platforms (similar to OpenCL_).

Currently, supported platforms in |Gromacs| are:

* Intel GPUs using `Intel oneAPI DPC++`_ (both OpenCL and LevelZero backends), 
* AMD GPUs with hipSYCL_: only discrete GPUs with GFX9 (RX Vega 64, Pro VII, 
  Instinct MI25, Instinct MI50) and CDNA (Instinct MI100) architectures,
* NVIDIA GPUs (experimental) using either hipSYCL_ or open-source 
  `Intel LLVM <https://github.com/intel/llvm>`_.

Feature support is broader than that of the OpenCL, but not yet on par with CUDA.

The SYCL_ support in |Gromacs| is intended to eventually replace
OpenCL_ as an acceleration mechanism for AMD and Intel hardware.

Note: SYCL_ support in |Gromacs| is less mature than either OpenCL or CUDA.
Please, pay extra attention to simulation correctness when you are using it.

SYCL GPU acceleration for Intel GPUs
""""""""""""""""""""""""""""""""""""

You should install the recent `Intel oneAPI DPC++`_ compiler toolkit.
For |Gromacs| 2022, version 2021.4 is recommended.
Using open-source `Intel LLVM <https://github.com/intel/llvm>`_ is possible,
but not extensively tested. We also recommend installing the most recent
`Neo driver <https://github.com/intel/compute-runtime/releases>`_.

With the toolkit installed and added to the environment (usually by running
``source /opt/intel/oneapi/setvars.sh`` or using an appropriate
:command:`module load` on an HPC system), the following CMake flags
must be set:

::

   cmake .. -DCMAKE_C_COMPILER=icx -DCMAKE_CXX_COMPILER=icpx -DGMX_GPU=SYCL

SYCL GPU acceleration for AMD GPUs
""""""""""""""""""""""""""""""""""

Using the most recent hipSYCL_ ``develop`` branch and the most recent ROCm
release is recommended.

Additionally, we strongly recommend using the ROCm-bundled LLVM for building
both hipSYCL and |Gromacs|.

The following CMake command can be used **when configuring hipSYCL** to ensure
that the proper Clang is used (assuming ``ROCM_PATH``
is set correctly, e.g. to ``/opt/rocm`` in the case of default installation):

::

   cmake .. -DCMAKE_C_COMPILER=${ROCM_PATH}/llvm/bin/clang -DCMAKE_CXX_COMPILER=${ROCM_PATH}/llvm/bin/clang++ -DLLVM_DIR=${ROCM_PATH}/llvm/lib/cmake/llvm/

After compiling and installing hipSYCL, the following settings can be used for
building |Gromacs| itself (set ``HIPSYCL_TARGETS`` to the target hardware):

::

   cmake .. -DCMAKE_C_COMPILER=${ROCM_PATH}/llvm/bin/clang -DCMAKE_CXX_COMPILER=${ROCM_PATH}/llvm/bin/clang++ -DGMX_GPU=SYCL -DGMX_SYCL_HIPSYCL=ON -DHIPSYCL_TARGETS='hip:gfxXYZ'

SYCL GPU acceleration for NVIDIA GPUs
"""""""""""""""""""""""""""""""""""""

SYCL support for NVIDIA GPUs is highly experimental. For production, please use CUDA_
(`CUDA GPU acceleration`_). Note that FFT is not currently supported on NVIDIA devices 
when using SYCL, PME offload is only possible in mixed mode (``-pme gpu -pmefft cpu``).

NVIDIA GPUs can be used with either hipSYCL_ or the open-source
`Intel LLVM <https://github.com/intel/llvm>`_.

For hipSYCL, make sure that hipSYCL itself is compiled with CUDA support,
and supply proper devices via ``HIPSYCL_TARGETS`` (e.g., ``-DHIPSYCL_TARGETS=cuda:sm_75``).
When compiling for CUDA, we recommend using the mainline Clang, not the ROCm-bundled one.

For Intel LLVM, make sure it is compiled with CUDA and OpenMP support, then use
the following CMake invocation:

::

   cmake .. -DCMAKE_C_COMPILER=/path/to/intel/clang -DCMAKE_CXX_COMPILER=/path/to/intel/clang++ -DGMX_GPU=SYCL -DGMX_GPU_NB_CLUSTER_SIZE=8 -DSYCL_CXX_FLAGS_EXTRA=-fsycl-targets=nvptx64-nvidia-cuda


SYCL GPU compilation options
""""""""""""""""""""""""""""

The following flags can be passed to CMake in order to tune |Gromacs|:

``-DGMX_GPU_NB_CLUSTER_SIZE``
      changes the data layout of non-bonded kernels. Default values: 4 when
      compiling with `Intel oneAPI DPC++`_, 8 when compiling with hipSYCL_.
      Those are reasonable defaults for Intel and AMD devices, respectively.

``-DGMX_SYCL_USE_USM``
      switches between SYCL buffers (``OFF``) and USM (``ON``) for data management.
      Default: on (for performance reasons).

Static linking
~~~~~~~~~~~~~~

Dynamic linking of the |Gromacs| executables will lead to a
smaller disk footprint when installed, and so is the default on
platforms where we believe it has been tested repeatedly and found to work.
In general, this includes Linux, Windows, Mac OS X and BSD systems.
Static binaries take more space, but on some hardware and/or under
some conditions they are necessary, most commonly when you are running a parallel
simulation using MPI libraries (e.g. Cray).

* To link |Gromacs| binaries statically against the internal |Gromacs|
  libraries, set ``-DBUILD_SHARED_LIBS=OFF``.
* To link statically against external (non-system) libraries as well,
  set ``-DGMX_PREFER_STATIC_LIBS=ON``. Note, that in
  general ``cmake`` picks up whatever is available, so this option only
  instructs ``cmake`` to prefer static libraries when both static and
  shared are available. If no static version of an external library is
  available, even when the aforementioned option is ``ON``, the shared
  library will be used. Also note that the resulting binaries will
  still be dynamically linked against system libraries on platforms
  where that is the default. To use static system libraries,
  additional compiler/linker flags are necessary, e.g. ``-static-libgcc
  -static-libstdc++``.
* To attempt to link a fully static binary set
  ``-DGMX_BUILD_SHARED_EXE=OFF``. This will prevent CMake from explicitly
  setting any dynamic linking flags. This option also sets
  ``-DBUILD_SHARED_LIBS=OFF`` and ``-DGMX_PREFER_STATIC_LIBS=ON`` by
  default, but the above caveats apply. For compilers which don't
  default to static linking, the required flags have to be specified. On
  Linux, this is usually ``CFLAGS=-static CXXFLAGS=-static``.

gmxapi C++ API
~~~~~~~~~~~~~~

For dynamic linking builds and on non-Windows platforms, an extra library and
headers are installed by setting ``-DGMXAPI=ON`` (default).
Build targets ``gmxapi-cppdocs`` and ``gmxapi-cppdocs-dev`` produce documentation in
``docs/api-user`` and ``docs/api-dev``, respectively.
For more project information and use cases,
refer to the tracked :issue:`2585`,
associated GitHub `gmxapi <https://github.com/kassonlab/gmxapi>`_ projects,
or DOI `10.1093/bioinformatics/bty484 <https://doi.org/10.1093/bioinformatics/bty484>`_.

gmxapi is not yet tested on Windows or with static linking, but these use cases
are targeted for future versions.

Portability aspects
~~~~~~~~~~~~~~~~~~~

A |Gromacs| build will normally not be portable, not even across
hardware with the same base instruction set, like x86. Non-portable
hardware-specific optimizations are selected at configure-time, such
as the SIMD instruction set used in the compute kernels. This
selection will be done by the build system based on the capabilities
of the build host machine or otherwise specified to ``cmake`` during
configuration.

Often it is possible to ensure portability by choosing the least
common denominator of SIMD support, e.g. SSE2 for x86. In rare cases
of very old x86 machines, ensure that
you use ``cmake -DGMX_USE_RDTSCP=off`` if any of the target CPU
architectures does not support the ``RDTSCP`` instruction.  However, we
discourage attempts to use a single |Gromacs| installation when the
execution environment is heterogeneous, such as a mix of AVX and
earlier hardware, because this will lead to programs (especially
mdrun) that run slowly on the new hardware. Building two full
installations and locally managing how to call the correct one
(e.g. using a module system) is the recommended
approach. Alternatively, one can use different suffixes to install 
several versions of |Gromacs| in the same location. To achieve this,
one can first build a full installation with the
least-common-denominator SIMD instruction set, e.g. ``-DGMX_SIMD=SSE2``,
in order for simple commands like ``gmx grompp`` to work on all machines,
then build specialized ``gmx`` binaries for each architecture present in
the heterogeneous environment. By using custom binary and library
suffixes (with CMake variables ``-DGMX_BINARY_SUFFIX=xxx`` and
``-DGMX_LIBS_SUFFIX=xxx``), these can be installed to the same
location.

Linear algebra libraries
~~~~~~~~~~~~~~~~~~~~~~~~

As mentioned above, sometimes vendor BLAS and LAPACK libraries
can provide performance enhancements for |Gromacs| when doing
normal-mode analysis or covariance analysis. For simplicity, the text
below will refer only to BLAS, but the same options are available
for LAPACK. By default, CMake will search for BLAS, use it if it
is found, and otherwise fall back on a version of BLAS internal to
|Gromacs|. The ``cmake`` option ``-DGMX_EXTERNAL_BLAS=on`` will be set
accordingly. The internal versions are fine for normal use. If you
need to specify a non-standard path to search, use
``-DCMAKE_PREFIX_PATH=/path/to/search``. If you need to specify a
library with a non-standard name (e.g. ESSL on Power machines
or ARMPL on ARM machines), then
set ``-DGMX_BLAS_USER=/path/to/reach/lib/libwhatever.a``.

If you are using Intel MKL_ for FFT, then the BLAS and
LAPACK it provides are used automatically. This could be
over-ridden with ``GMX_BLAS_USER``, etc.

On Apple platforms where the Accelerate Framework is available, these
will be automatically used for BLAS and LAPACK. This could be
over-ridden with ``GMX_BLAS_USER``, etc.

.. _installing with MiMiC:

Building with MiMiC QM/MM support
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

MiMiC QM/MM interface integration will require linking against MiMiC
communication library, that establishes the communication channel
between |Gromacs| and CPMD. The MiMiC Communication library can be
downloaded `here <https://gitlab.com/MiMiC-projects/CommLib>`__.
Compile and install it. Check that the installation folder of the
MiMiC library is added to CMAKE_PREFIX_PATH if it is installed in
non-standard location. Building QM/MM-capable version requires
double-precision version of |Gromacs| compiled with MPI support:

* ``-DGMX_DOUBLE=ON -DGMX_MPI -DGMX_MIMIC=ON``

.. _installing with CP2K:

Building with CP2K QM/MM support
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

CP2K QM/MM interface integration will require linking against libcp2k
library, that incorporates CP2K functionality into |Gromacs|. 

1. Download, compile and install CP2K (version 8.1 or higher is required).
CP2K latest distribution can be downloaded `here <https://github.com/cp2k/cp2k/releases/>`_.
For CP2K specific instructions please `follow <https://github.com/cp2k/cp2k/blob/master/INSTALL.md>`_.
You can also check instructions on the `official CP2K web-page <https://www.cp2k.org/howto>`_.

2. Make :file:`libcp2k.a` library by executing the following command::
    make ARCH=<your arch file> VERSION=<your version like psmp> libcp2k

The library archive (*e.g.* :file:`libcp2k.a`) should appear in the :file:`{<cp2k dir>}/lib/{<arch>}/{<version>}/` directory.

3. Configure |Gromacs| with :command:`cmake`, adding the following flags.

Build should be static:
* ``-DBUILD_SHARED_LIBS=OFF -DGMXAPI=OFF -DGMX_INSTALL_NBLIB_API=OFF``

Double precision in general is better than single for QM/MM 
(however both options are viable):
* ``-DGMX_DOUBLE=ON``

FFT, BLAS and LAPACK libraries should be the same between CP2K and |Gromacs|.
Use the following flags to do so:

* ``-DGMX_FFT_LIBRARY=<your library like fftw3> -DFFTWF_LIBRARY=<path to library> -DFFTWF_INCLUDE_DIR=<path to directory with headers>``
* ``-DGMX_BLAS_USER=<path to your BLAS>`` 
* ``-DGMX_LAPACK_USER=<path to your LAPACK>``

4. Compilation of QM/MM interface is controled by the following flags.

``-DGMX_CP2K=ON``
    Activates QM/MM interface compilation
``-DCP2K_DIR="<path to cp2k>/lib/local/psmp``
    Directory with libcp2k.a library
``-DCP2K_LINKER_FLAGS="<combination of LDFLAGS and LIBS>"``
    Other libraries used by CP2K. Typically that should be combination 
    of LDFLAGS and LIBS from the ARCH file used for CP2K compilation.
    Sometimes ARCH file could have several lines defining LDFLAGS and LIBS
    or even split one line into several using "\". In that case all of them
    should be concatenated into one long string without any extra slashes 
    or quotes.

.. _suffixes:

Changing the names of |Gromacs| binaries and libraries
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

It is sometimes convenient to have different versions of the same
|Gromacs| programs installed. The most common use cases have been single
and double precision, and with and without MPI. This mechanism can
also be used to install side-by-side multiple versions of mdrun
optimized for different CPU architectures, as mentioned previously.

By default, |Gromacs| will suffix programs and libraries for such builds
with ``_d`` for double precision and/or ``_mpi`` for MPI (and nothing
otherwise). This can be controlled manually with ``GMX_DEFAULT_SUFFIX
(ON/OFF)``, ``GMX_BINARY_SUFFIX`` (takes a string) and ``GMX_LIBS_SUFFIX``
(also takes a string). For instance, to set a custom suffix for
programs and libraries, one might specify:

::

    cmake .. -DGMX_DEFAULT_SUFFIX=OFF -DGMX_BINARY_SUFFIX=_mod -DGMX_LIBS_SUFFIX=_mod

Thus the names of all programs and libraries will be appended with
``_mod``.

Changing installation tree structure
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, a few different directories under ``CMAKE_INSTALL_PREFIX`` are used
when when |Gromacs| is installed. Some of these can be changed, which is mainly
useful for packaging |Gromacs| for various distributions. The directories are
listed below, with additional notes about some of them. Unless otherwise noted,
the directories can be renamed by editing the installation paths in the main
CMakeLists.txt.

``bin/``
    The standard location for executables and some scripts.
    Some of the scripts hardcode the absolute installation prefix, which needs
    to be changed if the scripts are relocated.
    The name of the directory can be changed using ``CMAKE_INSTALL_BINDIR`` CMake
    variable.
``include/gromacs/``
    The standard location for installed headers.
``lib/``
    The standard location for libraries. The default depends on the system, and
    is determined by CMake.
    The name of the directory can be changed using ``CMAKE_INSTALL_LIBDIR`` CMake
    variable.
``lib/pkgconfig/``
    Information about the installed ``libgromacs`` library for ``pkg-config`` is
    installed here.  The ``lib/`` part adapts to the installation location of the
    libraries.  The installed files contain the installation prefix as absolute
    paths.
``share/cmake/``
    CMake package configuration files are installed here.
``share/gromacs/``
    Various data files and some documentation go here. The first part can
    be changed using ``CMAKE_INSTALL_DATADIR``, and the second by using
    ``GMX_INSTALL_DATASUBDIR`` Using these CMake variables is the preferred
    way of changing the installation path for
    ``share/gromacs/top/``, since the path to this directory is built into
    ``libgromacs`` as well as some scripts, both as a relative and as an absolute
    path (the latter as a fallback if everything else fails).
``share/man/``
    Installed man pages go here.

Compiling and linking
^^^^^^^^^^^^^^^^^^^^^

Once you have configured with ``cmake``, you can build |Gromacs| with ``make``.
It is expected that this will always complete successfully, and
give few or no warnings. The CMake-time tests |Gromacs| makes on the settings
you choose are pretty extensive, but there are probably a few cases we
have not thought of yet. Search the web first for solutions to
problems, but if you need help, ask on gmx-users, being sure to
provide as much information as possible about what you did, the system
you are building on, and what went wrong. This may mean scrolling back
a long way through the output of ``make`` to find the first error
message!

If you have a multi-core or multi-CPU machine with ``N``
processors, then using

::

    make -j N

will generally speed things up by quite a bit. Other build generator systems
supported by ``cmake`` (e.g. ``ninja``) also work well.

.. _building just the mdrun binary:

Installing |Gromacs|
^^^^^^^^^^^^^^^^^^^^

Finally, ``make install`` will install |Gromacs| in the
directory given in ``CMAKE_INSTALL_PREFIX``. If this is a system
directory, then you will need permission to write there, and you
should use super-user privileges only for ``make install`` and
not the whole procedure.

.. _getting access to |Gromacs|:

Getting access to |Gromacs| after installation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

|Gromacs| installs the script ``GMXRC`` in the ``bin``
subdirectory of the installation directory
(e.g. ``/usr/local/gromacs/bin/GMXRC``), which you should source
from your shell:

::

    source /your/installation/prefix/here/bin/GMXRC

It will detect what kind of shell you are running and set up your
environment for using |Gromacs|. You may wish to arrange for your
login scripts to do this automatically; please search the web for
instructions on how to do this for your shell.

Many of the |Gromacs| programs rely on data installed in the
``share/gromacs`` subdirectory of the installation directory. By
default, the programs will use the environment variables set in the
``GMXRC`` script, and if this is not available they will try to guess the
path based on their own location.  This usually works well unless you
change the names of directories inside the install tree. If you still
need to do that, you might want to recompile with the new install
location properly set, or edit the ``GMXRC`` script.

|Gromacs| also installs a CMake cache file to help with building client software
(using the `-C option <https://cmake.org/cmake/help/latest/manual/cmake.1.html#options>`__
when configuring the client software with CMake.)
For an installation at ``/your/installation/prefix/here``,
hints files will be installed at
``/your/installation/prefix/share/cmake/gromacs${GMX_LIBS_SUFFIX}/gromacs-hints${GMX_LIBS_SUFFIX}.cmake``
where ``${GMX_LIBS_SUFFIX}`` is :ref:`as documented above <suffixes>`.

Testing |Gromacs| for correctness
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Since 2011, the |Gromacs| development uses an automated system where
every new code change is subject to regression testing on a number of
platforms and software combinations. While this improves
reliability quite a lot, not everything is tested, and since we
increasingly rely on cutting edge compiler features there is
non-negligible risk that the default compiler on your system could
have bugs. We have tried our best to test and refuse to use known bad
versions in ``cmake``, but we strongly recommend that you run through
the tests yourself. It only takes a few minutes, after which you can
trust your build.

The simplest way to run the checks is to build |Gromacs| with
``-DREGRESSIONTEST_DOWNLOAD``, and run ``make check``.
|Gromacs| will automatically download and run the tests for you.
Alternatively, you can download and unpack the |Gromacs|
regression test suite |gmx-regressiontests-package| tarball yourself
and use the advanced ``cmake`` option ``REGRESSIONTEST_PATH`` to
specify the path to the unpacked tarball, which will then be used for
testing. If the above does not work, then please read on.

The regression tests are also available from the download_ section.
Once you have downloaded them, unpack the tarball, source
``GMXRC`` as described above, and run ``./gmxtest.pl all``
inside the regression tests folder. You can find more options
(e.g. adding ``double`` when using double precision, or
``-only expanded`` to run just the tests whose names match
"expanded") if you just execute the script without options.

Hopefully, you will get a report that all tests have passed. If there
are individual failed tests it could be a sign of a compiler bug, or
that a tolerance is just a tiny bit too tight. Check the output files
the script directs you too, and try a different or newer compiler if
the errors appear to be real. If you cannot get it to pass the
regression tests, you might try dropping a line to the
`GROMACS users forum <https://gromacs.bioexcel.eu/c/gromacs-user-forum>`__,
but then you should include a detailed description of
your hardware, and the output of ``gmx mdrun -version`` (which contains
valuable diagnostic information in the header).

Non-standard suffix
~~~~~~~~~~~~~~~~~~~

If your ``gmx`` program has been suffixed in a non-standard way, then
the ``./gmxtest.pl -suffix`` option will let you specify that suffix to the
test machinery. You can use ``./gmxtest.pl -double`` to test the
double-precision version. You can use ``./gmxtest.pl -crosscompiling``
to stop the test harness attempting to check that the programs can
be run. You can use ``./gmxtest.pl -mpirun srun`` if your command to
run an MPI program is called ``srun``.

Running MPI-enabled tests
~~~~~~~~~~~~~~~~~~~~~~~~~

The ``make check`` target also runs integration-style tests that may run
with MPI if ``GMX_MPI=ON`` was set. To make these work with various possible
MPI libraries, you may need to
set the CMake variables ``MPIEXEC``, ``MPIEXEC_NUMPROC_FLAG``,
``MPIEXEC_PREFLAGS`` and ``MPIEXEC_POSTFLAGS`` so that
``mdrun-mpi-test_mpi`` would run on multiple ranks via the shell command

::

    ${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} ${NUMPROC} ${MPIEXEC_PREFLAGS} \
          mdrun-mpi-test_mpi ${MPIEXEC_POSTFLAGS} -otherflags

A typical example for SLURM is

::

     cmake .. -DGMX_MPI=on -DMPIEXEC=srun -DMPIEXEC_NUMPROC_FLAG=-n -DMPIEXEC_PREFLAGS= -DMPIEXEC_POSTFLAGS=


Testing |Gromacs| for performance
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

We are still working on a set of benchmark systems for testing
the performance of |Gromacs|. Until that is ready, we recommend that
you try a few different parallelization options, and experiment with
tools such as ``gmx tune_pme``.

Having difficulty?
^^^^^^^^^^^^^^^^^^

You are not alone - this can be a complex task! If you encounter a
problem with installing |Gromacs|, then there are a number of
locations where you can find assistance. It is recommended that you
follow these steps to find the solution:

1. Read the installation instructions again, taking note that you
   have followed each and every step correctly.

2. Search the |Gromacs| webpage_ and users emailing list for information
   on the error. Adding
   ``site:https://mailman-1.sys.kth.se/pipermail/gromacs.org_gmx-users``
   to a Google search may help filter better results.

3. Search the internet using a search engine such as Google.

4. Post to the |Gromacs| users emailing list gmx-users for
   assistance. Be sure to give a full description of what you have
   done and why you think it did not work. Give details about the
   system on which you are installing.  Copy and paste your command
   line and as much of the output as you think might be relevant -
   certainly from the first indication of a problem. In particular,
   please try to include at least the header from the mdrun logfile,
   and preferably the entire file.  People who might volunteer to help
   you do not have time to ask you interactive detailed follow-up
   questions, so you will get an answer faster if you provide as much
   information as you think could possibly help. High quality bug
   reports tend to receive rapid high quality answers.

.. _gmx-special-build:

Special instructions for some platforms
---------------------------------------

Building on Windows
^^^^^^^^^^^^^^^^^^^

Building on Windows using native compilers is rather similar to
building on Unix, so please start by reading the above. Then, download
and unpack the |Gromacs| source archive. Make a folder in which to do
the out-of-source build of |Gromacs|. For example, make it within the
folder unpacked from the source archive, and call it ``build-gromacs``.

For CMake, you can either use the graphical user interface provided on
Windows, or you can use a command line shell with instructions similar
to the UNIX ones above. If you open a shell from within your IDE
(e.g. Microsoft Visual Studio), it will configure the environment for
you, but you might need to tweak this in order to get either a 32-bit
or 64-bit build environment. The latter provides the fastest
executable. If you use a normal Windows command shell, then you will
need to either set up the environment to find your compilers and
libraries yourself, or run the ``vcvarsall.bat`` batch script provided
by MSVC (just like sourcing a bash script under Unix).

With the graphical user interface, you will be asked about what
compilers to use at the initial configuration stage, and if you use
the command line they can be set in a similar way as under UNIX.

Unfortunately ``-DGMX_BUILD_OWN_FFTW=ON`` (see `Using FFTW`_) does not
work on Windows, because there is no supported way to build FFTW on
Windows. You can either build FFTW some other way (e.g. MinGW), or
use the built-in fftpack (which may be slow), or `using MKL`_.

For the build, you can either load the generated solutions file into
e.g. Visual Studio, or use the command line with ``cmake --build`` so
the right tools get used.

Building on Cray
^^^^^^^^^^^^^^^^

|Gromacs| builds mostly out of the box on modern Cray machines, but
you may need to specify the use of static binaries with
``-DGMX_BUILD_SHARED_EXE=off``, and you may need to set the F77
environmental variable to ``ftn`` when compiling FFTW.
The ARM ThunderX2 Cray XC50 machines differ only in that the recommended
compiler is the ARM HPC Compiler (``armclang``).


Building on Solaris
^^^^^^^^^^^^^^^^^^^

The built-in |Gromacs| processor detection does not work on Solaris,
so it is strongly recommended that you build |Gromacs| with
``-DGMX_HWLOC=on`` and ensure that the ``CMAKE_PREFIX_PATH`` includes
the path where the hwloc headers and libraries can be found. At least
version 1.11.8 of hwloc is recommended.

Oracle Developer Studio is not a currently supported compiler (and
does not currently compile |Gromacs| correctly, perhaps because the
thread-MPI atomics are incorrectly implemented in |Gromacs|).

Intel Xeon Phi
^^^^^^^^^^^^^^

Xeon Phi processors, hosted or self-hosted, are supported.
The Knights Landing-based Xeon Phi processors behave like standard x86 nodes,
but support a special SIMD instruction set. When cross-compiling for such nodes,
use the ``AVX_512_KNL`` SIMD flavor.
Knights Landing processors support so-called "clustering modes" which
allow reconfiguring the memory subsystem for lower latency. |Gromacs| can
benefit from the quadrant or SNC clustering modes.
Care needs to be taken to correctly pin threads. In particular, threads of
an MPI rank should not cross cluster and NUMA boundaries.
In addition to the main DRAM memory, Knights Landing has a high-bandwidth
stacked memory called MCDRAM. Using it offers performance benefits if
it is ensured that ``mdrun`` runs entirely from this memory; to do so
it is recommended that MCDRAM is configured in "Flat mode" and ``mdrun`` is
bound to the appropriate NUMA node (use e.g. ``numactl --membind 1`` with
quadrant clustering mode).


Tested platforms
----------------

While it is our best belief that |Gromacs| will build and run pretty
much everywhere, it is important that we tell you where we really know
it works because we have tested it.
Every commit in our git source code repository
is currently tested with a range of configuration options on x86 with
gcc versions including 7 and 11,
clang versions including 7 and 13,
CUDA versions 11.0 and 11.4.2,
and
a version of oneAPI containing Intel's clang-based compiler.
For this testing, we use Ubuntu 20.04 operating system.
Other compiler, library, and OS versions are tested less frequently.
For details, you can have a look at the
`continuous integration server used by GROMACS <https://gitlab.com/gromacs/gromacs/>`_,
which uses GitLab runner on a local k8s x86 cluster with NVIDIA,
AMD, and Intel GPU support.

We test irregularly on ARM v8, Fujitsu A64FX, Cray, Power9,
and other environments, and
with other compilers and compiler versions, too.

Support
-------

Please refer to the `manual <http://manual.gromacs.org/>`_ for documentation,
downloads, and release notes for any GROMACS release.

Visit the `user forums <http://forums.gromacs.org/>`_ for discussions and advice.

Report bugs at https://gitlab.com/gromacs/gromacs/-/issues

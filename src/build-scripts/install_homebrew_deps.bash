#!/usr/bin/env bash

# This script, which assumes it is runnign on a Mac OSX with Homebrew
# installed, does a "brew install" in all packages reasonably needed by
# OIIO.

if [[ `uname` != "Darwin" ]] ; then
    echo "Don't run this script unless you are on Mac OSX"
    exit 1
fi

if [[ `which brew` == "" ]] ; then
    echo "You need to install Homebrew before running this script."
    echo "See http://brew.sh"
    exit 1
fi


#brew update >/dev/null
echo ""
echo "Before my brew installs:"
brew list --versions

# All cases except for clang-format target, we need the dependencies.
brew install --display-times gcc ccache cmake ninja boost || true
brew link --overwrite gcc
brew unlink python@2.7 || true
brew unlink python@3.9 || true
brew unlink python@3.8 || true
brew link --overwrite --force python@${PYTHON_VERSION} || true
brew upgrade --display-times cmake || true
brew install --display-times libtiff ilmbase openexr opencolorio
brew install --display-times libpng giflib webp jpeg-turbo openjpeg
brew install --display-times freetype libraw dcmtk pybind11 numpy || true
brew install --display-times ffmpeg libsquish ptex || true
brew install --display-times libde265 libheif || true
brew install --display-times openvdb tbb || true
brew install --display-times opencv || true
brew install --display-times qt
brew install --display-times field3d || true
brew install --display-times meson || true

echo ""
echo "After brew installs:"
brew list --versions

# Needed on some systems
if [[ $PYTHON_VERSION != "2.7" ]] ; then
    pip3 install numpy
else
    pip install numpy
fi

# Set up paths. These will only affect the caller if this script is
# run with 'source' rather than in a separate shell.
export PATH=/usr/local/opt/qt5/bin:$PATH ;
export PATH=/usr/local/opt/python/libexec/bin:$PATH ;
export PYTHONPATH=/usr/local/lib/python${PYTHON_VERSION}/site-packages:$PYTHONPATH ;
export PATH=/usr/local/opt/llvm/bin:$PATH ;

# If field3d and hdf5 get even slightly out of sync, hdf5 will throw fits.
# This is unnecessary, so we disable the step to make CI more likely to
# pass in cases where they don't exactly match on the Travis/GH instances.
export HDF5_DISABLE_VERSION_CHECK=1

#!/usr/bin/env bash

# Utility script to download and build libheif
#
# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/OpenImageIO/oiio

# Exit the whole script if any command fails.
set -ex

# Repo and branch/tag/commit of libheif to download if we don't have it yet
LIBHEIF_REPO=${LIBHEIF_REPO:=https://github.com/strukturag/libheif.git}
LIBHEIF_VERSION=${LIBHEIF_VERSION:=v1.9.1}

# Where to put libheif repo source (default to the ext area)
LOCAL_DEPS_DIR=${LOCAL_DEPS_DIR:=${PWD}/ext}
LIBHEIF_SRC_DIR=${LIBHEIF_SRC_DIR:=${LOCAL_DEPS_DIR}/libheif}
LIBHEIF_BUILD_DIR=${LIBHEIF_BUILD_DIR:=${LIBHEIF_SRC_DIR}/build}
LIBHEIF_INSTALL_DIR=${LIBHEIF_INSTALL_DIR:=${LOCAL_DEPS_DIR}/dist}
#LIBHEIF_CONFIG_OPTS=${LIBHEIF_CONFIG_OPTS:=}

pwd
echo "libheif install dir will be: ${LIBHEIF_INSTALL_DIR}"

mkdir -p ./ext
pushd ./ext

# Clone libheif project from GitHub and build
if [[ ! -e ${LIBHEIF_SRC_DIR} ]] ; then
    echo "git clone ${LIBHEIF_REPO} ${LIBHEIF_SRC_DIR}"
    git clone ${LIBHEIF_REPO} ${LIBHEIF_SRC_DIR}
fi
cd ${LIBHEIF_SRC_DIR}
echo "git checkout ${LIBHEIF_VERSION} --force"
git checkout ${LIBHEIF_VERSION} --force

pushd ${LIBHEIF_SRC_DIR}/third-party
bash dav1d.cmd
PKG_CONFIG_PATH=${PWD}/dist/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH}
cd dav1d/build
make
popd

mkdir -p ${LIBHEIF_BUILD_DIR}
cd ${LIBHEIF_BUILD_DIR}
time cmake --config Release \
           -DCMAKE_BUILD_TYPE=Release \
           -DCMAKE_INSTALL_PREFIX=${LIBHEIF_INSTALL_DIR} \
           -DWITH_EXAMPLES=OFF \
           -DUSE_LOCAL_DAV1D=ON \
           ${LIBHEIF_CONFIG_OPTS} ..
time cmake --build . --config Release --target install

#ls -R ${LIBHEIF_INSTALL_DIR}
popd


# Set up paths. These will only affect the caller if this script is
# run with 'source' rather than in a separate shell.
export LIBHEIF_ROOT=$LIBHEIF_INSTALL_DIR


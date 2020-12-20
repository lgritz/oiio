#!/usr/bin/env bash

# Utility script to download and build libde265
#
# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/OpenImageIO/oiio

# Exit the whole script if any command fails.
set -ex

# Repo and branch/tag/commit of libde265 to download if we don't have it yet
LIBDE265_REPO=${LIBDE265_REPO:=https://github.com/strukturag/libde265.git}
LIBDE265_VERSION=${LIBDE265_VERSION:=v1.0.7}

# Where to put libde265 repo source (default to the ext area)
LIBDE265_SRC_DIR=${LIBDE265_SRC_DIR:=${PWD}/ext/libde265}
# Temp build area (default to a build/ subdir under source)
LIBDE265_BUILD_DIR=${LIBDE265_BUILD_DIR:=${LIBDE265_SRC_DIR}/build}
# Install area for libde265 (default to ext/dist)
LIBDE265_INSTALL_DIR=${LIBDE265_INSTALL_DIR:=${PWD}/ext/dist}
#LIBDE265_CONFIG_OPTS=${LIBDE265_CONFIG_OPTS:=}

pwd
echo "libde265 install dir will be: ${LIBDE265_INSTALL_DIR}"

mkdir -p ./ext
pushd ./ext

# Clone libde265 project from GitHub and build
if [[ ! -e ${LIBDE265_SRC_DIR} ]] ; then
    echo "git clone ${LIBDE265_REPO} ${LIBDE265_SRC_DIR}"
    git clone ${LIBDE265_REPO} ${LIBDE265_SRC_DIR}
fi
cd ${LIBDE265_SRC_DIR}
echo "git checkout ${LIBDE265_VERSION} --force"
git checkout ${LIBDE265_VERSION} --force

mkdir -p ${LIBDE265_BUILD_DIR}
cd ${LIBDE265_BUILD_DIR}
time cmake --config Release \
           -DCMAKE_BUILD_TYPE=Release \
           -DCMAKE_INSTALL_PREFIX=${LIBDE265_INSTALL_DIR} \
           -DWITH_EXAMPLES=OFF \
           ${LIBDE265_CONFIG_OPTS} ..
time cmake --build . --config Release --target install

#ls -R ${LIBDE265_INSTALL_DIR}
popd


# Set up paths. These will only affect the caller if this script is
# run with 'source' rather than in a separate shell.
export LIBDE265_ROOT=$LIBDE265_INSTALL_DIR


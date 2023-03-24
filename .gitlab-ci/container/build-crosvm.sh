#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -ex

git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"

CROSVM_VERSION=7784d7dbad81ce2e7ec448ec7e821728a2d4ae72
git clone --single-branch -b main --no-checkout https://chromium.googlesource.com/crosvm/crosvm /platform/crosvm
pushd /platform/crosvm
git checkout "$CROSVM_VERSION"
git submodule update --init

# Revert "minijail: remove MS_NOSYMFOLLOW def because ChromeOS is now on glibc 2.35."
# Debian Bullseye has only glibc 2.31
pushd third_party/minijail
git cherry-pick 0bbf93fdc58520525c85f311c176219bb1b4e96f
popd

VIRGLRENDERER_VERSION=f0e07bfd20ac6672643dfaf5e08318d8ec803750
rm -rf third_party/virglrenderer
git clone --single-branch -b master --no-checkout https://gitlab.freedesktop.org/virgl/virglrenderer.git third_party/virglrenderer
pushd third_party/virglrenderer
git checkout "$VIRGLRENDERER_VERSION"
meson build/ -Drender-server-worker=process -Dvenus-experimental=true $EXTRA_MESON_ARGS
ninja -C build install
popd

RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  bindgen \
  -j ${FDO_CI_CONCURRENT:-4} \
  --root /usr/local \
  --version 0.60.1 \
  $EXTRA_CARGO_ARGS

RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  -j ${FDO_CI_CONCURRENT:-4} \
  --locked \
  --features 'default-no-sandbox gpu x virgl_renderer virgl_renderer_next' \
  --path . \
  --root /usr/local \
  $EXTRA_CARGO_ARGS

popd

rm -rf /platform/crosvm

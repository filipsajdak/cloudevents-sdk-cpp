#!/usr/bin/env bash
# Installs the toolchain the performance job measures with.
#
# The CI job and bench/perf/Dockerfile both run this script, so a local
# reproduction in the container uses the same packages as the runner. An
# instruction count is only comparable with one taken by the same compiler,
# standard library and Valgrind.
#
# Three libraries come from pinned upstream sources instead of apt:
#   - Glaze and yyjson are not packaged for Ubuntu 24.04. yyjson is needed only
#     by codec_bench, whose wall times the job reports but never gates on.
#   - Ubuntu's rapidjson-dev is the 2016 v1.1.0 tag, whose document.h does not
#     compile with GCC 14. The commit is the one cmake/CeDependencies.cmake
#     fetches for the in-tree codec.
set -euo pipefail

glaze_version=8.4.0
yyjson_version=0.13.0
rapidjson_commit=24b5e7a8b27f42fa16b96fc70aade9106cf7102f

packages=(
  g++-14 cmake ninja-build valgrind binutils git ca-certificates python3
  nlohmann-json3-dev libboost-json-dev
  libbenchmark-dev libsimdjson-dev
)

sudo=""
if [ "$(id -u)" -ne 0 ]; then
  sudo=sudo
fi

# The hosted runner carries third-party apt sources this project does not use;
# when one of them is down, apt-get update fails and takes the job with it.
$sudo rm -f /etc/apt/sources.list.d/microsoft-prod.list
$sudo apt-get update
DEBIAN_FRONTEND=noninteractive $sudo apt-get install -y --no-install-recommends "${packages[@]}"

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
git clone --quiet --depth 1 --branch "v${glaze_version}" \
  https://github.com/stephenberry/glaze.git "$scratch/glaze"
$sudo cp -r "$scratch/glaze/include/glaze" /usr/local/include/
echo "glaze ${glaze_version} headers installed in /usr/local/include"

git init --quiet "$scratch/rapidjson"
git -C "$scratch/rapidjson" fetch --quiet --depth 1 \
  https://github.com/Tencent/rapidjson.git "$rapidjson_commit"
git -C "$scratch/rapidjson" checkout --quiet FETCH_HEAD
$sudo cp -r "$scratch/rapidjson/include/rapidjson" /usr/local/include/
echo "rapidjson ${rapidjson_commit} headers installed in /usr/local/include"

git clone --quiet --depth 1 --branch "${yyjson_version}" \
  https://github.com/ibireme/yyjson.git "$scratch/yyjson"
cmake -S "$scratch/yyjson" -B "$scratch/yyjson-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-14 >/dev/null
cmake --build "$scratch/yyjson-build" >/dev/null
$sudo cmake --install "$scratch/yyjson-build" >/dev/null
echo "yyjson ${yyjson_version} installed in /usr/local"

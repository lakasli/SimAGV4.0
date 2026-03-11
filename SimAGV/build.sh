#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${root_dir}/build"

if [[ "${1:-}" == "clean" ]]; then
  rm -rf "${build_dir}"
  exit 0
fi

runtime_name="${SIMAGV_RUNTIME_NAME:-SimAGV}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --name)
      runtime_name="${2:-}"
      shift 2
      ;;
    --name=*)
      runtime_name="${1#--name=}"
      shift
      ;;
    *)
      shift
      ;;
  esac
done
runtime_name="$(printf '%s' "${runtime_name}" | tr -d '[:space:]')"
if [[ -z "${runtime_name}" ]]; then
  runtime_name="SimAGV"
fi

mkdir -p "${build_dir}/obj"

cxx="${CXX:-g++}"
common_flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread)
include_flags=(-I"${root_dir}")

runtime_main="${root_dir}/entrance_layer/sim_instance_startup.cpp"

mapfile -t all_cpp < <(find "${root_dir}" -type f -name '*.cpp' | sort)

lib_cpp=()
for src in "${all_cpp[@]}"; do
  if [[ "${src}" == "${runtime_main}" ]]; then
    continue
  fi
  if [[ "${src}" == *"_test.cpp" ]]; then
    continue
  fi
  lib_cpp+=("${src}")
done

compile_one() {
  local src="$1"
  local obj_rel
  obj_rel="$(realpath --relative-to="${root_dir}" "${src}")"
  obj_rel="${obj_rel//\//_}"
  local obj="${build_dir}/obj/${obj_rel%.cpp}.o"
  "${cxx}" "${common_flags[@]}" "${include_flags[@]}" -c "${src}" -o "${obj}"
  printf '%s\n' "${obj}"
}

lib_objs=()
for src in "${lib_cpp[@]}"; do
  lib_objs+=("$(compile_one "${src}")")
done

runtime_obj="$(compile_one "${runtime_main}")"
runtime_bin="${build_dir}/${runtime_name}"
"${cxx}" "${common_flags[@]}" -o "${runtime_bin}" "${runtime_obj}" "${lib_objs[@]}"

printf 'OK %s\n' "${runtime_bin}"

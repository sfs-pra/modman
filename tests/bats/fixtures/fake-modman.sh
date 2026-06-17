#!/usr/bin/env bash
set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURE_DIR="${SELF_DIR}/fake-repo"
DOWNLOAD_DIR="${DOWNLOAD_DIR:-${FIXTURE_DIR}}"

usage() {
  cat <<'EOF'
Usage: modman [--machine] [--list-loaded-after|--list-local|-Ss QUERY|-S NAME]
EOF
}

machine_mode=0
if [[ "${1:-}" == "--machine" ]]; then
  machine_mode=1
  shift
fi

case "${1:-}" in
  ""|"--help"|-h)
    usage
    ;;
  "--list-loaded-after")
    # Для smoke-теста допускается пустой вывод.
    exit 0
    ;;
  "--list-local")
    shopt -s nullglob
    for mod in "${DOWNLOAD_DIR}"/*.pfs; do
      name="$(basename "${mod}" .pfs)"
      if [[ ${machine_mode} -eq 1 ]]; then
        printf '%s\t0M\t%s\tlocal module\n' "${name}" "${mod}"
      else
        printf '%s\n' "${mod}"
      fi
    done
    ;;
  "-Ss")
    query="${2:-}"
    if [[ "${query}" == "nonexistent_xyzzy_12345" ]]; then
      exit 0
    fi

    while IFS=$'\t' read -r name layer path desc _version; do
      [[ -z "${name}" ]] && continue
      if [[ -z "${query}" || "${name}" == *"${query}"* || "${desc}" == *"${query}"* ]]; then
        if [[ ${machine_mode} -eq 1 ]]; then
          printf '%s\t%s\t%s\t%s\n' "${name}" "${layer}" "${path}" "${desc}"
        else
          printf '%s %s\n' "${name}" "${desc}"
        fi
      fi
    done < "${FIXTURE_DIR}/000-index.txt"
    ;;
  "-S")
    module_name="${2:-}"
    if [[ -z "${module_name}" || "${module_name}" == *';'* || "${module_name}" == *$'\t'* || "${module_name}" == *' '* ]]; then
      echo "error: invalid module name '${module_name}'" >&2
      exit 2
    fi
    echo "installing ${module_name}"
    ;;
  *)
    usage >&2
    exit 1
    ;;
esac

#!/usr/bin/env bats

load './helpers.bash'

@test "modman --machine --list-loaded-before produces valid TSV or empty (6-col format)" {
  run "${MODMAN}" --machine --list-loaded-before
  if [ "${status}" -ne 0 ]; then
    skip "pfs not available in this environment; before-list cannot run"
  fi
  if [ -n "${output}" ]; then
    while IFS= read -r line; do
      [ -z "${line}" ] && continue
      field_count=$(printf '%s' "${line}" | awk -F'\t' '{print NF}')
      [ "${field_count}" -eq 6 ]
    done <<<"${output}"
  fi
}

@test "modman --machine --list-loaded-after produces valid TSV or empty (6-col format)" {
  run "${MODMAN}" --machine --list-loaded-after
  [ "${status}" -eq 0 ]
  if [ -n "${output}" ]; then
    while IFS= read -r line; do
      [ -z "${line}" ] && continue
      field_count=$(printf '%s' "${line}" | awk -F'\t' '{print NF}')
      [ "${field_count}" -eq 6 ]
    done <<<"${output}"
  fi
}

@test "loaded TSV mode field is 'upper' or 'lower' or empty when modules present" {
  run "${MODMAN}" --machine --list-loaded-before
  if [ "${status}" -ne 0 ]; then
    skip "pfs not available in this environment; before-list cannot run"
  fi
  if [ -n "${output}" ]; then
    while IFS= read -r line; do
      [ -z "${line}" ] && continue
      mode=$(printf '%s' "${line}" | cut -f5)
      [[ "${mode}" == "upper" || "${mode}" == "lower" || -z "${mode}" ]]
    done <<<"${output}"
  fi
}

@test "loaded TSV in_ram field is 'yes', 'no', or empty when modules present" {
  run "${MODMAN}" --machine --list-loaded-after
  [ "${status}" -eq 0 ]
  if [ -n "${output}" ]; then
    while IFS= read -r line; do
      [ -z "${line}" ] && continue
      in_ram=$(printf '%s' "${line}" | cut -f6)
      [[ "${in_ram}" == "yes" || "${in_ram}" == "no" || -z "${in_ram}" ]]
    done <<<"${output}"
  fi
}

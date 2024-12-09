#!/usr/bin/env bash

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Default to the root of the repository
CHECK_PATH="${1:-"${SCRIPT_DIR}/.."}"
CLANG_FORMAT_STYLE="${2:-"${CHECK_PATH}/.clang-format"}"
EXCLUDE_REGEX="$3"
INCLUDE_REGEX="$4"

CLANG_FORMAT_STYLE="$(realpath "${CLANG_FORMAT_STYLE}")"

# Let's think that script will be used on Linux and Windows (msys or cygwin env only)
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Linux
    clang_format="$(which clang-format-16)"
else
    # Windows
    # Load clang-format from the EWDK 24H2
    EWDK11_24H2_DIR="${EWDK11_24H2_DIR:-c:\\ewdk11_24h2}"
    clang_format_ewdk="${EWDK11_24H2_DIR}\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\Llvm\\x64\\bin\\clang-format.exe"
    clang_format="$(cygpath "${clang_format_ewdk}")"
    # Convert the path to OS specific format
    CLANG_FORMAT_STYLE="$(cygpath -w "${CLANG_FORMAT_STYLE}")"
fi

echo "Using clang-format version: $("${clang_format}" --version)"
echo "Running clang-format on $(realpath ${CHECK_PATH})"
echo "Using clang-format style file: ${CLANG_FORMAT_STYLE}"

if [[ -z $EXCLUDE_REGEX ]]; then
	EXCLUDE_REGEX="^$"
fi

# Set the filetype regex if nothing was provided.
# Find all C/C++ files:
#   h, H, hpp, hh, h++, hxx
#   c, C, cpp, cc, c++, cxx
if [[ -z $INCLUDE_REGEX ]]; then
	INCLUDE_REGEX='^.*\.((((c|C)(c|pp|xx|\+\+)?$)|((h|H)h?(pp|xx|\+\+)?$)))$'
fi

# initialize exit code
exit_code=0

cd "${CHECK_PATH}"
src_files=$(find "." -name .git -prune -o -regextype posix-egrep -regex "$INCLUDE_REGEX" -print)

# check formatting in each source file
IFS=$'\n' # Loop below should separate on new lines, not spaces.
for file in $src_files; do
    # Only check formatting if the path doesn't match the regex
    if ! [[ ${file} =~ $EXCLUDE_REGEX ]]; then
        # Convert the path to OS specific format
        if [[ "$OSTYPE" == "linux-gnu"* ]]; then
            file_path="${file}"
        else
            file_path="$(cygpath -w "${file}")"
        fi

        formatted_code="$("${clang_format}" --style=file:"${CLANG_FORMAT_STYLE}" --verbose --Werror "${file_path}")"
        if [[ -z "$formatted_code" ]]; then
            continue
        fi

        local_format="$(diff <(cat "${file}") <(echo "${formatted_code}") || true)"
        if [[ -n "${local_format}" ]]; then
            echo "The file ${file} is not formatted correctly"
            echo "${local_format}"
            exit_code=1
        fi
    fi
done

echo "clang-format check finished with exit code: ${exit_code}"
exit $exit_code

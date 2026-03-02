#!/usr/bin/env bash
#
# Check that all commits contain a Signed-off-by line.

set -euo pipefail

SIGNOFF_PATTERN='^Signed-off-by: [^<]+ <[^@>]+@[^@>]+>$'

if [[ -z "${BASE_SHA:-}" ]] || [[ -z "${HEAD_SHA:-}" ]]; then
    echo "ERROR: BASE_SHA and HEAD_SHA are required"
    exit 1
fi
BASE_REF="${BASE_SHA}"

echo "Checking commits for Signed-off-by line..."
echo "Base: ${BASE_REF}"
echo "Head: ${HEAD_SHA}"

# Get all commits in the PR
commits=$(git rev-list --reverse "${BASE_REF}..${HEAD_SHA}")

if [[ -z "${commits}" ]]; then
    echo "No commits found"
    exit 0
fi

missing_signoff=()

# Check each commit for Signed-off-by line
while IFS= read -r commit; do
    if [[ -z "${commit}" ]]; then
        continue
    fi
    
    commit_msg=$(git log -1 --format=%B "${commit}")
    commit_subject=$(git log -1 --format=%s "${commit}")
    
    if ! echo "${commit_msg}" | grep -qE "${SIGNOFF_PATTERN}"; then
        echo "ERROR: Missing Signed-off-by in commit ${commit}"
        echo "       ${commit_subject}"
        missing_signoff+=("${commit}")
    else
        echo "PASS: ${commit}"
    fi
done <<< "${commits}"

echo ""
if [[ ${#missing_signoff[@]} -eq 0 ]]; then
    echo "All commits have valid Signed-off-by"
    exit 0
else
    echo "Found ${#missing_signoff[@]} commit(s) without Signed-off-by"
    exit 1
fi

#!/usr/bin/env bash
#
# Check that all commits contain a Signed-off-by line.
# When any commit has Signed-off-by from Red Hat (@redhat.com), require
# a Jira-style reference (e.g. RHEL-123456) in the PR title OR in each Red Hat-signed commit's subject.

set -euo pipefail

SIGNOFF_PATTERN='^Signed-off-by: [^<]+ <[^@>]+@[^@>]+>$'
JIRA_PATTERN='^[A-Za-z]+-[0-9]+'
REDHAT_SIGNOFF_PATTERN='Signed-off-by:.*@redhat\.com>'

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
has_redhat_signoff=0
missing_jira_in_subject=()

# Check each commit for Signed-off-by line, Red Hat signoff, and Jira ref in subject
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
        echo "PASS: ${commit} - ${commit_subject}"
    fi

    if echo "${commit_msg}" | grep -qE "${REDHAT_SIGNOFF_PATTERN}"; then
        has_redhat_signoff=1
        if ! echo "${commit_subject}" | grep -qE "${JIRA_PATTERN}"; then
            missing_jira_in_subject+=("${commit}")
        fi
    fi
done <<< "${commits}"

echo ""
if [[ ${#missing_signoff[@]} -ne 0 ]]; then
    echo "Found ${#missing_signoff[@]} commit(s) without Signed-off-by"
    exit 1
fi

# When Red Hat signed off, require Jira ref in PR title OR in each Red Hat-signed commit's subject.
if [[ ${has_redhat_signoff} -eq 1 ]]; then
    echo "PR title: ${PR_TITLE:-<empty>}"
    pr_title_ok=0
    if [[ -n "${PR_TITLE:-}" ]] && echo "${PR_TITLE}" | grep -qE "${JIRA_PATTERN}"; then
        pr_title_ok=1
    fi
    commits_ok=0
    if [[ ${#missing_jira_in_subject[@]} -eq 0 ]]; then
        commits_ok=1
    fi
    if [[ ${pr_title_ok} -eq 1 ]] || [[ ${commits_ok} -eq 1 ]]; then
        if [[ ${pr_title_ok} -eq 1 ]] && [[ ${commits_ok} -eq 1 ]]; then
            echo "PASS: PR title and Red Hat-signed commit subjects contain Jira reference (PR title: ${PR_TITLE})"
        elif [[ ${pr_title_ok} -eq 1 ]]; then
            echo "PASS: PR title contains Jira reference (PR title: ${PR_TITLE})"
        else
            echo "PASS: All Red Hat-signed commit subjects contain Jira reference"
        fi
    else
        if [[ -z "${PR_TITLE:-}" ]] || ! echo "${PR_TITLE}" | grep -qE "${JIRA_PATTERN}"; then
            echo "ERROR: PR title does not start with a Jira reference (e.g. RHEL-12345)"
        fi
        if [[ ${#missing_jira_in_subject[@]} -ne 0 ]]; then
            for c in "${missing_jira_in_subject[@]}"; do
                subj=$(git log -1 --format=%s "${c}")
                echo "ERROR: Red Hat-signed commit subject does not start with Jira reference: ${c} - ${subj}"
            done
        fi
        echo "ERROR: When Red Hat signs off, provide a Jira reference in the PR title OR in each Red Hat-signed commit subject"
        exit 1
    fi
fi

echo "All commits have valid Signed-off-by"
exit 0

# Contributing to virtio-win

We love your input! We want to make contributing to this project as easy and transparent as possible, whether it's:

- Reporting a bug
- Discussing the current state of the code
- Submitting a fix
- Proposing new features

## We Develop with Github
We use GitHub to host code, to track issues and feature requests, as well as accept pull requests.

## We Use [Github Flow](https://guides.github.com/introduction/flow/index.html), So All Code Changes Happen Through Pull Requests
Pull requests are the best way to propose changes to the codebase (we use [Github Flow](https://guides.github.com/introduction/flow/index.html)). We actively welcome your pull requests:

1. Fork the repo and create your branch from `master`.
2. If you've added code that should be tested, add tests.
3. If you've added new driver, changed usage, or made some nontrivial changes - update the documentation.
4. Ensure the test suite passes.
5. Don't forget to add "Signed-off-by: Your Name <your@email_domain.com>" line in the commit message, e.g. with `git commit -s`
6. If you are a Red Hat contributor, you MUST include the [Jira](https://issues.redhat.com/) key in the commit message.
7. If you are NOT a Red Hat contributor, but know of a relevant Jira key, please include it in the commit message.
8. Push your local commits to the remote branch, e.g. with `git push --branches --verbose`
9. Create that pull request!
<details>
<summary><ins>Examples of including the Jira key in the commit message</ins></summary>
<br>

Prefix commit messages with the Jira key first, followed by a reference to the relevant component, followed by a short description, e.g.:
- RHELMISC-8923: NetKVM: Implementing dynamic NDIS version support
- RHELMISC-8923: [NetKVM] Implementing dynamic NDIS version support

These can be issued from the command line using the following syntax:
```
git commit -s -m "RHELMISC-8923: [NetKVM] Implementing dynamic NDIS version support" -m "First line body content\nMore body content"
```

...or manually in the system editor, using:
```
git commit -s -e
```

...or use the CLI multiline editor, e.g:
```
git commit -s -m "RHELMISC-8923: [NetKVM] Implementing dynamic NDIS version support
First line body content
More body content"
```

In the examples above, the `-s` parameter causes `git` to include the _Signed-off-by_ signature in the commit message.
</details>


## Any contributions you make will be under the BSD 3-Clause License
In short, when you submit code changes, your submissions are understood to be under the same [BSD 3-Clause License](https://github.com/virtio-win/kvm-guest-drivers-windows/blob/master/LICENSE) that covers the project. Feel free to contact the maintainers if that's a concern.

## Report bugs using Github's [issues](https://github.com/virtio-win/kvm-guest-drivers-windows/issues)
We use GitHub issues to track public bugs. Report a bug by [opening a new issue](https://github.com/virtio-win/kvm-guest-drivers-windows/issues/new); it's that easy!

## Write bug reports with detail, background, and sample code
**Great Bug Reports** tend to have:

- A quick summary and/or background
- Steps to reproduce
  - Be specific!
  - Give sample code if you can.
- Driver version or commit hash that was used to build the driver
- QEMU command line
- What you expected would happen
- What actually happens
- Notes (possibly including why you think this might be happening, or stuff you tried that didn't work)

People *love* thorough bug reports.

## Use a Consistent Coding Style

* We use clang-format tool to check code style.
* In project we have two different code styles:
   - Style config file for Windows driver: `/.clang-format`
   - Style config file for VirtIO library: `/VirtIO/.clang-format`
* To run code style check locally on Linux or Windows (with MSYS or cygwin) use the `Tools/clang-format-helper.sh` helper
   - on Linux, the helper uses `clang-format-16` from PATH (because EWDK Win11 24H2 contains clang-format version 16.0.5)
   - on Windows, the helper uses `clang-format` from EWDK Win11 24H2

* Usage for clang-format helper tool:
   ```bash
   bash Tools/clang-format-helper.sh <check|format> <target directory> <path to .clang-format file> <exclude regexp> <include regexp>
   ```

   <details>
   <summary>More information on positional arguments and defaults...</summary>
   <br>

   Positional parameters:

   * 1\. Action: `check` or `format`. Required.
   * 2\. Directory to perform action. Required.
   * 3\. Path to .clang-format file (default: `${2}/.clang-format`)
   * 4\. Exclude regexp (default: `^$`)
   * 5\. Include regexp (default: `^.*\.((((c|C)(c|pp|xx|\+\+)?$)|((h|H)h?(pp|xx|\+\+)?$)))$`)
   
     Note: to use a default parameter use two single quotes, i.e. `''`
   </details>

   <details>
   <summary>Usage Examples</summary>
   <br>

   For all Windows drivers:
   
   ```bash
   bash Tools/clang-format-helper.sh check '.' '' './VirtIO'
   ```
   For `vioscsi` driver:
   
   ```bash
   bash Tools/clang-format-helper.sh check './vioscsi' './.clang-format' '.*/*trace.h|.*/wpp_.*_path.*.h' ''
   ```
   For `VirtIO` library:
   
   ```bash
   bash Tools/clang-format-helper.sh check 'VirtIO' '' ''
   ```
   </details>

## HCK\HLK tests
* The contributions should pass Microsoft certification tests. We are running CI to check that the changes in the pull request can pass. If you submit a lot of PRs, you can setup AutoHCK on your premises to test your code changes: [auto-hck setup](https://github.com/HCK-CI/HCK-CI-DOCS/blob/master/installing-hck-ci-from-scratch.txt)

## License
By contributing, you agree that your contributions will be licensed under BSD 3-Clause License.

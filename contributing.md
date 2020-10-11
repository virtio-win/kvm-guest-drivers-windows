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
5. Don't forget to add "Signed-off-by: Your Name <your@email_domain.com>" line in the commit message.
6. If you are a Red Hat contributor, you must include [BZ](https://bugzilla.redhat.com) number in the commit message
7. Prefix commit messages with the affected component. For example: "NetKVM: BZ#1234567: implementing dynamic NDIS version support".
8. Issue that pull request!


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
* 4 spaces for indentation rather than tabs

## HCK\HLK tests
* The contributions should pass Microsoft certification tests. We are running CI to check that the changes in the pull request can pass. If you submit a lot of PRs, you can setup AutoHCK on your premises to test your code changes: [auto-hck setup](https://github.com/HCK-CI/HCK-CI-DOCS/blob/master/installing-hck-ci-from-scratch.txt)

## License
By contributing, you agree that your contributions will be licensed under BSD 3-Clause License.

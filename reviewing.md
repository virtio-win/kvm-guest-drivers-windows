# Guidelines for the reviewers of the pull requests

## Our project philosophy

* The code base should be as stable as possible in the master branch. Many community members rely on the upstream drivers for their workloads
* All proposed changes must pass WHQL certification
* The usage of undocumented MS kernel DDI is discouraged

Exceptions: New drivers that require a lot of work (examples: virtio-vsock, 3D support as long as the miniport drier will be separated from the production virtio-gpu DOD miniport driver ). Also, branches can be used to submit experimental changes pending the maintainers' decision.

## Preliminary discussion of the changes

* Discuss every PR that might have potential to break stability of the master branch
* Workaround should be discussed in larger maintainers quorum
* Use community Slack channel or GitHub issues for the communication
* Please send email to yvugenfi (@) redhat.com to join the Slack channel

## General guidelines

* Check that CI passed
 - Help identify CI failures if needed
* PR should be submitted with commits that are as small as possible and have logical separation of changes
 - Reject huge single commit as a rule of thumb
* Cosmetic changes should be in separate commits
* Please keep constructive, respectful, and welcoming communication. Focus on the code, not the person

## Code specific considerations

* Pay attention to driver isolation requirements: https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/driver-isolation
 - Be extra careful if non-kernel components are added.
* Comments for "dev" verification
 - Use driver verifier. Unload the driver at the end of the test to trigger verifier memory leak verification.
* No asserts in "Release" builds!
* User space components must be statically linked
* PRs that are fixing regressions should be reverted first. Next PR can introduce improvements.
* The allocation of the memory that is passed to the host must use DMA API
 - Exception: inflated Balloon memory
* Check that kernel DDI is used on the appropriate IRQL level
* Check for potential memory leaks
* Future maintenance considerations, such as too much internal knowledge for specific features
* Check virtio spec compliance where appropriate
* As virtio HW devices are emerging and the spec does not cover some corner cases, always suggest to submit spec changes and take into consideration how current change will work out with future spec change
* Codying style is enforced by the CI

## Testing the build

* The CI runs only batch file build. It is encoraged to test that working with Visual Studio is not broken
* For the changes that are touching build infrastructure and Visual Studio projects, check that:
 - Can build from individual project
 - Can build from the buildall.bat file
 - Can build from the main virtio-win.sln
 - Verify that code is buildable for every platform x86, x64, ARM64
 - Note: current build CI only executes main buildall.bat

## Datapath considerations

* Check that the change is necessary
* Pay extra attention to locking
* Debug printing in data-path should be minimal and at the high debug level only
* It is highly recommended to discuss data path changes first in the community Slack
* Data path changes must be "dev" verified by the reviewer

## Preventing spam

* Be extra cautious towards accepting superfluous patches from people that are not active contributors to the project, such as:
 - Spelling fixes
 - Needless refactoring

##  Copyright considerations

* Check that commits have signed-off-by line: "Signed-off-by: <name> <email>"
* Please use the real name in the signed-off-by line
* Code licensing considerations. The project license is BSD. Be careful not to contaminate the project with GPL-licensed code
* New files must have copyright blobs

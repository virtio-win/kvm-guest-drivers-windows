Files in this directory used during build procedure for:

- creating of CAT files for driver installation package
- copying driver's installation package to NetKvm\Install
- signing  driver's installation package with development certificate
- creating ISO image for installation disk to be used during
   development and testing process (no utility provided)

Notes:
Put binary of one of known utilities into this folder or
  edit makecdimage.cmd to use another utility you like,
  see makecdimage.cmd for details
NetKVMTemporaryCert.pfx is temporary development certificate
  for signing drivers during development and debugging
NetKVMTemporaryCert.cer is temporary development certificate
  to be installed on target machine to allow installation
  of driver's package under development/debugging before
  it obtains official signature.
  Depending on specific target operating system installation
  of the driver's package, signed by developper, may
  require setting test signing mode on the target system.
NetKvmInstall.exe is (optional) installer of the driver.
  In order to have it on the installation disk,
  you need to build NetKvm\Installer\Package project
  using "buildAll.bat installer"
makeinstall.bat is called from buildAll.bat after
  after binary files are linked. It calls signing.cmd
  to create CAT file and sign package



IMPORTANT: Due to kernel.org restrictions the prepared certificates
are not part of the source tree. In order to sign the driver, such certificates
should be prepared. See http://msdn.microsoft.com/en-us/library/bfsktky3(VS.80).aspx
for more info on certificate creation.


@echo off
for %%D in (VirtIO NetKVM viostor vioscsi Balloon vioserial viorng vioinput pvpanic pciserial fwcfg packaging Q35) do (
  pushd %%D
  if exist cleanall.bat (
    call cleanall.bat
  ) else (
    call clean.bat
  )
  popd
)

if exist buildfre_*.log del buildfre_*.log
if exist buildchk_*.log del buildchk_*.log

_LNG=$(LANGUAGE)
_INX=.
STAMP=stampinf -f $@ -a $(_BUILDARCH).$(_NT_TARGET_MAJ_ARCH).$(_NT_TARGET_MIN_ARCH) -k $(KMDF_VERSION_MAJOR).$(KMDF_VERSION_MINOR)

$(OBJ_PATH)\$(O)\$(INF_NAME).inf: $(_INX)\$(INF_NAME).inf
    copy $(_INX)\$(@B).inf $@
    $(STAMP)

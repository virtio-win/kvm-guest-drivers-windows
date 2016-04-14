_LNG=$(LANGUAGE)
_INX=.
STAMP=stampinf -f $@ -d * -a $(_BUILDARCH).$(_NT_TARGET_MAJ_ARCH).$(_NT_TARGET_MIN_ARCH)


$(INF_TARGET): $(_INX)\$(INF_NAME).inx
    copy $(_INX)\$(INF_NAME).inx $@
    $(STAMP)



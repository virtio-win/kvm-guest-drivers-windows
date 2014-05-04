_LNG=$(LANGUAGE)
_INX=.
STAMP=stampinf -f $@ -a $(_BUILDARCH) -k $(KMDF_VERSION_MAJOR).$(KMDF_VERSION_MINOR)

$(OBJ_PATH)\$(O)\$(INF_NAME).inf: $(_INX)\$(INF_NAME).inf
    copy $(_INX)\$(@B).inf $@
    $(STAMP)

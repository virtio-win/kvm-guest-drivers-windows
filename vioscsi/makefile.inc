_LNG=$(LANGUAGE)
_INX=.
STAMP=stampinf -f $@ -d * -a $(_BUILDARCH)


$(OBJ_PATH)\$(O)\$(INF_NAME).inf: $(_INX)\$(INF_NAME).inx 
    copy $(_INX)\$(@B).inx $@
    $(STAMP)



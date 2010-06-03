_LNG=$(LANGUAGE)
_INX=.
STAMP=stampinf -f $@ -d * -a $(_BUILDARCH)


$(INF_TARGET): $(_INX)\$(INF_NAME).inx
    copy $(_INX)\$(INF_NAME).inx $@
    $(STAMP)



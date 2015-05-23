clean:
    del $(O)\vioscsidt.h $(O)\vioscsi.bmf

_LNG=$(LANGUAGE)
_INX=.
STAMP=stampinf -f $@ -d * -a $(_BUILDARCH)


$(OBJ_PATH)\$(O)\$(INF_NAME).inf: $(_INX)\$(INF_NAME).inx 
    copy $(_INX)\$(@B).inx $@
    $(STAMP)

$(O)\vioscsi.bmf: vioscsi.mof
    mofcomp -B:$(O)\vioscsi.bmf vioscsi.mof

vioscsidt.h: $(O)\vioscsi.bmf
    wmimofck -h$(O)\vioscsidt.h $(O)\vioscsi.bmf

vioscsi.html: $(O)\vioscsi.bmf
    wmimofck -w$(O)\vioscsi.html $(O)\vioscsi.bmf

vioscsi.vbs: $(O)\vioscsi.bmf
    wmimofck -t$(O)\vioscsi.vbs $(O)\vioscsi.bmf

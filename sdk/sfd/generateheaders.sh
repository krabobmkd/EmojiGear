# Note: sfd are file describing amiga library functions, used to generate includes
# so any compiler can just have a sfd->include tool to make sure it works with it.
# yet the includes generated with bebbo gcc6.5 "sfdc" quite works for all amiga compilers.
# just generating clib/inline/proto/pragmas are the "minimal OS3 needs"

# in the future if we manage multiple other OSes, generate what's needed for
# your compiler from the .sfd files with your compiler tool for this.

/opt/amiga/bin/sfdc utf8rastport_lib.sfd --mode clib >../include/clib/utf8rastport_protos.h
/opt/amiga/bin/sfdc utf8rastport_lib.sfd --mode proto >../include/proto/utf8rastport.h
/opt/amiga/bin/sfdc utf8rastport_lib.sfd --mode macros >../include/inline/utf8rastport.h
/opt/amiga/bin/sfdc utf8rastport_lib.sfd --mode pragmas >../include/pragmas/utf8rastport_pragmas.h

/opt/amiga/bin/sfdc unitexteditor_lib.sfd --mode clib >../include/clib/unitexteditor_protos.h
/opt/amiga/bin/sfdc unitexteditor_lib.sfd --mode proto >../include/proto/unitexteditor.h
/opt/amiga/bin/sfdc unitexteditor_lib.sfd --mode macros >../include/inline/unitexteditor.h
/opt/amiga/bin/sfdc unitexteditor_lib.sfd --mode pragmas >../include/pragmas/unitexteditor_pragmas.h

/opt/amiga/bin/sfdc unibutton_lib.sfd --mode clib >../include/clib/unibutton_protos.h
/opt/amiga/bin/sfdc unibutton_lib.sfd --mode proto >../include/proto/unibutton.h
/opt/amiga/bin/sfdc unibutton_lib.sfd --mode macros >../include/inline/unibutton.h
/opt/amiga/bin/sfdc unibutton_lib.sfd --mode pragmas >../include/pragmas/unibutton_pragmas.h

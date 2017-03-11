/* Some test definition here */
#define DEFINED_BUT_NO_VALUE
#define DEFINED_INT 3
#define DEFINED_STR "ABC"

/* definition to expand macro then apply to pragma message */
#define VALUE_TO_STRING(x) #x
#define VALUE(x) VALUE_TO_STRING(x)
#define VAR_NAME_VALUE(var) #var "="  VALUE(var)

#pragma warning(disable:4003)
/* Some example here */
//#pragma message(VAR_NAME_VALUE(NOT_DEF) )
//#pragma message(VAR_NAME_VALUE(DEFINED_BUT_NO_VALUE) )
//#pragma message(VAR_NAME_VALUE(DEFINED_INT) )
//#pragma message(VAR_NAME_VALUE(DEFINED_STR) )

#pragma message(VAR_NAME_VALUE(VZ_RELEASE_N) )
#pragma message(VAR_NAME_VALUE(VZ_RELEASE_A) )
#pragma message(VAR_NAME_VALUE(VZ_RELEASE_B) )
#pragma message(VAR_NAME_VALUE(VZ_RELEASE_C) )

#pragma message(VAR_NAME_VALUE(_NT_TARGET_MAJ) )
#pragma message(VAR_NAME_VALUE(_RHEL_RELEASE_VERSION_) )
#pragma message(VAR_NAME_VALUE(_BUILD_MAJOR_VERSION_) )
#pragma message(VAR_NAME_VALUE(_BUILD_MINOR_VERSION_) )

#pragma message(VAR_NAME_VALUE(RHEL_COPYRIGHT_STARTING_YEAR) )
#pragma message(VAR_NAME_VALUE(VZ_COPYRIGHT_STARTING_YEAR) )

#pragma message(VAR_NAME_VALUE(NTDDI_VERSION) )
//#error STOP

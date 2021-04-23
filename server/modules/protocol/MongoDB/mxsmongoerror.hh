// https://github.com/mongodb/mongo/blob/master/src/mongo/base/error_codes.yml

#if !defined(MXSMONGO_ERROR)
#error mxsmongoerror.hh cannot be included without MXSMONGO_ERROR being defined.
#endif

// The "location" errors are not documented, but appears to be created
// more or less on the spot in the Mongo code and used for fringe cases.

MXSMONGO_ERROR(OK,                         0, "OK")
MXSMONGO_ERROR(BAD_VALUE,                  2, "BadValue")
MXSMONGO_ERROR(FAILED_TO_PARSE,            9, "FailedToParse")
MXSMONGO_ERROR(UNAUTHORIZED,              13, "Unauthorized")
MXSMONGO_ERROR(TYPE_MISMATCH,             14, "TypeMismatch")
MXSMONGO_ERROR(INVALID_LENGTH,            16, "InvalidLength")
MXSMONGO_ERROR(NAMESPACE_NOT_FOUND,       26, "NamespaceNotFound")
MXSMONGO_ERROR(CURSOR_NOT_FOUND,          43, "CursorNotFound")
MXSMONGO_ERROR(NAMESPACE_EXISTS,          48, "NamespaceExists")
MXSMONGO_ERROR(COMMAND_NOT_FOUND,         59, "CommandNotFound")
MXSMONGO_ERROR(INVALID_NAMESPACE,         73, "InvalidNamespace")
MXSMONGO_ERROR(NO_REPLICATION_ENABLED,    76, "NoReplicationEnabled")
MXSMONGO_ERROR(COMMAND_FAILED,           125, "CommandFailed")
MXSMONGO_ERROR(LOCATION10065,          10065, "Location10065")
MXSMONGO_ERROR(DUPLICATE_KEY,          11000, "DuplicateKey")
MXSMONGO_ERROR(LOCATION15974,          15974, "Location15974")
MXSMONGO_ERROR(LOCATION15975,          15975, "Location15975")
MXSMONGO_ERROR(LOCATION40352,          40352, "Location40352")
MXSMONGO_ERROR(LOCATION40353,          40353, "Location40353")
MXSMONGO_ERROR(LOCATION40414,          40414, "Location40414")

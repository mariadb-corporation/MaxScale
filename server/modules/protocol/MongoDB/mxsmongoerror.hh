// https://github.com/mongodb/mongo/blob/master/src/mongo/base/error_codes.yml

#if !defined(MXSMONGO_ERROR)
#error mxsmongoerror.hh cannot be included without MXSMONGO_ERROR being defined.
#endif

MXSMONGO_ERROR(OK,                          0, "OK")
MXSMONGO_ERROR(BAD_VALUE,                   2, "BadValue")
MXSMONGO_ERROR(FAILED_TO_PARSE,             9, "FailedToParse")
MXSMONGO_ERROR(COMMAND_NOT_FOUND,          59, "CommandNotFound")
MXSMONGO_ERROR(NO_REPLICATION_ENABLED,     76, "NoReplicationEnabled")
MXSMONGO_ERROR(COMMAND_FAILED,            125, "CommandFailed")

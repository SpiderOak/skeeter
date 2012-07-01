//----------------------------------------------------------------------------
// display_strings.c
//
// string values of constants used for debugging and information display
//----------------------------------------------------------------------------

// strings for postgres ConnStatusType
const char * CONN_STATUS[] = {
	"CONNECTION_OK",
	"CONNECTION_BAD",
	"CONNECTION_STARTED",
	"CONNECTION_MADE",
	"CONNECTION_AWAITING_RESPONSE",
	"CONNECTION_AUTH_OK",
	"CONNECTION_SETENV",
	"CONNECTION_SSL_STARTUP",
	"CONNECTION_NEEDED"
};

const char * POLLING_STATUS[] = {
	"PGRES_POLLING_FAILED",
	"PGRES_POLLING_READING",
	"PGRES_POLLING_WRITING",
	"PGRES_POLLING_OK",
	"PGRES_POLLING_ACTIVE"
};


const char * EXEC_STATUS[] = {
	"PGRES_EMPTY_QUERY",
	"PGRES_COMMAND_OK",
	"PGRES_TUPLES_OK",
	"PGRES_COPY_OUT",
	"PGRES_COPY_IN",
	"PGRES_BAD_RESPONSE",
	"PGRES_NONFATAL_ERROR",
	"PGRES_FATAL_ERROR",
	"PGRES_COPY_BOTH"
};


/**
 * luapgsql - PostgreSQL driver
 * (c) 2009-19 Alacner zhang <alacner@gmail.com>
 * This content is released under the MIT License.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define LUA_PGSQL_VERSION "1.0.0"

#ifdef WIN32
#include <winsock2.h>
#define NO_CLIENT_LONG_LONG
#endif

#include "libpq-fe.h"
#include "pg_config.h"
#include <libpq/libpq-fs.h>

#include "lua.h"
#include "lauxlib.h"
#if ! defined (LUA_VERSION_NUM) || LUA_VERSION_NUM < 501
#include "compat-5.1.h"
#endif

/* Compatibility between Lua 5.1+ and Lua 5.0 */
#ifndef LUA_VERSION_NUM
#define LUA_VERSION_NUM 0
#endif
#if LUA_VERSION_NUM < 501
#define luaL_register(a, b, c) luaL_openlib((a), (b), (c), 0)
#endif

#define LUA_PGSQL_CONN "PgSQL connection"
#define LUA_PGSQL_RES "PgSQL result"
#define LUA_PGSQL_LO "PgSQL large object"
#define LUA_PGSQL_TABLENAME "pgsql"

#define LUA_PG_DATA_LENGTH 1
#define LUA_PG_DATA_ISNULL 2
#define LUA_PG_DATA_RESULT 3

#define LUA_PG_FIELD_NAME 1
#define LUA_PG_FIELD_SIZE 2
#define LUA_PG_FIELD_TYPE 3
#define LUA_PG_FIELD_TYPE_OID 4

#ifndef InvalidOid
#define InvalidOid ((Oid) 0)
#endif

#define PGSQL_ASSOC     1<<0
#define PGSQL_NUM       1<<1
#define PGSQL_BOTH      (PGSQL_ASSOC|PGSQL_NUM)

#define PGSQL_STATUS_LONG     1
#define PGSQL_STATUS_STRING   2

#define PGSQL_MAX_LENGTH_OF_LONG   30
#define PGSQL_MAX_LENGTH_OF_DOUBLE 60

#define PGSQL_LO_READ_BUF_SIZE  8192

#define safe_emalloc(nmemb, size, offset)  malloc((nmemb) * (size) + (offset)) 

typedef struct {
    short      closed;
} pseudo_data;

typedef struct {
    short   closed;
    int     env;
	int		field_class;
	int		field_types;
    int		lofd;
    PGconn *conn;
} lua_pg_conn;

typedef struct {
    short      closed;
    int        conn;               /* reference to connection */
    int        numcols;            /* number of columns */
	int        row;
    PGresult *res;
} lua_pg_res;

void luaM_setmeta (lua_State *L, const char *name);
int luaM_register (lua_State *L, const char *name, const luaL_reg *methods);
int luaopen_pgsql (lua_State *L);
int Lpg_get_field_types_hash (lua_State *L, PGconn *conn);
int Lpg_get_field_class_hash (lua_State *L, PGconn *conn);
void luaM_regconst(lua_State *L, const char *name, long value);

/* set const */
static int luaM_const (lua_State *L, const char *defined) {

	lua_newtable(L);
    /* For pg_fetch_array() */
	luaM_regconst(L, "PGSQL_ASSOC", PGSQL_ASSOC);
	luaM_regconst(L, "PGSQL_NUM", PGSQL_NUM);
	luaM_regconst(L, "PGSQL_BOTH", PGSQL_BOTH);
    /* For pg_connection_status() */
	luaM_regconst(L, "PGSQL_CONNECTION_BAD", CONNECTION_BAD);
	luaM_regconst(L, "PGSQL_CONNECTION_OK", CONNECTION_OK);
    /* For pg_transaction_status() */
	luaM_regconst(L, "PGSQL_TRANSACTION_IDLE", PQTRANS_IDLE);
	luaM_regconst(L, "PGSQL_TRANSACTION_ACTIVE", PQTRANS_ACTIVE);
	luaM_regconst(L, "PGSQL_TRANSACTION_INTRANS", PQTRANS_INTRANS);
	luaM_regconst(L, "PGSQL_TRANSACTION_INERROR", PQTRANS_INERROR);
	luaM_regconst(L, "PGSQL_TRANSACTION_UNKNOWN", PQTRANS_UNKNOWN);
    /* For pg_set_error_verbosity() */
	luaM_regconst(L, "PGSQL_ERRORS_TERSE", PQERRORS_TERSE);
	luaM_regconst(L, "PGSQL_ERRORS_DEFAULT", PQERRORS_DEFAULT);
	luaM_regconst(L, "PGSQL_ERRORS_VERBOSE", PQERRORS_VERBOSE);
    /* For lo_seek() */
	luaM_regconst(L, "PGSQL_SEEK_SET", SEEK_SET);
	luaM_regconst(L, "PGSQL_SEEK_CUR", SEEK_CUR);
	luaM_regconst(L, "PGSQL_SEEK_END", SEEK_END);
    /* For pg_result_status() return value type */
	luaM_regconst(L, "PGSQL_STATUS_LONG", PGSQL_STATUS_LONG);
	luaM_regconst(L, "PGSQL_STATUS_STRING", PGSQL_STATUS_STRING);
    /* For pg_result_status() return value */
	luaM_regconst(L, "PGSQL_EMPTY_QUERY", PGRES_EMPTY_QUERY);
	luaM_regconst(L, "PGSQL_COMMAND_OK", PGRES_COMMAND_OK);
	luaM_regconst(L, "PGSQL_TUPLES_OK", PGRES_TUPLES_OK);
	luaM_regconst(L, "PGSQL_COPY_OUT", PGRES_COPY_OUT);
	luaM_regconst(L, "PGSQL_COPY_IN", PGRES_COPY_IN);
	luaM_regconst(L, "PGSQL_BAD_RESPONSE", PGRES_BAD_RESPONSE);
	luaM_regconst(L, "PGSQL_NONFATAL_ERROR", PGRES_NONFATAL_ERROR);
	luaM_regconst(L, "PGSQL_FATAL_ERROR", PGRES_FATAL_ERROR);
    /* For pg_result_error_field() field codes */
	luaM_regconst(L, "PGSQL_DIAG_SEVERITY", PG_DIAG_SEVERITY);
	luaM_regconst(L, "PGSQL_DIAG_SQLSTATE", PG_DIAG_SQLSTATE);
	luaM_regconst(L, "PGSQL_DIAG_MESSAGE_PRIMARY", PG_DIAG_MESSAGE_PRIMARY);
	luaM_regconst(L, "PGSQL_DIAG_MESSAGE_DETAIL", PG_DIAG_MESSAGE_DETAIL);
	luaM_regconst(L, "PGSQL_DIAG_MESSAGE_HINT", PG_DIAG_MESSAGE_HINT);
	luaM_regconst(L, "PGSQL_DIAG_STATEMENT_POSITION", PG_DIAG_STATEMENT_POSITION);
	luaM_regconst(L, "PGSQL_DIAG_INTERNAL_POSITION", PG_DIAG_INTERNAL_POSITION);
	luaM_regconst(L, "PGSQL_DIAG_INTERNAL_QUERY", PG_DIAG_INTERNAL_QUERY);
	luaM_regconst(L, "PGSQL_DIAG_CONTEXT", PG_DIAG_CONTEXT);
	luaM_regconst(L, "PGSQL_DIAG_SOURCE_FILE", PG_DIAG_SOURCE_FILE);
	luaM_regconst(L, "PGSQL_DIAG_SOURCE_LINE", PG_DIAG_SOURCE_LINE);
	luaM_regconst(L, "PGSQL_DIAG_SOURCE_FUNCTION", PG_DIAG_SOURCE_FUNCTION);
    /* pg_convert options */
	//luaM_regconst(L, "PGSQL_CONV_IGNORE_DEFAULT", PGSQL_CONV_IGNORE_DEFAULT);
	//luaM_regconst(L, "PGSQL_CONV_FORCE_NULL", PGSQL_CONV_FORCE_NULL);
	//luaM_regconst(L, "PGSQL_CONV_IGNORE_NOT_NULL", PGSQL_CONV_IGNORE_NOT_NULL);
    /* pg_insert/update/delete/select options */
	//luaM_regconst(L, "PGSQL_DML_NO_CONV", PGSQL_DML_NO_CONV);
	//luaM_regconst(L, "PGSQL_DML_EXEC", PGSQL_DML_EXEC);
	//luaM_regconst(L, "PGSQL_DML_ASYNC", PGSQL_DML_ASYNC);
	//luaM_regconst(L, "PGSQL_DML_STRING", PGSQL_DML_STRING);

	lua_pushstring(L, defined);	
	lua_gettable(L, -2); 
	
	int ct = lua_tonumber(L, -1);
	lua_pop(L, 1);

	return ct;
}

/**                   
* Return the name of the object's metatable.
* This function is used by `tostring'.     
*/                            
static int luaM_tostring (lua_State *L) {                
    char buff[100];             
    pseudo_data *obj = (pseudo_data *)lua_touserdata (L, 1);     
    if (obj->closed)                          
        strcpy (buff, "closed");
    else
        sprintf (buff, "%p", (void *)obj);
    lua_pushfstring (L, "%s (%s)", lua_tostring(L,lua_upvalueindex(1)), buff);
    return 1;                            
}       

void luaM_regconst(lua_State *L, const char *name, long value) {
	lua_pushstring(L, name);
	lua_pushnumber(L, value);
	lua_rawset(L, -3);
}

/**
* Define the metatable for the object on top of the stack
*/
void luaM_setmeta (lua_State *L, const char *name) {
    luaL_getmetatable (L, name);
    lua_setmetatable (L, -2);
}     

/**
* Create a metatable and leave it on top of the stack.
*/
int luaM_register (lua_State *L, const char *name, const luaL_reg *methods) {
    if (!luaL_newmetatable (L, name))
        return 0;

    /* define methods */
    luaL_register (L, NULL, methods);

    /* define metamethods */
    lua_pushliteral (L, "__gc");
    lua_pushcfunction (L, methods->func);
    lua_settable (L, -3);

    lua_pushliteral (L, "__index");
    lua_pushvalue (L, -2);
    lua_settable (L, -3);

    lua_pushliteral (L, "__tostring");
    lua_pushstring (L, name);
    lua_pushcclosure (L, luaM_tostring, 1);
    lua_settable (L, -3);

    lua_pushliteral (L, "__metatable");
    lua_pushliteral (L, "you're not allowed to get this metatable");
    lua_settable (L, -3);

    return 1;
}

/**
* message
*/

static void luaM_msg(lua_State *L, const int n, const char *m) {
    if (n) {
        lua_pushnumber(L, 1);
    } else {
        lua_pushnil(L);
    }
    lua_pushstring(L, m);
}

/**
* Push the value of #i field of #tuple row.
*/
static void luaM_pushvalue (lua_State *L, void *row, long int len) {
    if (row == NULL)
        lua_pushnil (L);
    else
        lua_pushlstring (L, row, len);
}

/**
* Handle Part
*/

/**
* Check for valid connection.
*/
static lua_pg_conn *Mget_conn (lua_State *L) {
    lua_pg_conn *my_conn = (lua_pg_conn *)luaL_checkudata (L, 1, LUA_PGSQL_CONN);
    luaL_argcheck (L, my_conn != NULL, 1, "connection expected");
    luaL_argcheck (L, !my_conn->closed, 1, "connection is closed");
    return my_conn;
}

/**
* Check for valid result.
*/
static lua_pg_res *Mget_res (lua_State *L) {
    lua_pg_res *my_res = (lua_pg_res *)luaL_checkudata (L, 1, LUA_PGSQL_RES);
    luaL_argcheck (L, my_res != NULL, 1, "result expected");
    luaL_argcheck (L, !my_res->closed, 1, "result is closed");
    return my_res;
}

/**
* PGSQL operate functions
*/

/**
* Open a connection to a PgSQL Server
*/
static int Lpg_connect (lua_State *L) {
    const char *conninfo = luaL_optstring(L, 1, "dbname = postgres");

	PGconn *conn = PQconnectdb(conninfo);

	if (conn == NULL) {
        luaM_msg (L, 0, "Unable to connect to PostgreSQL server");
		return 2;
	}
    else if (PQstatus(conn) != CONNECTION_OK) {
		luaM_msg(L, 0, PQerrorMessage(conn));
        PQfinish(conn);
        return 2;
    }

	int ft = Lpg_get_field_types_hash(L, conn);
	int fc = Lpg_get_field_class_hash(L, conn);

    lua_pg_conn *my_conn = (lua_pg_conn *)lua_newuserdata(L, sizeof(lua_pg_conn));
    luaM_setmeta (L, LUA_PGSQL_CONN);

    /* fill in structure */
    my_conn->closed = 0;
    my_conn->env = LUA_NOREF;
    my_conn->conn = conn;
	my_conn->field_types = ft;
	my_conn->field_class = fc;

	return 1;
}

int Lpg_get_field_types_hash (lua_State *L, PGconn *conn) {
	PGresult *res;

	lua_newtable (L); /* field types result */

	/* hash all oid's */
	if ((res = PQexec(conn, "select oid,typname from pg_type")) == NULL || PQresultStatus(res) != PGRES_TUPLES_OK) {
		if (res) {
			PQclear(res);
		}
	} else {

		int i,num_rows;
		int oid_offset,name_offset;
		char *tmp_oid, *tmp_name;

		num_rows = PQntuples(res);
		oid_offset = PQfnumber(res, "oid");
		name_offset = PQfnumber(res, "typname");


		for ( i = 0; i < num_rows; i++) {
			if ((tmp_oid = PQgetvalue(res, i, oid_offset)) == NULL) {
				continue;
			}

			if ((tmp_name = PQgetvalue(res, i, name_offset)) == NULL) {
				continue;
			}

			luaM_pushvalue(L, tmp_name, strlen(tmp_name));
			lua_rawseti(L, -2, atoi(tmp_oid));
		}
		PQclear(res);
	}

	return luaL_ref (L, LUA_REGISTRYINDEX);
}

int Lpg_get_field_class_hash (lua_State *L, PGconn *conn) {

	lua_newtable (L); /* field tables result */

	return luaL_ref (L, LUA_REGISTRYINDEX);
}

static int Lpg_host (lua_State *L) {
    lua_pushstring(L, PQhost(Mget_conn(L)->conn));
    return 1;
}

static int Lpg_port (lua_State *L) {
    lua_pushstring(L, PQport(Mget_conn(L)->conn));
    return 1;
}

static int Lpg_dbname (lua_State *L) {
    lua_pushstring(L, PQdb(Mget_conn(L)->conn));
    return 1;
}

static int Lpg_tty (lua_State *L) {
    lua_pushstring(L, PQtty(Mget_conn(L)->conn));
    return 1;
}

static int Lpg_get_pid (lua_State *L) {
    lua_pushnumber(L, PQbackendPID(Mget_conn(L)->conn));
    return 1;
}

static int Lpg_do_get_field_name (lua_State *L, Oid oid) {
	lua_rawgeti (L, LUA_REGISTRYINDEX, Mget_conn(L)->field_types);

    if ( ! lua_istable(L, -1)) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "get `field_types' info error!");
		return 2;
	}

	if ( ! oid) return 1; /* return all field name in a table */

	lua_pushnumber(L, oid);
	lua_gettable(L, -2); 

	return 1;
}

static int Lpg_get_field_name (lua_State *L) {
    lua_Number oid = luaL_optnumber(L, 2, 0);
	return Lpg_do_get_field_name (L, oid);
}

static int Lpg_do_get_field_table (lua_State *L, Oid oid, int force) {
	const char *sql;
	PGresult *res;

	if (! oid) {
		return 1;
	}

	lua_pg_conn *my_conn = Mget_conn (L);
	lua_rawgeti (L, LUA_REGISTRYINDEX, my_conn->field_class);

	if ( ! force) { /* return field_table form cache hash */
		const char *name;
		lua_pushnumber(L, oid);
		lua_gettable(L, -2); 
		name = lua_tolstring(L, -1, NULL);
		lua_pop(L, 1);
		
		if (name != NULL) {
			lua_pushstring(L, name);
			return 1;
		}
	}

	lua_pushfstring(L, "select oid,relname from pg_class where oid=%d", oid);
	sql = lua_tolstring(L, -1, NULL);
	lua_pop(L, 1);

	if ((res = PQexec(my_conn->conn, sql)) == NULL || PQresultStatus(res) != PGRES_TUPLES_OK) {
		if (res) {
			PQclear(res);
		}
	} else {

		int i,num_rows;
		int oid_offset,class_offset;
		char *tmp_oid, *tmp_name;

		num_rows = PQntuples(res);
		oid_offset = PQfnumber(res, "oid");
		class_offset = PQfnumber(res, "relname");


		for ( i = 0; i < num_rows; i++) {
			if ((tmp_oid = PQgetvalue(res, i, oid_offset)) == NULL) {
				continue;
			}

			if ((tmp_name = PQgetvalue(res, i, class_offset)) == NULL) {
				continue;
			}

			luaM_pushvalue(L, tmp_name, strlen(tmp_name));
			lua_rawseti(L, -2, atoi(tmp_oid));
		}
		PQclear(res);
	}

    if ( ! lua_istable(L, -1)) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "get `field_types' info error!");
		return 2;
	}

	lua_pushnumber(L, oid);
	lua_gettable(L, -2); 

	return 1;
}


static int Lpg_get_field_table (lua_State *L) {
    lua_Number oid = luaL_optnumber(L, 2, 0);
    lua_Number is_force = luaL_optnumber(L, 3, 0);
	return Lpg_do_get_field_table (L, oid, is_force);
}

static int Lpg_field_table (lua_State *L) {
	Oid oid;

	lua_pg_res *my_res = Mget_res (L);
    lua_Number field_number = luaL_optnumber(L, 2, 0);
    lua_Number is_oid = luaL_optnumber(L, 3, 0);
    lua_Number is_force = luaL_optnumber(L, 4, 0);

	if (field_number < 0 || field_number >= PQnfields(my_res->res)) {
		luaM_msg(L, 0,  "Bad field offset specified");
		return 2;
	}

    oid = PQftable(my_res->res, field_number);

    if (InvalidOid == oid) {
		lua_pushboolean(L, 0);
		return 1;
    }

	if (is_oid) {
		lua_pushnumber(L, oid);
		return 1;
	}

	luaM_msg(L, 0, "building");
	return 2;
	return Lpg_do_get_field_table (L, oid, is_force);
}

static int Lpg_version (lua_State *L) {
	char *client = PG_VERSION;
	lua_Number protocol = PQprotocolVersion(Mget_conn(L)->conn);
	lua_Number server_version = PQserverVersion(Mget_conn(L)->conn);

	lua_newtable(L); /* result */

    luaM_pushvalue (L, "client", strlen("client"));
    luaM_pushvalue (L, client, strlen(client));
    lua_rawset (L, -3);


    luaM_pushvalue (L, "protocol", strlen("protocol"));
    lua_pushnumber (L, protocol);
    lua_rawset (L, -3);

	if (protocol >= 3) {
		server_version = atoi(PQparameterStatus(Mget_conn(L)->conn, "server_version"));
    }

    luaM_pushvalue (L, "server_version", strlen("server_version"));
    lua_pushnumber (L, server_version);
    lua_rawset (L, -3);

    return 1;
}

static int Lpg_connection_status (lua_State *L) {
    lua_pushnumber(L, PQstatus(Mget_conn(L)->conn));
    return 1;
}

static int Lpg_transaction_status (lua_State *L) {
    lua_pushnumber(L, PQtransactionStatus(Mget_conn(L)->conn));
    return 1;
}

static int Lpg_options (lua_State *L) {
    lua_pushstring(L, PQoptions(Mget_conn(L)->conn));
    return 1;
}

static int Lpg_parameter_status (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);
    const char *param_name = luaL_optstring(L, 2, NULL);
    const char *status = PQparameterStatus(my_conn->conn, param_name);
    lua_pushstring (L, status);
    return 1;
}

static int Lpg_ping (lua_State *L) {
	PGresult *res;

    lua_pg_conn *my_conn = Mget_conn (L);

    /* ping connection */
     res = PQexec(my_conn->conn, "SELECT 1;");
    PQclear(res);

    /* check status. */
    if (PQstatus(my_conn->conn) == CONNECTION_OK) {
		lua_pushboolean(L, 1);
		return 1;
	}

    /* reset connection if it's broken */
    PQreset(my_conn->conn);
    if (PQstatus(my_conn->conn) == CONNECTION_OK) {
		lua_pushboolean(L, 1);
		return 1;
    }

	lua_pushboolean(L, 0);
	return 1;
}

static int Lpg_get_notify (lua_State *L) {
	long result_type = PGSQL_ASSOC;
    PGnotify *pgsql_notify;

    lua_pg_conn *my_conn = Mget_conn (L);

    const char *result_type_str = luaL_optstring (L, 2, "PGSQL_ASSOC");
	result_type = luaM_const(L, result_type_str);
    if ( ! (result_type & PGSQL_BOTH)) {
        lua_pushstring(L, "Invalid result type");
		return 1;
    }

    PQconsumeInput(my_conn->conn);
    pgsql_notify = PQnotifies(my_conn->conn);
    if ( ! pgsql_notify) {
        /* no notify message */
		lua_pushboolean(L, 0);
		return 1;
    }

	lua_newtable(L);

    if (result_type & PGSQL_NUM) {
		lua_pushstring(L, pgsql_notify->relname);
		lua_rawseti(L, -2, 0);
		lua_pushnumber(L, pgsql_notify->be_pid);
		lua_rawseti(L, -2, 1);
    }
    if (result_type & PGSQL_ASSOC) {
		lua_pushstring(L, "message");
		lua_pushstring(L, pgsql_notify->relname);
		lua_rawset(L, -3);
		lua_pushstring(L, "pid");
		lua_pushnumber(L, pgsql_notify->be_pid);
		lua_rawset(L, -3);
    }
    PQfreemem(pgsql_notify);

	return 1;
}

static int Lpg_meta_data (lua_State *L) {
    PGresult *res;
    const char *tmp_name, *tmp_name2 = NULL;
	const char *sql;
    int i, num_rows;
    lua_pg_conn *my_conn = Mget_conn (L);

    const char *table_name = luaL_optstring (L, 2, NULL);

    if (table_name == NULL) {
		lua_pushboolean(L, 0);
        lua_pushstring(L, "The table name must be specified");
		return 2;
    }

	/* match out the public.tbl */
	lua_getglobal(L, "string");
	lua_getfield(L, -1, "match");

	if (lua_isfunction(L, -1) != 1) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "string.match is not a function?");
		return 2;
	}

	lua_pushstring(L, table_name);
	lua_pushstring(L, "^([^.]+)%.([^.]+)");

	if (lua_pcall(L, 2, 2, 0) != 0) {
		lua_pushboolean(L, 0);
		lua_tostring(L, -1);
		return 2;
	}

	if (lua_isnoneornil(L, -1)) {
        /* Default schema */
        tmp_name2 = table_name;
        tmp_name = "public";
	} else {
		tmp_name = lua_tostring(L, -2);
		tmp_name2 = lua_tostring(L, -1);
	}

	lua_pop(L, 4);

    lua_pushfstring(L,
        "SELECT a.attname, a.attnum, t.typname, a.attlen, a.attnotNULL, a.atthasdef, a.attndims "
        "FROM pg_class as c, pg_attribute a, pg_type t, pg_namespace n "
        "WHERE a.attnum > 0 AND a.attrelid = c.oid AND c.relname = '%s' AND c.relnamespace = n.oid AND n.nspname = '%s'"
		"AND a.atttypid = t.oid ORDER BY a.attnum;",
		tmp_name2, tmp_name);
	sql = lua_tolstring(L, -1, NULL);
	lua_pop(L, 1);


    res = PQexec(my_conn->conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || (num_rows = PQntuples(res)) == 0) {
		lua_pushboolean(L, 0);
        lua_pushfstring(L, "Table '%s' doesn't exists", table_name);
        PQclear(res);
        return 2;
    }

	//return 1;
	lua_newtable(L);

    for (i = 0; i < num_rows; i++) {
		/* table key */
		lua_pushstring(L, PQgetvalue(res,i,0));
		/* sub table value */
		lua_newtable(L);

		lua_pushstring(L, "num");
		lua_pushnumber(L, atoi(PQgetvalue(res,i,1)));
		lua_rawset(L, -3);

		lua_pushstring(L, "type");
		lua_pushstring(L, PQgetvalue(res,i,2));
		lua_rawset(L, -3);

		lua_pushstring(L, "len");
		lua_pushnumber(L, atoi(PQgetvalue(res,i,3)));
		lua_rawset(L, -3);

        if (!strcmp(PQgetvalue(res, i, 4), "t")) {
			lua_pushstring(L, "not null");
			lua_pushboolean(L, 1);
			lua_rawset(L, -3);
        }
        else {
			lua_pushstring(L, "not null");
			lua_pushboolean(L, 0);
			lua_rawset(L, -3);
        }
        if (!strcmp(PQgetvalue(res,i,5), "t")) {
			lua_pushstring(L, "has default");
			lua_pushboolean(L, 1);
			lua_rawset(L, -3);
        }
        else {
			lua_pushstring(L, "has default");
			lua_pushboolean(L, 0);
			lua_rawset(L, -3);
        }

		lua_pushstring(L, "array dims");
		lua_pushnumber(L, atoi(PQgetvalue(res,i,6)));
		lua_rawset(L, -3);
		/* into sub table*/
		lua_rawset(L, -3);
    }

    PQclear(res);
	
	return 1;
}

static int Lpg_last_error (lua_State *L) {
    lua_pushstring(L, PQerrorMessage(Mget_conn(L)->conn));
    return 1;
}

static int Lpg_set_error_verbosity (lua_State *L) {
    const char *result_type = luaL_optstring (L, 2, "PQERRORS_DEFAULT");
	lua_pushnumber(L, PQsetErrorVerbosity(Mget_conn (L)->conn, luaM_const(L, result_type)));
    return 1;
}

static int Lpg_escape_bytea (lua_State *L) {
	size_t to_len;
	unsigned char *to; 

    lua_pg_conn *my_conn = Mget_conn (L);
    const char *from = luaL_optstring(L, 2, NULL);
	if (my_conn->conn) {
		to = PQescapeByteaConn(my_conn->conn, (unsigned char*)from, strlen(from), &to_len);
	} else {
		to = PQescapeBytea((unsigned char*)from, strlen(from), &to_len);
	}
    luaM_pushvalue (L, to, to_len-1);
	PQfreemem(to);
    return 1;
}

static int Lpg_unescape_bytea (lua_State *L) {
	size_t to_len;
    //lua_pg_conn *my_conn = Mget_conn (L);
    const char *from = luaL_optstring(L, 2, NULL);
    unsigned char *to = PQunescapeBytea((unsigned char*)from, &to_len);
    luaM_pushvalue (L, to, to_len);
	PQfreemem(to);
    return 1;
}

static int Lpg_escape_string (lua_State *L) {
	char *to = NULL;
	int to_len, from_len;

    lua_pg_conn *my_conn = Mget_conn (L);
    const char *from = luaL_optstring(L, 2, NULL);
	from_len = strlen(from);
	to = (char *) safe_emalloc(from_len, 2, 1);

	if (my_conn->conn) {
		to_len = (int) PQescapeStringConn(my_conn->conn, to, from, (size_t)from_len, NULL);
	} else {
		to_len = (int) PQescapeString(to, from, (size_t)from_len);
	}

    luaM_pushvalue (L, to, to_len);
    return 1;
}

static int Lpg_connection_busy (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);

    PQconsumeInput(my_conn->conn);
    lua_pushboolean(L, PQisBusy(my_conn->conn));
    return 1;
}

static int Lpg_connection_reset (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);

    PQreset(my_conn->conn);
    if (PQstatus(my_conn->conn) == CONNECTION_BAD) {
		lua_pushboolean(L, 0);
    }
	lua_pushboolean(L, 1);
    return 1;
}

static int Lpg_cancel_query (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);
	PGresult *res;
	lua_Number return_value;
    return_value = PQrequestCancel(my_conn->conn);
    while ((res = PQgetResult(my_conn->conn))) {
		PQclear(res);
    }

    lua_pushboolean(L, return_value);
    return 1;
}

static int Lpg_trace (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);
	FILE *fp = NULL;

    const char *filename = luaL_optstring(L, 2, NULL);
    const char *mode = luaL_optstring(L, 3, "w");
	fp = fopen(filename, mode);
	if (fp == NULL) {
		lua_pushboolean(L, 0);
		return 1;
	}
	PQtrace(my_conn->conn, fp);
    lua_pushboolean(L, 1);
    return 1;
}

static int Lpg_untrace (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);

	PQuntrace(my_conn->conn);
    lua_pushboolean(L, 1);
    return 1;
}

static int Lpg_client_encoding (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);

	char *result_value = (char *)pg_encoding_to_char(PQclientEncoding(my_conn->conn));
	lua_pushstring(L, result_value);
    return 1;
}

static int Lpg_set_client_encoding (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);

    const char *encoding = luaL_optstring(L, 2, NULL);
	int result_value = (PQsetClientEncoding (my_conn->conn, encoding) == 0) ? 1 : 0;
    lua_pushboolean(L, result_value);
    return 1;
}

static int Lpg_query (lua_State *L) {
	int leftover = 0;
	ExecStatusType status;
	PGresult *res;

    lua_pg_conn *my_conn = Mget_conn (L);
	const char *statement = luaL_checkstring (L, 2);

	if (PQsetnonblocking(my_conn->conn, 0)) {
		lua_pushstring(L, "Cannot set connection to blocking mode");
		return 1;
	}

    while ((res = PQgetResult(my_conn->conn))) {
        PQclear(res);
        leftover = 1;
    }

    if (leftover) {
        lua_pushstring(L, "Found results on this connection. Use db:get_result() to get these results first");
		return 1;
    }
	
	res = PQexec(my_conn->conn, statement);

	// auto reset
    if (PQstatus(my_conn->conn) != CONNECTION_OK) {
        PQclear(res);
        PQreset(my_conn->conn);
        res = PQexec(my_conn->conn, statement);
    }

    if (res) {
        status = PQresultStatus(res);
    } else {
        status = (ExecStatusType) PQstatus(my_conn->conn);
    }

    switch (status) {
        case PGRES_EMPTY_QUERY:
        case PGRES_BAD_RESPONSE:
        case PGRES_NONFATAL_ERROR:
        case PGRES_FATAL_ERROR:
			lua_pushboolean(L, 0);
			luaM_msg(L, 0, PQerrorMessage(my_conn->conn));
            PQclear(res);
            return 2;
            break;
        case PGRES_COMMAND_OK: /* successful command that did not return rows */
        default:
            if (res) {
				lua_pg_res *my_res = (lua_pg_res *)lua_newuserdata(L, sizeof(lua_pg_res));
				luaM_setmeta (L, LUA_PGSQL_RES);

				/* fill in structure */
				my_res->closed = 0;
				my_res->row = 0;
				my_res->conn = LUA_NOREF;
				my_res->numcols = PQnfields(res);
				my_res->res = res;

				lua_pushvalue(L, 1);

				my_res->conn = luaL_ref (L, LUA_REGISTRYINDEX);

				return 1;
            } else {
                PQclear(res);
				lua_pushboolean (L, 0);
				return 1;
            }
            break;
    }
}

static int Lpg_send_query (lua_State *L) {
	int leftover = 0;
	PGresult *res;

    lua_pg_conn *my_conn = Mget_conn (L);
	const char *statement = luaL_checkstring (L, 2);

	if (PQsetnonblocking(my_conn->conn, 1)) {
		lua_pushstring(L, "Cannot set connection to nonblocking mode");
		return 1;
	}

    while ((res = PQgetResult(my_conn->conn))) {
        PQclear(res);
        leftover = 1;
    }

    if (leftover) {
        lua_pushstring(L, "Found results on this connection. Use db:get_result() to get these results FALSE");
		return 1;
    }

    if ( ! PQsendQuery(my_conn->conn, statement)) {
        if (PQstatus(my_conn->conn) != CONNECTION_OK) {
            PQreset(my_conn->conn);
        }
        if ( ! PQsendQuery(my_conn->conn, statement)) {
			lua_pushboolean(L, 0);
			return 0;
        }
    }

	if (PQsetnonblocking(my_conn->conn, 0)) {
		lua_pushstring(L, "Cannot set connection to blocking mode");
		return 1;
	}

	lua_pushboolean(L, 1);
	return 1;
}

static int Lpg_send_prepare (lua_State *L) {
	int leftover = 0;
	PGresult *res;

    lua_pg_conn *my_conn = Mget_conn (L);
	const char *stmtname = luaL_checkstring (L, 2);
	const char *query = luaL_checkstring (L, 3);

	if (PQsetnonblocking(my_conn->conn, 1)) {
		lua_pushstring(L, "Cannot set connection to nonblocking mode");
		return 1;
	}

    while ((res = PQgetResult(my_conn->conn))) {
        PQclear(res);
        leftover = 1;
    }

    if (leftover) {
        lua_pushstring(L, "Found results on this connection. Use db:get_result() to get these results FALSE");
		return 1;
    }

    if ( ! PQsendPrepare(my_conn->conn, stmtname, query, 0, NULL)) {
        if (PQstatus(my_conn->conn) != CONNECTION_OK) {
            PQreset(my_conn->conn);
        }
		if ( ! PQsendPrepare(my_conn->conn, stmtname, query, 0, NULL)) {
			lua_pushboolean(L, 0);
			return 0;
        }
    }

	if (PQsetnonblocking(my_conn->conn, 0)) {
		lua_pushstring(L, "Cannot set connection to blocking mode");
		return 1;
	}

	lua_pushboolean(L, 1);
	return 1;
}

static int Lpg_send_execute (lua_State *L) {
	int leftover = 0;
	PGresult *res;
	int num_params = 0;
	char **params = NULL;

    lua_pg_conn *my_conn = Mget_conn (L);
	const char *stmtname = luaL_checkstring (L, 2);

	if (lua_istable(L, 3)) {
		luaL_checktype(L, 3, LUA_TTABLE);
		num_params = lua_objlen(L, 3);
		if (num_params > 0) {
			int i = 0;
			params = (char **)safe_emalloc(sizeof(char *), 1, 0);

			for (i = 0; i < num_params; i++) {
				lua_rawgeti(L, 3, i);
				params[i] = (char *) lua_tostring(L, -1);
				lua_pop(L, 1);
			}
		}
	} else {
		num_params = 1;
		params = (char **)safe_emalloc(sizeof(char *), 1, 0);
		if (lua_isstring(L, 3)) { // auto set one param value
			params[0] = (char *) luaL_checkstring(L, 3);
		} else {
			params[0] = NULL;
		}
	}

	if (PQsetnonblocking(my_conn->conn, 0)) {
		lua_pushstring(L, "Cannot set connection to nonblocking mode");
		return 1;
	}

    while ((res = PQgetResult(my_conn->conn))) {
        PQclear(res);
        leftover = 1;
    }

    if (leftover) {
        lua_pushstring(L, "Found results on this connection. Use db:get_result() to get these results first");
		return 1;
    }


    if ( ! PQsendQueryPrepared(my_conn->conn, stmtname, num_params,
					(const char * const *)params, NULL, NULL, 0)) {
        if (PQstatus(my_conn->conn) != CONNECTION_OK) {
			PQreset(my_conn->conn);
        }
		if ( ! PQsendQueryPrepared(my_conn->conn, stmtname, num_params,
						(const char * const *)params, NULL, NULL, 0)) {
			lua_pushboolean(L, 0);
			return 1;
        }
    }

	if (PQsetnonblocking(my_conn->conn, 0)) {
		lua_pushstring(L, "Cannot set connection to blocking mode");
		return 1;
	}

	lua_pushboolean(L, 1);
	return 1;
}

static int Lpg_send_query_params (lua_State *L) {
	int leftover = 0;
	PGresult *res;
	int num_params = 0;
	char **params = NULL;

    lua_pg_conn *my_conn = Mget_conn (L);
	const char *query = luaL_checkstring (L, 2);

	if (lua_istable(L, 3)) {
		luaL_checktype(L, 3, LUA_TTABLE);
		num_params = lua_objlen(L, 3);
		if (num_params > 0) {
			int i = 0;
			params = (char **)safe_emalloc(sizeof(char *), 1, 0);

			for (i = 0; i < num_params; i++) {
				lua_rawgeti(L, 3, i);
				params[i] = (char *) lua_tostring(L, -1);
				lua_pop(L, 1);
			}
		}
	} else {
		num_params = 1;
		params = (char **)safe_emalloc(sizeof(char *), 1, 0);
		if (lua_isstring(L, 3)) { // auto set one param value
			params[0] = (char *) luaL_checkstring(L, 3);
		} else {
			params[0] = NULL;
		}
	}

	if (PQsetnonblocking(my_conn->conn, 1)) {
		lua_pushstring(L, "Cannot set connection to nonblocking mode");
		return 1;
	}

    while ((res = PQgetResult(my_conn->conn))) {
        PQclear(res);
        leftover = 1;
    }

    if (leftover) {
        lua_pushstring(L, "Found results on this connection. Use db:get_result() to get these results first");
		return 1;
    }


    if ( ! PQsendQueryParams(my_conn->conn, query, num_params,
					 NULL, (const char * const *)params, NULL, NULL, 0)) {
		if (PQstatus(my_conn->conn) != CONNECTION_OK) {
			PQreset(my_conn->conn);
        }
		if ( ! PQsendQueryParams(my_conn->conn, query, num_params,
						 NULL, (const char * const *)params, NULL, NULL, 0)) {
        }
    }

	if (PQsetnonblocking(my_conn->conn, 0)) {
		lua_pushstring(L, "Cannot set connection to blocking mode");
		return 1;
	}

	lua_pushboolean(L, 1);
	return 1;
}

static int Lpg_put_line (lua_State *L) {
    int result = 0;
    lua_pg_conn *my_conn = Mget_conn (L);

	const char *statement = luaL_checkstring (L, 2);


    result = PQputline(my_conn->conn, statement);
    if (result == EOF) {
        lua_pushfstring(L, "Query failed: %s", PQerrorMessage(my_conn->conn));
		return 1;
    }

	lua_pushboolean(L, 1);
	return 1;
}

static int Lpg_end_copy (lua_State *L) {
	int result = 0;

    lua_pg_conn *my_conn = Mget_conn (L);

    result = PQendcopy(my_conn->conn);

    if (result != 0) {
        lua_pushfstring(L, "Query failed: %s", PQerrorMessage(my_conn->conn));
		return 1;
    }
	
	lua_pushboolean(L, 1);
	return 1;
}

static int Lpg_prepare (lua_State *L) {
	int leftover = 0;
	ExecStatusType status;
	PGresult *res;

    lua_pg_conn *my_conn = Mget_conn (L);
	const char *stmtname = luaL_checkstring (L, 2);
	const char *query = luaL_checkstring (L, 3);


	if (PQsetnonblocking(my_conn->conn, 0)) {
		lua_pushstring(L, "Cannot set connection to blocking mode");
		return 1;
	}

    while ((res = PQgetResult(my_conn->conn))) {
        PQclear(res);
        leftover = 1;
    }

    if (leftover) {
        lua_pushstring(L, "Found results on this connection. Use db:get_result() to get these results first");
		return 1;
    }

    res = PQprepare(my_conn->conn, stmtname, query, 0, NULL);
    if (PQstatus(my_conn->conn) != CONNECTION_OK) {
        PQclear(res);
        PQreset(my_conn->conn);
        res = PQprepare(my_conn->conn, stmtname, query, 0, NULL);
    }
    if (res) {
        status = PQresultStatus(res);
    } else {
        status = (ExecStatusType) PQstatus(my_conn->conn);
    }

    switch (status) {
        case PGRES_EMPTY_QUERY:
        case PGRES_BAD_RESPONSE:
        case PGRES_NONFATAL_ERROR:
        case PGRES_FATAL_ERROR:
			lua_pushboolean(L, 0);
			luaM_msg(L, 0, PQerrorMessage(my_conn->conn));
            PQclear(res);
            return 2;
            break;
        case PGRES_COMMAND_OK: /* successful command that did not return rows */
        default:
            if (res) {
				lua_pg_res *my_res = (lua_pg_res *)lua_newuserdata(L, sizeof(lua_pg_res));
				luaM_setmeta (L, LUA_PGSQL_RES);

				/* fill in structure */
				my_res->closed = 0;
				my_res->row = 0;
				my_res->conn = LUA_NOREF;
				my_res->numcols = PQnfields(res);
				my_res->res = res;

				lua_pushvalue(L, 1);

				my_res->conn = luaL_ref (L, LUA_REGISTRYINDEX);

				return 1;
            } else {
                PQclear(res);
				lua_pushboolean (L, 0);
				return 1;
            }
            break;
    }
}

static int Lpg_execute (lua_State *L) {
	int leftover = 0;
	ExecStatusType status;
	PGresult *res;
	int num_params = 0;
	char **params = NULL;

    lua_pg_conn *my_conn = Mget_conn (L);
	const char *stmtname = luaL_checkstring (L, 2);

	if (lua_istable(L, 3)) {
		luaL_checktype(L, 3, LUA_TTABLE);
		num_params = lua_objlen(L, 3);
		if (num_params > 0) {
			int i = 0;
			params = (char **)safe_emalloc(sizeof(char *), 1, 0);

			for (i = 0; i < num_params; i++) {
				lua_rawgeti(L, 3, i);
				params[i] = (char *) lua_tostring(L, -1);
				lua_pop(L, 1);
			}
		}
	} else {
		num_params = 1;
		params = (char **)safe_emalloc(sizeof(char *), 1, 0);
		if (lua_isstring(L, 3)) { // auto set one param value
			params[0] = (char *) luaL_checkstring(L, 3);
		} else {
			params[0] = NULL;
		}
	}

	if (PQsetnonblocking(my_conn->conn, 0)) {
		lua_pushstring(L, "Cannot set connection to blocking mode");
		return 1;
	}

    while ((res = PQgetResult(my_conn->conn))) {
        PQclear(res);
        leftover = 1;
    }

    if (leftover) {
        lua_pushstring(L, "Found results on this connection. Use db:get_result() to get these results first");
		return 1;
    }


    res = PQexecPrepared(my_conn->conn, stmtname, num_params,
                    (const char * const *)params, NULL, NULL, 0);

    if (PQstatus(my_conn->conn) != CONNECTION_OK) {
        PQclear(res);
        PQreset(my_conn->conn);
		res = PQexecPrepared(my_conn->conn, stmtname, num_params,
						(const char * const *)params, NULL, NULL, 0);
    }

    if (res) {
        status = PQresultStatus(res);
    } else {
        status = (ExecStatusType) PQstatus(my_conn->conn);
    }

    switch (status) {
        case PGRES_EMPTY_QUERY:
        case PGRES_BAD_RESPONSE:
        case PGRES_NONFATAL_ERROR:
        case PGRES_FATAL_ERROR:
			lua_pushboolean(L, 0);
			luaM_msg(L, 0, PQerrorMessage(my_conn->conn));
            PQclear(res);
            return 2;
            break;
        case PGRES_COMMAND_OK: /* successful command that did not return rows */
        default:
            if (res) {
				lua_pg_res *my_res = (lua_pg_res *)lua_newuserdata(L, sizeof(lua_pg_res));
				luaM_setmeta (L, LUA_PGSQL_RES);

				/* fill in structure */
				my_res->closed = 0;
				my_res->row = 0;
				my_res->conn = LUA_NOREF;
				my_res->numcols = PQnfields(res);
				my_res->res = res;

				lua_pushvalue(L, 1);

				my_res->conn = luaL_ref (L, LUA_REGISTRYINDEX);

				return 1;
            } else {
                PQclear(res);
				lua_pushboolean (L, 0);
				return 1;
            }
            break;
    }
}

static int Lpg_query_params (lua_State *L) {
	int leftover = 0;
	ExecStatusType status;
	PGresult *res;
	int num_params = 0;
	char **params = NULL;

    lua_pg_conn *my_conn = Mget_conn (L);
	const char *query = luaL_checkstring (L, 2);

	if (lua_istable(L, 3)) {
		luaL_checktype(L, 3, LUA_TTABLE);
		num_params = lua_objlen(L, 3);
		if (num_params > 0) {
			int i = 0;
			params = (char **)safe_emalloc(sizeof(char *), 1, 0);

			for (i = 0; i < num_params; i++) {
				lua_rawgeti(L, 3, i);
				params[i] = (char *) lua_tostring(L, -1);
				lua_pop(L, 1);
			}
		}
	} else {
		num_params = 1;
		params = (char **)safe_emalloc(sizeof(char *), 1, 0);
		if (lua_isstring(L, 3)) { // auto set one param value
			params[0] = (char *) luaL_checkstring(L, 3);
		} else {
			params[0] = NULL;
		}
	}

	if (PQsetnonblocking(my_conn->conn, 0)) {
		lua_pushstring(L, "Cannot set connection to blocking mode");
		return 1;
	}

    while ((res = PQgetResult(my_conn->conn))) {
        PQclear(res);
        leftover = 1;
    }

    if (leftover) {
        lua_pushstring(L, "Found results on this connection. Use db:get_result() to get these results first");
		return 1;
    }


    res = PQexecParams(my_conn->conn, query, num_params,
                    NULL, (const char * const *)params, NULL, NULL, 0);

    if (PQstatus(my_conn->conn) != CONNECTION_OK) {
        PQclear(res);
        PQreset(my_conn->conn);
		res = PQexecParams(my_conn->conn, query, num_params,
					NULL, (const char * const *)params, NULL, NULL, 0);
    }

    if (res) {
        status = PQresultStatus(res);
    } else {
        status = (ExecStatusType) PQstatus(my_conn->conn);
    }

    switch (status) {
        case PGRES_EMPTY_QUERY:
        case PGRES_BAD_RESPONSE:
        case PGRES_NONFATAL_ERROR:
        case PGRES_FATAL_ERROR:
			lua_pushboolean(L, 0);
			luaM_msg(L, 0, PQerrorMessage(my_conn->conn));
            PQclear(res);
            return 2;
            break;
        case PGRES_COMMAND_OK: /* successful command that did not return rows */
        default:
            if (res) {
				lua_pg_res *my_res = (lua_pg_res *)lua_newuserdata(L, sizeof(lua_pg_res));
				luaM_setmeta (L, LUA_PGSQL_RES);

				/* fill in structure */
				my_res->closed = 0;
				my_res->row = 0;
				my_res->conn = LUA_NOREF;
				my_res->numcols = PQnfields(res);
				my_res->res = res;

				lua_pushvalue(L, 1);

				my_res->conn = luaL_ref (L, LUA_REGISTRYINDEX);

				return 1;
            } else {
                PQclear(res);
				lua_pushboolean (L, 0);
				return 1;
            }
            break;
    }
}

static int Lpg_field_num (lua_State *L) {
	lua_pg_res *my_res = Mget_res (L);
    const char *field_name = luaL_optstring(L, 2, NULL);
    lua_pushnumber (L, (lua_Number) PQfnumber (my_res->res, field_name));
    return 1;
}

static int Lpg_get_field_info(lua_State *L, int entry_type) {
	Oid oid;
	char *fname;
	int fsize;
	lua_pg_res *my_res = Mget_res (L);
    lua_Number field_number = luaL_optnumber(L, 2, 0);

    if (field_number < 0 || field_number >= PQnfields(my_res->res)) {
		lua_pushboolean(L, 0);
        lua_pushstring(L, "Bad field offset specified");
		return 2;
    }

    switch (entry_type) {
        case LUA_PG_FIELD_NAME:
			fname = PQfname(my_res->res, field_number);
            luaM_pushvalue(L, fname, strlen(fname));
			return 1;
            break;
        case LUA_PG_FIELD_SIZE:
			fsize = PQfsize(my_res->res, field_number);
            lua_pushnumber(L, fsize);
            break;
        case LUA_PG_FIELD_TYPE:
            oid = PQftype(my_res->res, field_number);
			lua_pushnumber(L, oid);
			return Lpg_do_get_field_name(L, oid);
            break;
        case LUA_PG_FIELD_TYPE_OID:
            oid = PQftype(my_res->res, field_number);
			lua_pushnumber(L, oid);
            break;
        default:
			lua_pushboolean(L, 0);
    }
	return 1;
}

static int Lpg_field_name (lua_State *L) {
	return Lpg_get_field_info(L, LUA_PG_FIELD_NAME);
}

static int Lpg_field_size (lua_State *L) {
	return Lpg_get_field_info(L, LUA_PG_FIELD_SIZE);
}

static int Lpg_field_type (lua_State *L) {
	return Lpg_get_field_info(L, LUA_PG_FIELD_TYPE);
}

static int Lpg_field_type_oid (lua_State *L) {
	return Lpg_get_field_info(L, LUA_PG_FIELD_TYPE_OID);
}

static int Lpg_num_rows (lua_State *L) {
    lua_pushnumber(L, PQntuples(Mget_res(L)->res));
    return 1;
}

static int Lpg_num_fields (lua_State *L) {
    lua_pushnumber(L, PQnfields(Mget_res(L)->res));
    return 1;
}

static int Lpg_affected_rows (lua_State *L) {
    lua_pushnumber(L, atoi(PQcmdTuples(Mget_res(L)->res)));
    return 1;
}

static int Lpg_data_info (lua_State *L, int entry_type) {
	int field_offset, pgsql_row;

	lua_pg_res *my_res = Mget_res (L);
    const char *row = luaL_optstring(L, 2, 0);
    const char *field = luaL_optstring(L, 3, NULL);
	
	if (field == NULL) {
		pgsql_row = my_res->row;
		field = row;
	} else {
		pgsql_row = atoi(row);
	}

	if (pgsql_row >= PQntuples(my_res->res)) {
		lua_pushfstring(L, "Unable to jump to row %d on PostgreSQL!", pgsql_row);
		return 1;
	}

	lua_pushstring(L, field);
	if (lua_isnumber(L, -1)) {
		field_offset = atoi(field);		
	} else {
		field_offset = PQfnumber(my_res->res, field);
	}
	
	if (field_offset < 0 || field_offset >= PQnfields(my_res->res)) {
		lua_pushstring(L, "Bad column offset specified");
		return 1;
	}

    switch (entry_type) {
        case LUA_PG_DATA_LENGTH:
            lua_pushnumber(L, PQgetlength(my_res->res, pgsql_row, field_offset));
            break;
        case LUA_PG_DATA_ISNULL:
            lua_pushnumber(L, PQgetisnull(my_res->res, pgsql_row, field_offset));
            break;
        case LUA_PG_DATA_RESULT:
			if (PQgetisnull(my_res->res, pgsql_row, field_offset)) {
				lua_pushnil(L);
			} else {
				char *value = PQgetvalue(my_res->res, pgsql_row, field_offset);
				int value_len = PQgetlength(my_res->res, pgsql_row, field_offset);
				luaM_pushvalue(L, value, value_len);
			}
			break;
    }

    return 1;
}

static int Lpg_field_is_null (lua_State *L) {
	return Lpg_data_info(L, LUA_PG_DATA_ISNULL);
}

static int Lpg_field_prtlen(lua_State *L) {
	return Lpg_data_info(L, LUA_PG_DATA_LENGTH);
}

static int Lpg_do_fetch(lua_State *L, int result_type) {
	int	i, num_fields;
    char            *element, *field_name;
    uint            element_len;
	lua_pg_res *my_res = Mget_res (L);

    if ( ! result_type) {
		result_type = PGSQL_BOTH;
    }

	/* use internal row counter to access next row */
	if (my_res->row >= PQntuples(my_res->res)) {
		lua_pushboolean(L, 0);
		return 1;
	}

	lua_newtable (L); /* result */

    for (i = 0, num_fields = PQnfields(my_res->res); i < num_fields; i++) {
        if (PQgetisnull(my_res->res, my_res->row, i)) {
			if (result_type & PGSQL_NUM) {
				lua_pushstring (L, ""); //lua_pushnil (L); if use pushnil, it'll loss the all key-value
				lua_rawseti (L, -2, i);
			}
			if (result_type & PGSQL_ASSOC) {
				field_name = PQfname(my_res->res, i);
				lua_pushstring(L, field_name);
				lua_pushstring (L, "");
				lua_rawset (L, -3);
			}
        } else {
            element = PQgetvalue(my_res->res, my_res->row, i);
            element_len = (element ? strlen(element) : 0);

			if (element) {
				if (result_type & PGSQL_NUM) {
					lua_pushstring(L, element);
					lua_rawseti (L, -2, i);
				}
				if (result_type & PGSQL_ASSOC) {
					field_name = PQfname(my_res->res, i);
					lua_pushstring(L, field_name);
					luaM_pushvalue(L, element, element_len);
					lua_rawset (L, -3);
				}
			}
        }
    }

	my_res->row++;

	return 1;
}

static int Lpg_fetch_row (lua_State *L) {
    return Lpg_do_fetch(L, PGSQL_NUM);
}

static int Lpg_fetch_assoc (lua_State *L) {
    return Lpg_do_fetch(L, PGSQL_ASSOC);
}

static int Lpg_fetch_array (lua_State *L) {
    const char *result_type = luaL_optstring (L, 2, "PGSQL_BOTH");
    return Lpg_do_fetch(L, luaM_const(L, result_type));
}

static int Lpg_fetch_result (lua_State *L) {
	return Lpg_data_info(L, LUA_PG_DATA_RESULT);
}


static int Lpg_get_result (lua_State *L) {
    PGresult *res;

    lua_pg_conn *my_conn = Mget_conn (L);

    res = PQgetResult(my_conn->conn);
    if ( ! res) {
        /* no result */
		lua_pushboolean(L, 0);
		return 1;
    }

	lua_pg_res *my_res = (lua_pg_res *)lua_newuserdata(L, sizeof(lua_pg_res));
	luaM_setmeta (L, LUA_PGSQL_RES);

	/* fill in structure */
	my_res->closed = 0;
	my_res->row = 0;
	my_res->conn = LUA_NOREF;
	my_res->numcols = PQnfields(res);
	my_res->res = res;

	lua_pushvalue(L, 1);

	my_res->conn = luaL_ref (L, LUA_REGISTRYINDEX);

	return 1;
}

static int Lpg_do_fetch_all (lua_State *L, lua_pg_res *my_res) {

    char *field_name, *element;
    size_t num_fields, element_len;
    int pg_numrows, pg_row;
    uint i;

	//lua_pg_res *my_res = Mget_res (L);

    if ((pg_numrows = PQntuples(my_res->res)) <= 0) {
		lua_pushboolean(L, 0);
		return 1;
    }


	lua_newtable(L); /* result */

    for (pg_row = 0; pg_row < pg_numrows; pg_row++) {
		
		lua_newtable(L); /* row result */

        for (i = 0, num_fields = PQnfields(my_res->res); i < num_fields; i++) {

            if (PQgetisnull(my_res->res, pg_row, i)) {
                field_name = PQfname(my_res->res, i);
				lua_pushstring(L, field_name);
				lua_pushstring (L, "");
				lua_rawset (L, -3);
            } else {

                element = PQgetvalue(my_res->res, pg_row, i);
				element_len = (element ? strlen(element) : 0);

				if (element) {
					field_name = PQfname(my_res->res, i);
					lua_pushstring(L, field_name);
					luaM_pushvalue(L, element, element_len);
					lua_rawset (L, -3);
				}
            }
        }
		lua_rawseti (L, -2, pg_row);
    }

	return 1;
}

static int Lpg_fetch_all (lua_State *L) {
	lua_pg_res *my_res = Mget_res (L);
	return Lpg_do_fetch_all(L, my_res);
}

static int Lpg_fetch_all_columns (lua_State *L) {

    char *element;
    size_t num_fields, element_len;
    int pg_numrows, pg_row;

	lua_pg_res *my_res = Mget_res (L);

    long colno = luaL_optnumber(L, 2, 0);

    if ((pg_numrows = PQntuples(my_res->res)) <= 0) {
		lua_pushboolean(L, 0);
		return 1;
    }

	num_fields = PQnfields(my_res->res);

    if (colno >= num_fields || colno < 0) {
		lua_pushboolean(L, 0);
		lua_pushfstring(L, "Invalid column number '%ld'", colno);
		return 2;
    }

	lua_newtable(L); /* result */

    for (pg_row = 0; pg_row < pg_numrows; pg_row++) {
		
		if (PQgetisnull(my_res->res, pg_row, colno)) {
			lua_pushstring (L, "");
			lua_rawseti (L, -2, pg_row);
		} else {

			element = PQgetvalue(my_res->res, pg_row, colno);
			element_len = (element ? strlen(element) : 0);

			if (element) {
				luaM_pushvalue(L, element, element_len);
				lua_rawseti (L, -2, pg_row);
			}
		}
    }

	return 1;
}

static int Lpg_last_oid (lua_State *L) {
	Oid oid;

    lua_pg_res *my_res = Mget_res (L);

    oid = PQoidValue(my_res->res); // = (char *) PQoidStatus(my_res->res);
    if (oid == InvalidOid) {
		lua_pushboolean(L, 0);
    }
    lua_pushnumber(L, oid);
    return 1;
}

static int Lpg_result_error (lua_State *L) {
	lua_pushstring(L, (char *)PQresultErrorMessage(Mget_res (L)->res));
    return 1;
}

static int Lpg_result_seek (lua_State *L) {
	lua_pg_res *my_res = Mget_res (L);

    lua_Number row = luaL_optnumber(L, 2, -1);


    if (row < 0 || row >= PQntuples(my_res->res)) {
		lua_pushboolean(L, 0);
		return 1;
    }

    /* seek to offset */
    my_res->row = row;

	lua_pushboolean(L, 1);
	return 1;
}

static int Lpg_result_status (lua_State *L) {
	lua_pg_res *my_res = Mget_res (L);

    const char *result_type_str = luaL_optstring(L, 2, "PGSQL_STATUS_LONG");

    long result_type = luaM_const(L, result_type_str);
    ExecStatusType status;

    if (result_type == PGSQL_STATUS_LONG) {
        status = PQresultStatus(my_res->res);
		lua_pushnumber(L, status);
		return 1;
    }
    else if (result_type == PGSQL_STATUS_STRING) { lua_pushstring(L, PQcmdStatus(my_res->res));
		return 1;
    }
    else {
        lua_pushfstring(L, "Optional 1th parameter should be PGSQL_STATUS_LONG or PGSQL_STATUS_STRING");
		return 1;
    }
}

static int Lpg_result_error_field (lua_State *L) {
    char *field = NULL;
	lua_pg_res *my_res = Mget_res (L);

    long fieldcode = luaL_optnumber(L, 2, -1);

    if ( ! my_res->res) {
		lua_pushboolean(L, 0);
		return 1;
    }

    if (fieldcode & (PG_DIAG_SEVERITY|PG_DIAG_SQLSTATE|PG_DIAG_MESSAGE_PRIMARY|PG_DIAG_MESSAGE_DETAIL
                |PG_DIAG_MESSAGE_HINT|PG_DIAG_STATEMENT_POSITION
                |PG_DIAG_INTERNAL_POSITION
                |PG_DIAG_INTERNAL_QUERY
                |PG_DIAG_CONTEXT|PG_DIAG_SOURCE_FILE|PG_DIAG_SOURCE_LINE
                |PG_DIAG_SOURCE_FUNCTION)) {
        field = (char *)PQresultErrorField(my_res->res, fieldcode);
        if (field == NULL) {
			lua_pushnil(L);
        } else {
            lua_pushstring(L, field);
        }
    } else {
		lua_pushboolean(L, 0);
    }
	return 1;
}

static int Lpg_free_result (lua_State *L) {
    lua_pg_res *my_res = (lua_pg_res *)luaL_checkudata (L, 1, LUA_PGSQL_RES);
    luaL_argcheck (L, my_res != NULL, 1, "result expected");
    if (my_res->closed) {
        lua_pushboolean (L, 0);
        return 1;
    }

    /* Nullify structure fields. */
    my_res->closed = 1;
    luaL_unref (L, LUA_REGISTRYINDEX, my_res->conn);
    luaL_unref (L, LUA_REGISTRYINDEX, my_res->row);

    lua_pushboolean (L, 1);

    return 1;
}

static int Lpg_lo_create (lua_State *L) {
	Oid pgsql_oid;

    lua_pg_conn *my_conn = Mget_conn (L);

    if ((pgsql_oid = lo_creat(my_conn->conn, INV_READ|INV_WRITE)) == InvalidOid) {
		lua_pushboolean(L, 0);
        lua_pushfstring(L, "Unable to create PostgreSQL large object");
		return 2;
    }

	lua_pushnumber(L, pgsql_oid);
	return 1;
}

static int Lpg_lo_unlink (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);
	long oid = luaL_checknumber (L, 2);

	if (lo_unlink(my_conn->conn, oid) == -1) {
		lua_pushboolean(L, 0);
		lua_pushfstring(L, "Unable to delete PostgreSQL large object %u", oid);
		return 1;
	}

	lua_pushboolean(L, 1);
	return 1;
}

static int Lpg_lo_open (lua_State *L) {
	int pgsql_mode = 0, pgsql_lofd;
	int create=0;

    lua_pg_conn *my_conn = Mget_conn (L);
	long oid = luaL_checknumber (L, 2);
	const char *mode_string = luaL_checkstring (L, 3);

    if (strchr(mode_string, 'r') == mode_string) {
        pgsql_mode |= INV_READ;
        if (strchr(mode_string, '+') == mode_string+1) {
            pgsql_mode |= INV_WRITE;
        }
    }
    if (strchr(mode_string, 'w') == mode_string) {
        pgsql_mode |= INV_WRITE;
        create = 1;
        if (strchr(mode_string, '+') == mode_string+1) {
            pgsql_mode |= INV_READ;
        }
    }

    if ((pgsql_lofd = lo_open(my_conn->conn, oid, pgsql_mode)) == -1) {
        if (create) {
            if ((oid = lo_creat(my_conn->conn, INV_READ|INV_WRITE)) == 0) {
				lua_pushboolean(L, 0);
                lua_pushfstring(L, "Unable to create PostgreSQL large object");
				return 2;
            } else {
                if ((pgsql_lofd = lo_open(my_conn->conn, oid, pgsql_mode)) == -1) {
                    if (lo_unlink(my_conn->conn, oid) == -1) {
						lua_pushboolean(L, 0);
                        lua_pushfstring(L, "Something is really messed up! Your database is badly corrupted in a way NOT related to LUA");
						return 2;
                    }
					lua_pushboolean(L, 0);
                    lua_pushfstring(L, "Unable to open PostgreSQL large object");
					return 2;
                } else {
					my_conn->lofd = pgsql_lofd;
					lua_pushnumber(L, pgsql_lofd);
                }
            }
        } else {
			lua_pushboolean(L, 0);
            lua_pushfstring(L, "Unable to open PostgreSQL large object");
			return 2;
        }
    } else {
		my_conn->lofd = pgsql_lofd;
		lua_pushnumber(L, pgsql_lofd);
    }

	return 1;
}

static int Lpg_lo_close (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);

	int lofd = luaL_optnumber(L, 2, my_conn->lofd);

	if (lo_close(my_conn->conn, lofd) < 0) {
		lua_pushboolean(L, 0);
		lua_pushfstring(L, "Unable to close PostgreSQL large object descriptor %d", lofd);
		return 1;
	}

	lua_pushboolean(L, 1);
	return 1;
}

static int Lpg_lo_read (lua_State *L) {
	char *buf;
	int nbytes;
    lua_pg_conn *my_conn = Mget_conn (L);

	int lofd = luaL_optnumber(L, 2, my_conn->lofd);
	int buf_len = luaL_optnumber(L, 3, PGSQL_LO_READ_BUF_SIZE);

	buf = (char *) safe_emalloc(sizeof(char), (buf_len+1), 0);

	if ((nbytes = lo_read(my_conn->conn, lofd, buf, buf_len)) < 0) {
		lua_pushboolean(L, 0);
		return 1;
	}

	buf[nbytes] = '\0';
	luaM_pushvalue(L, buf, nbytes);
	return 1;
}

static int Lpg_lo_write (lua_State *L) {
	int nbytes;

    lua_pg_conn *my_conn = Mget_conn (L);

	int lofd = luaL_optnumber(L, 2, my_conn->lofd);
	const char *str = luaL_optstring(L, 3, NULL);

	if ((nbytes = lo_write(my_conn->conn, lofd, str, strlen(str))) == -1) {
		lua_pushboolean(L, 0);
		return 1;
	}

	lua_pushnumber(L, nbytes);
	return 1;
}

static int Lpg_lo_read_all (lua_State *L) {
	int tbytes;
	volatile int nbytes;
	char buf[PGSQL_LO_READ_BUF_SIZE];

    lua_pg_conn *my_conn = Mget_conn (L);

	int lofd = luaL_optnumber(L, 2, my_conn->lofd);

	tbytes = 0;
	if ((nbytes = lo_read(my_conn->conn, lofd, buf, PGSQL_LO_READ_BUF_SIZE)) > 0) {
		lua_pushnumber(L, nbytes);
		lua_pushstring(L, buf);
		return 2;
	}

	lua_pushnumber(L, 0);
	return 1;
}

static int Lpg_lo_import (lua_State *L) {
	Oid oid;

    lua_pg_conn *my_conn = Mget_conn (L);

	const char *file_in = luaL_checkstring(L, 2);

	oid = lo_import(my_conn->conn, file_in);
	
	if (oid == InvalidOid) {
		lua_pushboolean(L, 0);
		return 1;
	}

	lua_pushnumber(L, oid);
	return 1;
}

static int Lpg_lo_export (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);

	Oid oid = luaL_checknumber(L, 2);
	const char *file_out = luaL_checkstring(L, 3);

	if (lo_export(my_conn->conn, oid, file_out)) {
		lua_pushboolean(L, 1);
		return 1;
	}

	lua_pushboolean(L, 0);
	return 1;
}

static int Lpg_lo_seek (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);

	int lofd = luaL_optnumber(L, 2, my_conn->lofd);
	long offset = luaL_optnumber(L, 3, 0);
    const char *result_type = luaL_optstring (L, 4, "SEEK_CUR");

	if (lo_lseek(my_conn->conn, lofd, offset, luaM_const(L, result_type)) > -1) {
		lua_pushboolean(L, 1);
	}
	else {
		lua_pushboolean(L, 0);
	}

	return 1;
}

static int Lpg_lo_tell (lua_State *L) {
	int offset = 0;
    lua_pg_conn *my_conn = Mget_conn (L);

	int lofd = luaL_optnumber(L, 2, my_conn->lofd);

	offset = lo_tell(my_conn->conn, lofd);
	lua_pushnumber(L, offset);

	return 1;
}

/**
* Close PgSQL connection
*/
static int Lpg_close (lua_State *L) {
    lua_pg_conn *my_conn = Mget_conn (L);
    luaL_argcheck (L, my_conn != NULL, 1, "connection expected");
    if (my_conn->closed) {
        lua_pushboolean (L, 0);
        return 1;
    }

    my_conn->closed = 1;
    luaL_unref (L, LUA_REGISTRYINDEX, my_conn->env);
    luaL_unref (L, LUA_REGISTRYINDEX, my_conn->field_types);
    PQfinish (my_conn->conn);
    lua_pushboolean (L, 1);
    return 1;
}

/**
* version info
*/
static int Lversion (lua_State *L) {
    lua_pushfstring(L, "luapgsql (%s) - PostgreSQL driver\n", LUA_PGSQL_VERSION);
    lua_pushstring(L, "(c) 2009-19 Alacner zhang <alacner@gmail.com>\n");
    lua_pushstring(L, "This content is released under the MIT License.\n");

    lua_concat (L, 3);
    return 1;
}
/**
* Creates the metatables for the objects and registers the
* driver open method.
*/
int luaopen_pgsql (lua_State *L) {
    struct luaL_reg driver[] = {
        { "connect",   Lpg_connect },
        { "version",   Lversion },
        { NULL, NULL },
    };

    struct luaL_reg result_methods[] = {
        { "field_num",   Lpg_field_num },
        { "field_name",   Lpg_field_name },
        { "field_table",   Lpg_field_table },
        { "field_size",   Lpg_field_size },
        { "field_type",   Lpg_field_type },
        { "field_type_oid",   Lpg_field_type_oid },
        { "field_is_null",   Lpg_field_is_null },
        { "field_prtlen",   Lpg_field_prtlen },
        { "fetch_row",   Lpg_fetch_row },
        { "fetch_assoc",   Lpg_fetch_assoc },
        { "fetch_array",   Lpg_fetch_array },
        { "fetch_result",   Lpg_fetch_result },
        { "fetch_all",   Lpg_fetch_all },
        { "fetch_all_columns",   Lpg_fetch_all_columns },
        { "last_oid",   Lpg_last_oid },
        { "result_error",   Lpg_result_error },
        { "result_seek",   Lpg_result_seek },
        { "result_status",   Lpg_result_status },
        { "result_error_field",   Lpg_result_error_field },
        { "free_result",   Lpg_free_result },
        { "num_fields",   Lpg_num_fields },
        { "num_rows",   Lpg_num_rows },
        { "affected_rows",   Lpg_affected_rows },
        { NULL, NULL }
    };

    struct luaL_reg connection_methods[] = {
        { "host",   Lpg_host },
        { "port",   Lpg_port },
        { "dbname",   Lpg_dbname },
        { "tty",   Lpg_tty },
        { "get_pid",   Lpg_get_pid },
        { "get_field_name",   Lpg_get_field_name },
        { "get_field_table",   Lpg_get_field_table },
        { "ping",   Lpg_ping },
        { "version",   Lpg_version },
        { "connection_status",   Lpg_connection_status },
        { "connection_busy", Lpg_connection_busy},
        { "connection_reset", Lpg_connection_reset},
        { "transaction_status",   Lpg_transaction_status },
        { "options",   Lpg_options },
        { "parameter_status",   Lpg_parameter_status },
        { "last_error",   Lpg_last_error },
        { "escape_bytea",   Lpg_escape_bytea},
        { "unescape_bytea",   Lpg_unescape_bytea},
        { "escape_string",   Lpg_escape_string},
        { "cancel_query", Lpg_cancel_query},
        { "trace", Lpg_trace},
        { "untrace", Lpg_untrace},
        { "client_encoding", Lpg_client_encoding},
        { "set_client_encoding", Lpg_set_client_encoding},
        { "set_error_verbosity", Lpg_set_error_verbosity},
        { "query",   Lpg_query },
        { "query_params",   Lpg_query_params },
        { "prepare",   Lpg_prepare },
        { "execute",   Lpg_execute },
        { "send_query",   Lpg_send_query },
        { "send_prepare",   Lpg_send_prepare },
        { "send_execute",   Lpg_send_execute },
        { "send_query_params",   Lpg_send_query_params },
        { "get_result",   Lpg_get_result },
        { "put_line",   Lpg_put_line },
        { "get_notify",   Lpg_get_notify },
        { "end_copy",   Lpg_end_copy },
        { "meta_data",   Lpg_meta_data },
        { "lo_create",   Lpg_lo_create },
        { "lo_unlink",   Lpg_lo_unlink },
        { "lo_open",   Lpg_lo_open },
        { "lo_close",   Lpg_lo_close },
        { "lo_read",   Lpg_lo_read },
        { "lo_write",   Lpg_lo_write },
        { "lo_read_all",   Lpg_lo_read_all },
        { "lo_import",   Lpg_lo_import },
        { "lo_export",   Lpg_lo_export },
        { "lo_seek",   Lpg_lo_seek },
        { "lo_tell",   Lpg_lo_tell },
        { "close",   Lpg_close },
        { NULL, NULL }
    };

    luaM_register (L, LUA_PGSQL_CONN, connection_methods);
    luaM_register (L, LUA_PGSQL_RES, result_methods);
    lua_pop (L, 2);

    luaL_register (L, LUA_PGSQL_TABLENAME, driver);

    lua_pushliteral (L, "PG_VERSION");
    lua_pushliteral (L, PG_VERSION);   
    lua_settable (L, -3);     

    return 1;
}

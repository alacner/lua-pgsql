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

#define LUA_PGSQL_CONN "PostgreSQL connection"
#define LUA_PGSQL_RES "PostgreSQL result"
#define LUA_PGSQL_TABLENAME "pgsql"

typedef struct {
    short      closed;
} pseudo_data;

typedef struct {
    short   closed;
    int     env;
    PGconn *conn;
} lua_pg_conn;

typedef struct {
    short      closed;
    int        conn;               /* reference to connection */
    int        numcols;            /* number of columns */
    int        colnames, coltypes; /* reference to column information tables */
    PGresult *res;
} lua_pg_res;

void luaM_setmeta (lua_State *L, const char *name);
int luaM_register (lua_State *L, const char *name, const luaL_reg *methods);
int luaopen_pgsql (lua_State *L);

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

static int luaM_msg(lua_State *L, const int n, const char *m) {
    if (n) {
        lua_pushnumber(L, 1);
    } else {
        lua_pushnil(L);
    }
    lua_pushstring(L, m);
    return 2;
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
* MYSQL operate functions
*/

/**
* Open a connection to a PgSQL Server
*/
static int Lpg_connect (lua_State *L) {
    lua_pg_conn *my_conn = (lua_pg_conn *)lua_newuserdata(L, sizeof(lua_pg_conn));
    luaM_setmeta (L, LUA_PGSQL_CONN);

    const char *conninfo = luaL_optstring(L, 1, "dbname = postgres");

	PGconn *conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        return luaM_msg (L, 0, PQerrorMessage(conn));
        PQfinish(conn); /* Close conn if connect failed */
    }

    /* fill in structure */
    my_conn->closed = 0;
    my_conn->env = LUA_NOREF;
    my_conn->conn = conn;

    return 1;
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

static int Lpg_last_error (lua_State *L) {
    lua_pushstring(L, PQerrorMessage(Mget_conn(L)->conn));
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
//        { "escape_string",   Lpg_escape_string },
        { "version",   Lversion },
        { NULL, NULL },
    };

    struct luaL_reg result_methods[] = {
//        { "data_seek",   Lpg_data_seek },
//        { "num_fields",   Lpg_num_fields },
//        { "num_rows",   Lpg_num_rows },
//        { "fetch_row",   Lpg_fetch_row },
//        { "fetch_assoc",   Lpg_fetch_assoc },
//        { "fetch_array",   Lpg_fetch_array },
//        { "free_result",   Lpg_free_result },
        { NULL, NULL }
    };

    static const luaL_reg connection_methods[] = {
        { "host",   Lpg_host },
        { "port",   Lpg_port },
        { "dbname",   Lpg_dbname },
        { "tty",   Lpg_tty },
        { "get_pid",   Lpg_get_pid },
        { "ping",   Lpg_ping },
        { "version",   Lpg_version },
        { "connection_status",   Lpg_connection_status },
        { "transaction_status",   Lpg_transaction_status },
        { "options",   Lpg_options },
        { "parameter_status",   Lpg_parameter_status },
        { "last_error",   Lpg_last_error },
        { "close",   Lpg_close },
        { NULL, NULL }
    };

    luaM_register (L, LUA_PGSQL_CONN, connection_methods);
    luaM_register (L, LUA_PGSQL_RES, result_methods);
    lua_pop (L, 2);
//
    luaL_register (L, LUA_PGSQL_TABLENAME, driver);

    //lua_pushliteral (L, "_PGSQLVERSION");
    //lua_pushliteral (L, MYSQL_SERVER_VERSION);   
    //lua_settable (L, -3);     
    lua_settable (L, -1);     

    return 1;
}

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

///**
//* Get the internal database type of the given column.
//*/
//static char *luaM_getcolumntype (enum enum_field_types type) {
//
//    switch (type) {
//        //case MYSQL_TYPE_ENUM: case MYSQL_TYPE_SET:
//        //    return "set";
//        //case MYSQL_TYPE_NULL:
//        //    return "null";
//        default:
//            return "undefined";
//    }
//}

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

    PGconn *conn;

    conn = PQconnectdb(conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        PQfinish(conn); /* Close conn if connect failed */
        return luaM_msg (L, 0, PQerrorMessage(conn));
    }

    /* fill in structure */
    my_conn->closed = 0;
    my_conn->env = LUA_NOREF;
    my_conn->conn = conn;

    return 1;
}

///**
//* Select a PgSQL database
//*/
//static int Lpg_select_db (lua_State *L) {
//    lua_pg_conn *my_conn = Mget_conn (L);
//    const char *db = luaL_checkstring (L, 2);
//
//    if (pg_select_db(my_conn->conn, db) != 0) {
//        return luaM_msg (L, 0, pg_error(my_conn->conn));
//    }
//    else {
//        lua_pushboolean(L, 1);
//        return 1;
//    }
//}
//
///**
//* Sets the client character set
//*/
//static int Lpg_set_charset (lua_State *L) {
//    lua_pg_conn *my_conn = Mget_conn (L);
//    const char *ncharset = luaL_checkstring (L, 2);
//    char charset[1024];
//
//    /* major_version*10000 + minor_version *100 + sub_version
//      For example, 5.1.5 is returned as 50105. 
//    */
//    unsigned long version = pg_get_server_version(my_conn->conn);
//
//    int i, j=0;
//    for (i = 0; i < strlen(ncharset); i++) {
//        if (ncharset[i] != '-') {
//            charset[j] = ncharset[i];
//            j++;
//        }
//    }
//    charset[j] = 0;
//
//    /* set charset */
//    if ( version > 41000) {
//        const char *statement1 = lua_pushfstring(L, "SET character_set_connection=%s, character_set_results=%s, character_set_client=binary", charset, charset);
//        unsigned long st_len1 = strlen(statement1);
//        if (pg_real_query(my_conn->conn, statement1, st_len1)) {
//            return luaM_msg (L, 0, pg_error(my_conn->conn));
//        }
//    }
//    else {
//        if (pg_set_character_set(my_conn->conn, charset)) {
//            return luaM_msg (L, 0, pg_error(my_conn->conn));
//        }
//    }
//
//    if ( version > 50001) {
//        const char *statement2 = "SET sql_mode=''";
//        unsigned long st_len2 = strlen(statement2);
//        if (pg_real_query(my_conn->conn, statement2, st_len2)) {
//            return luaM_msg (L, 0, pg_error(my_conn->conn));
//        }
//    }
//
//    lua_pushboolean(L, 1);
//    return 1;
//}
//
///**
//* Returns the text of the error message from previous PgSQL operation
//*/
//static int Lpg_error (lua_State *L) {
//    lua_pushstring(L, pg_error(Mget_conn(L)->conn));
//    return 1;
//}
//
///**
//* Returns the numerical value of the error message from previous PgSQL operation
//*/
//static int Lpg_errno (lua_State *L) {
//    lua_pushnumber(L, pg_errno(Mget_conn(L)->conn));
//    return 1;
//}
//
///**
//* Returns the version number of the server as an integer
//* major_version*10000 + minor_version *100 + sub_version
//* For example, 5.1.5 is returned as 50105. 
//*/
//static int Lpg_get_server_version (lua_State *L) {
//    lua_pushnumber(L, pg_get_server_version(Mget_conn(L)->conn));
//    return 1;
//}
//
///**
//* Returns a string that represents the server version number. 
//*/
//static int Lpg_get_server_info (lua_State *L) {
//    lua_pushstring(L, pg_get_server_info(Mget_conn(L)->conn));
//    return 1;
//}
//
///**
//* Get number of affected rows in previous PgSQL operation
//*/
//static int Lpg_affected_rows (lua_State *L) {
//    lua_pushnumber(L, pg_affected_rows(Mget_conn(L)->conn));
//    return 1;
//}
//
///**
//* do Send a PgSQL query
//*/
//static int Lpg_do_query (lua_State *L, int use_store) {
//    lua_pg_conn *my_conn = Mget_conn (L);
//    const char *statement = luaL_checkstring (L, 2);
//    unsigned long st_len = strlen(statement);
//    MYSQL_RES *res;
//    /* pg_query is binary unsafe, use pg_real_query */
//#if MYSQL_VERSION_ID > 32199
//    if (pg_real_query(my_conn->conn, statement, st_len)) {
//#else
//    if (pg_query(my_conn->conn, statement)) {
//#endif
//        /* error executing query */
//        return luaM_msg (L, 0, pg_error(my_conn->conn));
//    }
//    else
//    {
//        if(use_store == MYSQL_USE_RESULT) {
//            res = pg_use_result(my_conn->conn);
//        } else {
//            res = pg_store_result(my_conn->conn);
//        }
//
//        unsigned int num_cols = pg_field_count(my_conn->conn);
//
//        if (res) { /* tuples returned */
//            lua_pg_res *my_res = (lua_pg_res *)lua_newuserdata(L, sizeof(lua_pg_res));
//            luaM_setmeta (L, LUA_PGSQL_RES);
//
//            /* fill in structure */
//            my_res->closed = 0;
//            my_res->conn = LUA_NOREF;
//            my_res->numcols = num_cols;
//            my_res->colnames = LUA_NOREF;
//            my_res->coltypes = LUA_NOREF;
//            my_res->res = res;
//            lua_pushvalue (L, 1);
//            my_res->conn = luaL_ref (L, LUA_REGISTRYINDEX);
//
//            return 1;
//        }
//        else { /* pg_use_result() returned nothing; should it have? */
//            if (num_cols == 0) { /* no tuples returned */
//                /* query does not return data (it was not a SELECT) */
//                lua_pushnumber(L, pg_affected_rows(my_conn->conn));
//                return 1;
//            }
//            else { /* pg_use_result() should have returned data */
//                return luaM_msg (L, 0, pg_error(my_conn->conn));
//            }
//        }
//    }
//}
//
///**
//* Send a PgSQL query
//*/
//static int Lpg_query (lua_State *L) {
//    return Lpg_do_query(L, MYSQL_STORE_RESULT);
//}
//
///**
//*  Send an SQL query to PgSQL, without fetching and buffering the result rows
//*/
//static int Lpg_unbuffered_query (lua_State *L) {
//    return Lpg_do_query(L, MYSQL_USE_RESULT);
//}
//
///**
//* Get the ID generated from the previous INSERT operation
//*/
//static int Lpg_insert_id (lua_State *L) {
//    lua_pushnumber(L, pg_insert_id(Mget_conn(L)->conn));
//    return 1;
//}
//
///**
//* Escapes a string for use in a pg_query
//*/
//static int Lpg_escape_string (lua_State *L) {
//    const char *unescaped_string = luaL_checkstring (L, 1);
//    unsigned long st_len = strlen(unescaped_string);
//    char to[st_len*2+1]; 
//    pg_escape_string(to, unescaped_string, st_len);
//    lua_pushstring(L, to);
//    return 1;
//}
//
///**
//* Escapes special characters in a string for use in a SQL statement
//*/
//static int Lpg_real_escape_string (lua_State *L) {
//    lua_pg_conn *my_conn = Mget_conn (L);
//    const char *unescaped_string = luaL_checkstring (L, 2);
//    unsigned long st_len = strlen(unescaped_string);
//    char to[st_len*2+1]; 
//    pg_real_escape_string(my_conn->conn, to, unescaped_string, st_len);
//    lua_pushstring(L, to);
//    return 1;
//}
//
///**
//* Move internal result pointer
//*/
//static int Lpg_data_seek (lua_State *L) {
//    lua_Number offset = luaL_optnumber(L, 2, 0);
//
//    pg_data_seek(Mget_res(L)->res, offset);
//    return 0;
//}
//
///**
//* Get number of fields in result
//*/
//static int Lpg_num_fields (lua_State *L) {
//    lua_pushnumber (L, (lua_Number)pg_num_fields (Mget_res(L)->res));
//    return 1;
//}
//
///**
//* Get number of rows in result
//*/
//static int Lpg_num_rows (lua_State *L) {
//    lua_pushnumber (L, (lua_Number)pg_num_rows (Mget_res(L)->res));
//    return 1;
//}
//
///**
//* do fetch
//*/
//static int Lpg_do_fetch (lua_State *L, lua_pg_res *my_res, int result_type) {
//    MYSQL_RES *res = my_res->res;
//    unsigned long *lengths;
//
//    MYSQL_ROW row = pg_fetch_row(res);
//    if (row == NULL) {
//        lua_pushnil(L);  /* no more results */
//        return 1;
//    }
//
//    lengths = pg_fetch_lengths(res);
//
//
//    if (result_type == MYSQL_NUM) {
//        int i;
//        lua_newtable(L); /* result */
//
//        for (i = 0; i < my_res->numcols; i++) {
//            luaM_pushvalue (L, row[i], lengths[i]);
//            lua_rawseti (L, -2, i+1);
//        }
//    }
//    else {
//
//        int i,j;
//        /* Check if colnames exists */
//        if (my_res->colnames == LUA_NOREF)
//            luaM_colinfo(L, my_res);
//        lua_rawgeti (L, LUA_REGISTRYINDEX, my_res->colnames);/* Push colnames*/
//
//        lua_newtable(L); /* result */
//
//        for (i = 0; i < my_res->numcols; i++) {
//            lua_rawgeti(L, -2, i+1); /* push the field name */
//
//            /* Actually push the value */
//            luaM_pushvalue (L, row[i], lengths[i]);
//            lua_rawset (L, -3);
//        }
//
//        if (result_type == MYSQL_BOTH) {
//            /* Copy values to numerical indices */
//            for (j = 0; j < my_res->numcols; j++) {
//                lua_pushnumber(L, j+1);
//                luaM_pushvalue (L, row[j], lengths[j]);
//                lua_rawset (L, -3);
//            }
//        }
//    }
//    return 1;
//}
//
///**
//* Get a result row as an enumerated array
//*/
//static int Lpg_fetch_row (lua_State *L) {
//    return Lpg_do_fetch(L, Mget_res(L), MYSQL_NUM);
//}
//
///**
//* Get a result row as an enumerated array
//*/
//static int Lpg_fetch_assoc (lua_State *L) {
//    return Lpg_do_fetch(L, Mget_res(L), MYSQL_ASSOC);
//}
//
///**
//* Fetch a result row as an associative array, a numeric array, or both
//*/
//static int Lpg_fetch_array (lua_State *L) {
//    lua_pg_res *my_res = Mget_res (L);
//    const char *result_type = luaL_optstring (L, 2, "MYSQL_BOTH");
//
//    if ( ! strcasecmp(result_type, "MYSQL_NUM")) {
//        return Lpg_do_fetch(L, my_res, MYSQL_NUM);
//    }
//    else if ( ! strcasecmp(result_type, "MYSQL_ASSOC")) {
//        return Lpg_do_fetch(L, my_res, MYSQL_ASSOC);
//    }
//    else {
//        return Lpg_do_fetch(L, my_res, MYSQL_BOTH);
//    }
//}
//
///*
//* Rollback the current transaction.
//*/
//static int Lpg_rollback (lua_State *L) {
//    lua_pg_conn *my_conn = Mget_conn (L);
//    if (pg_rollback(my_conn->conn)) {
//        return luaM_msg (L, 0, pg_error(my_conn->conn));
//    }
//    return 0;
//}
//
///**
//* Free result memory
//*/
//static int Lpg_free_result (lua_State *L) {
//    lua_pg_res *my_res = (lua_pg_res *)luaL_checkudata (L, 1, LUA_PGSQL_RES);
//    luaL_argcheck (L, my_res != NULL, 1, "result expected");
//    if (my_res->closed) {
//        lua_pushboolean (L, 0);
//        return 1;
//    }
//
//    /* Nullify structure fields. */
//    my_res->closed = 1;
//    pg_free_result(my_res->res);
//    luaL_unref (L, LUA_REGISTRYINDEX, my_res->conn);
//    luaL_unref (L, LUA_REGISTRYINDEX, my_res->colnames);
//    luaL_unref (L, LUA_REGISTRYINDEX, my_res->coltypes);
//
//    lua_pushboolean (L, 1);
//
//    return 1;
//}
//

static int Lpg_host (lua_State *L) {
    lua_pushstring(L, PQhost(Mget_conn(L)->conn));
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
//
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
//
    static const luaL_reg connection_methods[] = {
        { "host",   Lpg_host },
//        { "error",   Lpg_error },
//        { "errno",   Lpg_errno },
//        { "select_db",   Lpg_select_db },
//        { "insert_id",   Lpg_insert_id },
//        { "set_charset",   Lpg_set_charset },
//        { "affected_rows",   Lpg_affected_rows },
//        { "get_server_info",   Lpg_get_server_info },
//        { "get_server_version",   Lpg_get_server_version },
//        { "real_escape_string",   Lpg_real_escape_string },
//        { "query",   Lpg_query },
//        { "unbuffered_query",   Lpg_unbuffered_query },
//        { "rollback",   Lpg_rollback },
        { "close",   Lpg_close },
        { NULL, NULL }
    };
//
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

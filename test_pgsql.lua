require "pgsql"
require "print_r"
--[[
CREATE TABLE IF NOT EXISTS `table` (
  `id` bigint(20) NOT NULL auto_increment,
  `hits` int(10) NOT NULL default '0',
  `time` varchar(255) NOT NULL,
  `col1` varchar(255) NOT NULL,
  `col2` text NOT NULL,
  PRIMARY KEY  (`id`)
) ENGINE=MyISAM  DEFAULT CHARSET=utf8 ;
--]]
local db, err = pgsql.connect("host=localhost dbname=test user=postgres")
print_r(db)
print_r(err)
print("---------------")
--local db, err = pgsql.connect("host=localhost dbname=test user=postgres")
local db, err = pgsql.connect("host=localhost dbname=test user=postgresql")
print_r(db)
print_r(err)
print("++++++++++++++++++++")
--print("++++++++++++++++++++")
--[====[
local eba = db:escape_bytea("INSERT INTO test_table (image) VALUES ('$image_escaped'::bytea);")
print_r(eba)
print_r(db:unescape_bytea(eba))
--print_r(db:close())
print_r(db:host())
print_r(db:dbname())
print_r(db:port())
print_r(db:tty())
print_r(db:get_pid())
print_r(db:version())
print_r(db:connection_status())
print_r(db:transaction_status())
print_r(db:options())
print_r(db:parameter_status("server_encoding"))
print_r(db:last_error())
print_r(db:ping())
local eba = db:escape_string("INSERT INTO test_table (image) VALUES ('$image_escaped%\\'::bytea);")
print_r(eba)
print_r(db:trace("/tmp/test.log"))
print_r(db:connection_busy())
print_r(db:cancel_query())
print_r(db:client_encoding())
print_r(db:set_client_encoding('gbk'))
print_r(db:client_encoding())
print_r(db:connection_reset())
local res = db:query([[INSERT INTO "public"."tbl" ("time") VALUES (NULL) ]])
print_r(res)
print_r(res:num_rows());
print_r(res:num_fields());
print_r(res:affected_rows());

print('---- line -1 -----')
local n = db:get_field_name()
print_r(n)
print_r(n[30])
print('---- line -2 -----')
print_r(db:get_field_name(30))
print_r(db:get_field_name(18))

print_r(db:set_error_verbosity("PGSQL_ERRORS_VERBOSE"));
]====]--
local res = db:query([[INSERT INTO "public"."tbl" ("time") VALUES (NULL) ]])
print_r(res)
print_r(res:num_rows())
print_r(res:num_fields())
print_r(res:affected_rows())
print('---- line -2.2 -----')
local t = db:get_field_table()
print_r(t)
print('---- line -2.4 -----')
print_r(db:get_field_table(16387))
print('---- line 1 -----')
db:send_query([[SELECT "id","time" FROM "public"."tbl"]])
local res = db:get_result()
print_r(res:result_error())
--[=====[
print('---- line 1.1 -----')
print_r(res)
print('---- line 1.2 -----')
--print_r(res:fetch_result(10, 0));
--print_r(res:fetch_result(1, 0));
print_r(res:fetch_all());
print_r(res:last_oid());
print_r(res:field_table(0, 1))
local m, n = res:field_table(0)
print_r(m)
print_r(n)
--]=====]
local f = res:fetch_row()
--local f = res:fetch_assoc()
while f do
	print_r(f)
	local p = res:field_table(0)
	print_r(p)
--print_r(db:get_field_table(p))
--print_r(db:get_field_table(p, 1))
	f = res:fetch_array("PGSQL_NUM")
	--f = res:fetch_assoc()
	--f = res:fetch_row()
end
print_r('==============')
print_r(res:result_status("PGSQL_STATUS_STRING"))
print_r('==============')
--[=====[
print_r(db:end_copy())

db:query("create table bar (a int4, b char(16), d float8)")
db:query("copy bar from stdin")
db:put_line("3\thello world\t4.5\n")
db:put_line("4\tgoodbye world\t7.11\n")
db:put_line("\\.\n")
db:end_copy()


db:query("LISTEN author_updated;")
local notify = db:get_notify("PGSQL_BOTH")
print_r(notify)
print('--------------------------------');
local a,b,c = db:meta_data('xx.tbl')
print_r(a)
print_r(b)
local a,b,c = db:meta_data('onetbl')
print_r(a)
print_r(b)
print('--------------------------------');

local a,b,c = db:meta_data('tbl')
print_r(a)
print_r(b)
print('--------------------------------');

--print_r(_G);
local f = res:fetch_assoc()
print_r(res:field_is_null('id'));
print_r(res:field_prtlen('id'));
while f do
	print_r(f)
	--f = res:fetch_assoc()
end
--]]
--local f,t = res:field_type(0);
--local f,t = res:field_type_oid(0);
--print_r(db:get_field_name(f))
print_r(f)
print_r(t)
--[====[
print_r(f)
print_r(t)
--local f = res:filed_name(0)
--print_r(res:filed_name(0))
print('---- line 1.3 -----')
print_r(res)
print('---- line 2 -----')
--print_r(db:untrace())
]====]--
]=====]--

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
local db, err = pgsql.connect("dbname=test")
print_r(db)
print_r(err)
print_r(db:host())
--[===[
print(db:select_db('testdb'))
print(db:set_charset("utf-8"))
--os.exit();
print(db:query("insert `table` (`hits`,`time`,`col1`,`col2`) values (10000, 33333, '天使', 'hehe')"))
print(db:insert_id())
print(db:affected_rows())
print(db:query("insert `table` (`dhits`,`time`,`col1`,`col2`) values (10000, 33333, '天使', 'hehe')"))
print(db:affected_rows())
print(mysql.escape_string('哈哈"'))
print(db:real_escape_string([['哈哈`"'\n]]))
os.exit();
local rs = db:query("select * from `table`")
--local rs = db:query("select * from `table` limit 30")
--local rs = db:unbuffered_query("select * from `table`")
print(rs:num_fields())
print(rs:num_rows())
--os.exit();



print('------ row ----------')
local row = rs:fetch_row()
print_r(row)
print('------ assoc ----------')
local row = rs:fetch_assoc()
print_r(row)
print('------array MYSQL_BOTH----------')
local row = rs:fetch_array("MYSQL_BOTH")
print_r(row)
print('------array MYSQL_ASSOC----------')
local row = rs:fetch_array("MYSQL_ASSOC")
print_r(row)
print('------array MYSQL_NUM----------')
local row = rs:fetch_array("MYSQL_NUM")
print_r(row)
print('------array----------')
local row = rs:fetch_array()
print_r(row)
--os.exit()
while row do
    print_r(row)
    --row = rs:fetch_array("MYSQL_NUM")
    row = rs:fetch_array("MYSQL_BOTH")
    --row = rs:fetch_array("MYSQL_ASSOC")
end

print(mysql.version());








--[[
print('----row------------')
local row = rs:fetch_row()
while row do
    print_r(row)
    row = rs:fetch_row()
end
--]]

--[[
print('--------assoc--------')
local row = rs:fetch_assoc()
while row do
    print_r(row)
    row = rs:fetch_assoc()
end
--]]

]===]

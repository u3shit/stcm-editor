-- -*- poly-lua-c++-lua -*-

local setfenv, getfenv, setmetatable, concat =
  setfenv, getfenv, setmetatable, table.concat

local utils = require("gen_binding.utils")

local cache = {}
setmetatable(cache, {__mode="v"})
local index_G_tbl = {__index=_G}

local template_tostring
local function template_code(str)
  return "'"..str:gsub("\\(.)", "%1"):
    gsub("^=(.*)$", "__tmpl_out[#__tmpl_out+1]=__tmpl_tos(%1)")..
    " __tmpl_out[#__tmpl_out+1]='"
end
local function template(inp, tbl, c)
  local fun = cache[inp]
  if not fun then
    local ninp = inp:gsub("[\\'\n]", "\\%1"):
      gsub("//$([^\n]*\\\n)", template_code):
      gsub("/%*$(.-)%*%/", template_code)
    ninp = "local __tmpl_tos, __tmpl_concat = ... return function() "..
      "local __tmpl_out = {} __tmpl_out[#__tmpl_out+1]='"..ninp..
      "' return __tmpl_concat(__tmpl_out) end"
    ninp = ninp:gsub("out%[%#out%+1%]%=''", "")
    --print(ninp)
    local err
    fun, err = loadstring(ninp)
    fun = fun and fun(template_tostring, concat)
    if not fun then
      utils.print_error("Invalid template: "..err, c)
      return
    end
    cache[inp] = fun
  end

  setfenv(fun, setmetatable(tbl, index_G_tbl))
  return fun()
end

template_tostring = function(x)
  return template(tostring(x), getfenv(2))
end

local template_str = [=[
// Auto generated code, do not edit. See gen_binding in project root.
#include "lua/user_type.hpp"

//$ for i,cls in ipairs(classes) do
namespace Neptools
{
namespace Lua
{

// class /*$= cls.name */
template<>
void TypeRegister::DoRegister</*$= cls.cpp_name */>(TypeBuilder& bld)
{
//$   local x = { cls.cpp_name }
//$   for i,v in ipairs(cls.parents) do
//$     if not v.no_inherit then x[#x+1] = v.cpp_name end
//$   end
//$   if x[2] then
    bld.Inherit</*$= table.concat(x, ", ") */>();
//$   end

//$ for _,k in ipairs(cls.methods_ord) do
//$   local v = cls.methods[k]
    bld.Add<
//$       if v[2] then --overloaded
//$         for i,m in ipairs(v) do
        Overload</*$= m.type_str */, /*$= m.value_str */>/*$= i == #v and '' or ',' */
//$         end
//$       else
        /*$= v[1].type_str */, /*$= v[1].value_str */
//$       end
    >("/*$= k */");
//$ end
/*$= cls.post_register or "" */
}
static TypeRegister::StateRegister</*$= cls.cpp_name */> reg_/*$= cls.name:gsub("%.", "_") */;

}
}

/*$ if cls.template then */template <>/*$ end */
const char /*$= cls.cpp_name */::TYPE_NAME[] = "/*$= cls.name */";

//$ end
]=]

local function generate(classes)
  return assert(template(template_str, {classes=classes}), "Generate failed")
end

return { template = template, generate = generate }

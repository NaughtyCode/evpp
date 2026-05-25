-- class.lua - Single-inheritance class system (Lua 5.5)
--
-- Each class is a callable table. Calling a class creates an instance.
-- Instance metatables use __index for method lookup and __name for tostring().
-- Single inheritance: a subclass's __index chain falls back to its superclass.
--
-- Usage:
--   local Animal = Class("Animal")
--   function Animal:__ctor(name)
--       self.name = name
--   end
--   function Animal:speak()
--       return "..."
--   end
--
--   local Dog = Class("Dog", Animal)
--   function Dog:__ctor(name, breed)
--       self:super():__ctor(name)
--       self.breed = breed
--   end
--   function Dog:speak()
--       return "Woof! I'm " .. self.name
--   end
--
--   local d = Dog("Rex", "Shepherd")
--   assert(d:isinstanceof(Dog))     -- true
--   assert(d:isinstanceof(Animal))  -- true

local function Class(classname, super)
    if super ~= nil then
        assert(type(super) == "table",
            "bad argument #2 to 'Class' (table expected, got " .. type(super) .. ")")
    end

    local cls = {}
    cls.__name = classname or "anonymous"
    if super then
        cls.__super = super
    end

    -- Metatable applied to the class table itself.
    -- __call makes cls(...) create instances.
    -- __index (when super is set) provides class-level method inheritance.
    local mt = {
        __call = function(self, ...)
            -- Each instance gets its own metatable with __index = class
            -- and __name for Lua 5.5 tostring() integration.
            local instance = setmetatable({}, {
                __index = self,
                __name  = self.__name,
            })
            local ctor = rawget(self, "__ctor")
            if ctor then
                ctor(instance, ...)
            end
            return instance
        end,
    }
    if super then
        mt.__index = super
    end
    setmetatable(cls, mt)

    -- is-instance-of check (works on both instances and classes).
    -- Walks the __index chain to see if target appears as a class ancestor.
    function cls:isinstanceof(target)
        local mt = getmetatable(self)
        while mt do
            if mt.__index == target then
                return true
            end
            mt = getmetatable(mt.__index)
        end
        return false
    end

    -- Return the superclass, if any.
    -- Callable on both instances and classes.
    function cls:super()
        local mt = getmetatable(self)
        if mt then
            return rawget(mt.__index, "__super")
        end
        return nil
    end

    return cls
end

return Class

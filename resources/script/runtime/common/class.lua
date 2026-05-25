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
--       Animal.__ctor(self, name)
--       self.breed = breed
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
    -- __name lets Lua 5.5 tostring() print the class name.
    local mt = {
        __call = function(self, ...)
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
        __name = classname or "anonymous",
    }
    if super then
        mt.__index = super
    end
    setmetatable(cls, mt)

    -- Returns true if self is an instance (or subclass instance) of target.
    -- Walks the __index chain to find target among class ancestors.
    function cls:isinstanceof(target)
        if target == nil then
            return false
        end
        local mt = getmetatable(self)
        while mt do
            if mt.__index == target then
                return true
            end
            mt = getmetatable(mt.__index)
        end
        return false
    end

    -- Returns the direct superclass, or nil.
    -- Works on both instances (looks up the instance's class, then its super)
    -- and classes (uses the __super field stored directly on the class).
    function cls:super()
        -- self might be a class: check for the direct __super field first.
        local sup = rawget(self, "__super")
        if sup then
            return sup
        end
        -- self is an instance: look up its class, then that class's __super.
        local mt = getmetatable(self)
        if mt then
            local idx = mt.__index
            if idx then
                return rawget(idx, "__super")
            end
        end
        return nil
    end

    return cls
end

return Class

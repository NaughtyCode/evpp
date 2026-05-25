-- class.lua - Single-inheritance class system (Lua 5.5)
--
-- Each class is a callable table. Calling a class creates an instance.
-- Instance metatables use __index for method lookup and __name for tostring().
-- Single inheritance: a subclass's __index chain falls back to its superclass.
--
-- Hot-reload: named classes are held in a module-level registry.
-- When Class("Name") is called and a class with that name already exists
-- (from a prior load), the existing table is reused. Method definitions
-- from the re-executed script overwrite the old ones, so both existing and
-- new instances see the latest code. The super chain is also updated to
-- point to the latest version of the parent class.
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

-- Module-level registry for hot-reload table reuse.
-- Weak values so classes that become unreferenced can be GC'd.
local _registry = setmetatable({}, { __mode = "v" })

local function Class(classname, super)
    if super ~= nil then
        assert(type(super) == "table",
            "bad argument #2 to 'Class' (table expected, got " .. type(super) .. ")")
    end

    -- Determine if this class should participate in hot-reload.
    -- Only caller-provided names go into the registry — anonymous
    -- classes (nil classname) can't be reliably looked up across modules.
    local named = classname ~= nil

    -- Hot-reload: reuse existing class table so existing instances
    -- whose __index points to it continue to work.
    local cls = named and _registry[classname]
    if cls then
        -- Resolve super to the latest registered version so the
        -- inheritance chain points to up-to-date class tables.
        if super and super.__name and _registry[super.__name] then
            super = _registry[super.__name]
        end

        cls.__name = classname
        cls.__super = super

        local cls_mt = getmetatable(cls)
        if super then
            cls_mt.__index = super
        else
            cls_mt.__index = nil
        end

        return cls
    end

    -- First load: create a new class table.
    cls = {}
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
        local sup = rawget(self, "__super")
        if sup then
            return sup
        end
        local mt = getmetatable(self)
        if mt then
            local idx = mt.__index
            if idx then
                return rawget(idx, "__super")
            end
        end
        return nil
    end

    -- Register for hot-reload.
    if named then
        _registry[classname] = cls
    end

    return cls
end

return Class

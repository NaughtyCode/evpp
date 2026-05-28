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

-- Persistent registry for hot-reload table reuse. Stored as a well-known
-- global so it survives reloads of class.lua itself. Weak values ensure
-- classes that become unreferenced can be GC'd.
local registry_key = "__class_registry__"
local _registry = rawget(_G, registry_key)
if type(_registry) ~= "table" then
    _registry = setmetatable({}, { __mode = "v" })
    rawset(_G, registry_key, _registry)
end

local function Class(classname, super)
    assert(type(classname) == "string" and classname ~= "",
        "bad argument #1 to 'Class' (non-empty string expected, got " .. type(classname) .. ")")
    if super ~= nil then
        assert(type(super) == "table",
            "bad argument #2 to 'Class' (table expected, got " .. type(super) .. ")")
    end

    -- Hot-reload: reuse existing class table so existing instances
    -- whose __index points to it continue to work.
    local cls = _registry[classname]
    if cls then
        -- Resolve super to the latest registered version so the
        -- inheritance chain points to up-to-date class tables.
        if super and super.__name and _registry[super.__name] then
            super = _registry[super.__name]
        end

        cls.__name = classname
        cls.__super = super

        -- Rebuild ancestor set for O(1) isinstanceof.
        local ancestors = {}
        local cursor = super
        while cursor do
            ancestors[cursor] = true
            cursor = rawget(cursor, "__super")
        end
        cls.__ancestors = ancestors

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
    cls.__name = classname
    if super then
        cls.__super = super
    end

    -- Build ancestor set for O(1) isinstanceof checks.
    -- Stored on the class table so all instances share it via __index.
    local ancestors = {}
    local cursor = super
    while cursor do
        ancestors[cursor] = true
        cursor = rawget(cursor, "__super")
    end
    cls.__ancestors = ancestors

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
        __name = classname,
    }
    if super then
        mt.__index = super
    end
    setmetatable(cls, mt)

    -- Returns true if self is an instance (or subclass instance) of target.
    -- O(1) hash lookup via precomputed ancestor set on the class.
    function cls:isinstanceof(target)
        if target == nil then
            return false
        end
        local inst_mt = getmetatable(self)
        if not inst_mt then
            return false
        end
        local class = inst_mt.__index
        if class == target then
            return true
        end
        local anc = rawget(class, "__ancestors")
        if anc then
            return anc[target] == true
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
    _registry[classname] = cls

    return cls
end

return Class

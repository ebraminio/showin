add_rules("mode.debug", "mode.release")

target("showin", function()
    set_kind("binary")
    add_files("showin.cc", "showin.rc")
    add_syslinks("user32", "gdi32", "advapi32")
    if is_plat("mingw") then
        add_ldflags("-mwindows", {force = true})
        add_cxxflags("-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", {force = true})
    end
end)

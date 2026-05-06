add_rules("mode.debug", "mode.release")

target("showin", function()
    set_kind("binary")
    add_files("showin.cc", "showin.rc")
    add_syslinks("user32", "gdi32", "advapi32", "kernel32")
    set_exceptions("none")
    if is_plat("mingw") then
        add_cxxflags("-fno-rtti", "-fno-threadsafe-statics", {force = true})
        add_ldflags("-mwindows", "-estart", "-nostartfiles", {force = true})
    elseif is_plat("windows") then
        add_cxxflags("/GR-", "/GS-", "/O1", "/Zc:threadSafeInit-", {force = true})
        add_ldflags("/SUBSYSTEM:WINDOWS,5.01", "/INCREMENTAL:NO", "/entry:start", "/MERGE:.rdata=.text", {force = true})
    end
end)

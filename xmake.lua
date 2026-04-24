add_rules("mode.debug", "mode.release")

target("showin", function()
    set_kind("binary")
    add_files("showin.cc", "showin.rc")
    add_syslinks("user32", "gdi32")
end)

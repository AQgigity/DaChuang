idf_component_register(
    SRCS "ui.c"
         "ui_helpers.c"
         "screens/ui_Screen1.c"
         "components/ui_comp_hook.c"
    INCLUDE_DIRS "." "screens" "components"
    REQUIRES lvgl
)

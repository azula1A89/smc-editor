#include <gvc/gvplugin.h>

extern "C" {
extern gvplugin_library_t gvplugin_dot_layout_LTX_library;
extern gvplugin_library_t gvplugin_pango_LTX_library;

lt_symlist_t lt_preloaded_symbols[] = {
    {"gvplugin_dot_layout_LTX_library", &gvplugin_dot_layout_LTX_library},
    {"gvplugin_pango_LTX_library", &gvplugin_pango_LTX_library},
    {nullptr, nullptr},
};
}

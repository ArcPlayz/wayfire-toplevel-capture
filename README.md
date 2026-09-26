# wayfire-foreign-toplevel
This Wayfire plugin implements the `ext_foreign_toplevel_image_capture_source_manager_v1` protocol.

It allows windows (toplevels) to be captured separately by clients like [`xdg-desktop-portal-wlr`](https://github.com/emersion/xdg-desktop-portal-wlr).
## Installation:
```
meson build; sudo ninja -C build install
```

You may also need to add this environment variable:

```
export WAYFIRE_PLUGIN_PATH="/usr/local/lib/wayfire"
```

To uninstall the plugin run:

```
sudo ninja -C build uninstall
```

or remove these files:
```
/usr/local/lib/wayfire/libtoplevel-capture.so
/usr/share/wayfire/metadata/toplevel-capture.xml
```
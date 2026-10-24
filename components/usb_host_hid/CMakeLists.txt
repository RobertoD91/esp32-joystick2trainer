# Vendored copy of espressif/usb_host_hid 1.2.1 (Apache-2.0) with an added interrupt OUT
# endpoint API (see NOTICE.md). The `usb` host library comes from the component registry on
# ESP-IDF >= 6.0 (declared in idf_component.yml) and from the IDF tree on older releases.
set(requires "")
if(${IDF_VERSION_MAJOR} LESS 6)
    list(APPEND requires usb)
endif()

idf_component_register(SRCS "hid_host.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "${requires}")

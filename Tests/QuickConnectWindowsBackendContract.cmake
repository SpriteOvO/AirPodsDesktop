file(READ "${SOURCE_FILE}" source)

set(required_fragments
    "connectedConnector->QueryInterface("
    "part->GetTopologyObject("
    "connectedTopology->GetDeviceId("
    "connectedDevice->Activate("
    "KSPROPERTY_TYPE_GET"
)

foreach(fragment IN LISTS required_fragments)
    string(FIND "${source}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Windows quick connect is missing the required device-topology step: ${fragment}")
    endif()
endforeach()

string(FIND "${source}" "connectedConnector->QueryInterface(\n        ksControl.GetIID()" direct_ks_query)
if(NOT direct_ks_query EQUAL -1)
    message(FATAL_ERROR "Windows quick connect must activate IKsControl on the connected IMMDevice, not query it from IConnector")
endif()

string(FIND "${source}" "KSPROPERTY_TYPE_SET" reconnect_set_request)
if(NOT reconnect_set_request EQUAL -1)
    message(FATAL_ERROR "KSPROPERTY_ONESHOT_RECONNECT must be requested with KSPROPERTY_TYPE_GET")
endif()

file(READ "${SETTINGS_SOURCE_FILE}" settings_source)
string(FIND "${settings_source}" "tray_quick_connect_device_id.clear()" destructive_selection_clear)
if(NOT destructive_selection_clear EQUAL -1)
    message(FATAL_ERROR "Refreshing Settings must not clear a saved quick-connect device after transient enumeration failure")
endif()

foreach(gui_source_file IN ITEMS "${TRAY_SOURCE_FILE}" "${SETTINGS_WINDOW_SOURCE_FILE}")
    file(READ "${gui_source_file}" gui_source)
    string(FIND "${gui_source}" "ApdApp" application_singleton_access)
    if(NOT application_singleton_access EQUAL -1)
        message(FATAL_ERROR "GUI library code must receive quick-connect dependencies explicitly: ${gui_source_file}")
    endif()
endforeach()

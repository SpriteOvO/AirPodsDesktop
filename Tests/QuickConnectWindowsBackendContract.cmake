file(READ "${SOURCE_FILE}" source)

set(required_fragments
    "connectedConnector->QueryInterface("
    "part->GetTopologyObject("
    "connectedTopology->GetDeviceId("
    "connectedDevice->Activate("
)

foreach(fragment IN LISTS required_fragments)
    string(FIND "${source}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Windows quick connect is missing the required device-topology step: ${fragment}")
    endif()
endforeach()

string(FIND "${source}" "connectedConnector->QueryInterface(\n+        ksControl.GetIID()" direct_ks_query)
if(NOT direct_ks_query EQUAL -1)
    message(FATAL_ERROR "Windows quick connect must activate IKsControl on the connected IMMDevice, not query it from IConnector")
endif()

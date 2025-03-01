lldb_tablegen(ProcessGDBRemoteProperties.inc -gen-lldb-property-defs
  SOURCE ProcessGDBRemoteProperties.td
  TARGET LLDBPluginProcessGDBRemotePropertiesGen)

lldb_tablegen(ProcessGDBRemotePropertiesEnum.inc -gen-lldb-property-enum-defs
  SOURCE ProcessGDBRemoteProperties.td
  TARGET LLDBPluginProcessGDBRemotePropertiesEnumGen)

set(LLDB_PLUGINS
  lldbPluginProcessUtility
  lldbPluginPlatformMacOSX
)

if(HAVE_LIBCOMPRESSION)
  set(LIBCOMPRESSION compression)
endif()

add_lldb_library(lldbPluginProcessGDBRemote PLUGIN
  GDBRemoteClientBase.cpp
  GDBRemoteCommunication.cpp
  GDBRemoteCommunicationClient.cpp
  GDBRemoteCommunicationHistory.cpp
  GDBRemoteCommunicationServer.cpp
  GDBRemoteCommunicationServerCommon.cpp
  GDBRemoteCommunicationServerLLGS.cpp
  GDBRemoteCommunicationServerPlatform.cpp
  GDBRemoteRegisterContext.cpp
  ProcessGDBRemote.cpp
  ProcessGDBRemoteLog.cpp
  ThreadGDBRemote.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbDataFormatters
    lldbHost
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbUtility
    ${LLDB_PLUGINS}
    ${LIBCOMPRESSION}
  LINK_COMPONENTS
    Support
  )

add_dependencies(lldbPluginProcessGDBRemote
  LLDBPluginProcessGDBRemotePropertiesGen
  LLDBPluginProcessGDBRemotePropertiesEnumGen)

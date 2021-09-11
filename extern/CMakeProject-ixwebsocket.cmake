#set(CMAKE_CXX_STANDARD 11)
#set(CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

set(IXW_DIR "${SM_EXTERN_DIR}/IXWebSocket-11.3.2/ixwebsocket")

list(APPEND IXW_SRC
            "${IXW_DIR}/IXBench.cpp"
            "${IXW_DIR}/IXCancellationRequest.cpp"
            "${IXW_DIR}/IXConnectionState.cpp"
            "${IXW_DIR}/IXDNSLookup.cpp"
            "${IXW_DIR}/IXExponentialBackoff.cpp"
            "${IXW_DIR}/IXGetFreePort.cpp"
            "${IXW_DIR}/IXGzipCodec.cpp"
            "${IXW_DIR}/IXHttp.cpp"
            "${IXW_DIR}/IXHttpClient.cpp"
            "${IXW_DIR}/IXHttpServer.cpp"
            "${IXW_DIR}/IXNetSystem.cpp"
            "${IXW_DIR}/IXSelectInterrupt.cpp"
            "${IXW_DIR}/IXSelectInterruptFactory.cpp"
            "${IXW_DIR}/IXSelectInterruptPipe.cpp"
            "${IXW_DIR}/IXSetThreadName.cpp"
            "${IXW_DIR}/IXSocket.cpp"
            "${IXW_DIR}/IXSocketConnect.cpp"
            "${IXW_DIR}/IXSocketFactory.cpp"
            "${IXW_DIR}/IXSocketServer.cpp"
            "${IXW_DIR}/IXSocketTLSOptions.cpp"
            "${IXW_DIR}/IXStrCaseCompare.cpp"
            "${IXW_DIR}/IXUdpSocket.cpp"
            "${IXW_DIR}/IXUrlParser.cpp"
            "${IXW_DIR}/IXUuid.cpp"
            "${IXW_DIR}/IXUserAgent.cpp"
            "${IXW_DIR}/IXWebSocket.cpp"
            "${IXW_DIR}/IXWebSocketCloseConstants.cpp"
            "${IXW_DIR}/IXWebSocketHandshake.cpp"
            "${IXW_DIR}/IXWebSocketHttpHeaders.cpp"
            "${IXW_DIR}/IXWebSocketPerMessageDeflate.cpp"
            "${IXW_DIR}/IXWebSocketPerMessageDeflateCodec.cpp"
            "${IXW_DIR}/IXWebSocketPerMessageDeflateOptions.cpp"
            "${IXW_DIR}/IXWebSocketProxyServer.cpp"
            "${IXW_DIR}/IXWebSocketServer.cpp"
            "${IXW_DIR}/IXWebSocketTransport.cpp")

list(APPEND IXW_HPP
            "${IXW_DIR}/IXBench.h"
            "${IXW_DIR}/IXCancellationRequest.h"
            "${IXW_DIR}/IXConnectionState.h"
            "${IXW_DIR}/IXDNSLookup.h"
            "${IXW_DIR}/IXExponentialBackoff.h"
            "${IXW_DIR}/IXGetFreePort.h"
            "${IXW_DIR}/IXGzipCodec.h"
            "${IXW_DIR}/IXHttp.h"
            "${IXW_DIR}/IXHttpClient.h"
            "${IXW_DIR}/IXHttpServer.h"
            "${IXW_DIR}/IXNetSystem.h"
            "${IXW_DIR}/IXProgressCallback.h"
            "${IXW_DIR}/IXSelectInterrupt.h"
            "${IXW_DIR}/IXSelectInterruptFactory.h"
            "${IXW_DIR}/IXSelectInterruptPipe.h"
            "${IXW_DIR}/IXSetThreadName.h"
            "${IXW_DIR}/IXSocket.h"
            "${IXW_DIR}/IXSocketConnect.h"
            "${IXW_DIR}/IXSocketFactory.h"
            "${IXW_DIR}/IXSocketServer.h"
            "${IXW_DIR}/IXSocketTLSOptions.h"
            "${IXW_DIR}/IXStrCaseCompare.h"
            "${IXW_DIR}/IXUdpSocket.h"
            "${IXW_DIR}/IXUniquePtr.h"
            "${IXW_DIR}/IXUrlParser.h"
            "${IXW_DIR}/IXUuid.h"
            "${IXW_DIR}/IXUtf8Validator.h"
            "${IXW_DIR}/IXUserAgent.h"
            "${IXW_DIR}/IXWebSocket.h"
            "${IXW_DIR}/IXWebSocketCloseConstants.h"
            "${IXW_DIR}/IXWebSocketCloseInfo.h"
            "${IXW_DIR}/IXWebSocketErrorInfo.h"
            "${IXW_DIR}/IXWebSocketHandshake.h"
            "${IXW_DIR}/IXWebSocketHandshakeKeyGen.h"
            "${IXW_DIR}/IXWebSocketHttpHeaders.h"
            "${IXW_DIR}/IXWebSocketInitResult.h"
            "${IXW_DIR}/IXWebSocketMessage.h"
            "${IXW_DIR}/IXWebSocketMessageType.h"
            "${IXW_DIR}/IXWebSocketOpenInfo.h"
            "${IXW_DIR}/IXWebSocketPerMessageDeflate.h"
            "${IXW_DIR}/IXWebSocketPerMessageDeflateCodec.h"
            "${IXW_DIR}/IXWebSocketPerMessageDeflateOptions.h"
            "${IXW_DIR}/IXWebSocketProxyServer.h"
            "${IXW_DIR}/IXWebSocketSendInfo.h"
            "${IXW_DIR}/IXWebSocketServer.h"
            "${IXW_DIR}/IXWebSocketTransport.h"
            "${IXW_DIR}/IXWebSocketVersion.h")

if(APPLE)
  list(APPEND IXW_SRC "${IXW_DIR}/IXSocketAppleSSL.cpp")
  list(APPEND IXW_HPP "${IXW_DIR}/IXSocketAppleSSL.h")
else()
  list(APPEND IXW_SRC "${IXW_DIR}/IXSocketOpenSSL.cpp")
  list(APPEND IXW_HPP "${IXW_DIR}/IXSocketOpenSSL.h")
endif()

source_group("" FILES ${IXW_SRC} ${IXW_HPP})

add_library("ixwebsocket" STATIC ${IXW_SRC} ${IXW_HPP})

set_property(TARGET "ixwebsocket" PROPERTY FOLDER "External Libraries")

disable_project_warnings("ixwebsocket")

sm_add_compile_definition("ixwebsocket" IXWEBSOCKET_USE_TLS)

if(APPLE)
  sm_add_compile_definition("ixwebsocket" IXWEBSOCKET_USE_SECURE_TRANSPORT)
  target_link_libraries(ixwebsocket "-framework foundation" "-framework security")
else()
  sm_add_compile_definition("ixwebsocket" IXWEBSOCKET_USE_OPEN_SSL)

  # Use OPENSSL_ROOT_DIR CMake variable if you need to use your own openssl
  find_package(OpenSSL REQUIRED)
  message(STATUS "OpenSSL: " ${OPENSSL_VERSION})

  add_definitions(${OPENSSL_DEFINITIONS})
  target_include_directories(ixwebsocket PUBLIC ${OPENSSL_INCLUDE_DIR})
  target_link_libraries(ixwebsocket ${OPENSSL_LIBRARIES})
endif()

sm_add_compile_definition("ixwebsocket" IXWEBSOCKET_USE_ZLIB)

if(WIN32)
  target_link_libraries(ixwebsocket wsock32 ws2_32 shlwapi Crypt32)
  sm_add_compile_definition(ixwebsocket _CRT_SECURE_NO_WARNINGS)
endif()

if(UNIX)
  find_package(Threads)
  target_link_libraries(ixwebsocket ${CMAKE_THREAD_LIBS_INIT})
endif()

target_include_directories("ixwebsocket" PUBLIC "${IXW_DIR}")

set(IXWEBSOCKET_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/IXWebSocket-11.3.2")

target_include_directories(ixwebsocket PUBLIC
  "zlib"
  $<BUILD_INTERFACE:${IXWEBSOCKET_INCLUDE_DIRS}/>
  $<INSTALL_INTERFACE:include/ixwebsocket>
)

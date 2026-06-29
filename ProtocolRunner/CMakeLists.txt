cmake_minimum_required(VERSION 3.28)
project(NebulaProtocolRunner VERSION 1.0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROTOCOL_DIR "${CMAKE_SOURCE_DIR}/Protocol")
set(BASE_DIR     "${CMAKE_SOURCE_DIR}/Base")

# Protocol sources — DLL 빌드용으로 모두 재수집
set(PROTOCOL_SOURCES
    ${PROTOCOL_DIR}/Socket/TcpSocket.cpp
    ${PROTOCOL_DIR}/Socket/TlsSocket.cpp
    ${PROTOCOL_DIR}/Socket/UdpSocket.cpp

    ${PROTOCOL_DIR}/Ipc/Pipe.cpp
    ${PROTOCOL_DIR}/Ipc/SharedMemory.cpp
    ${PROTOCOL_DIR}/Ipc/MessageQueue.cpp
    ${PROTOCOL_DIR}/Ipc/Semaphore.cpp

    ${PROTOCOL_DIR}/Ftp/FtpClient.cpp
    ${PROTOCOL_DIR}/Sftp/SftpClient.cpp

    ${PROTOCOL_DIR}/Http/HttpUtil.cpp
    ${PROTOCOL_DIR}/Http/Client/Socket.cpp
    ${PROTOCOL_DIR}/Http/Client/ConnectionPool.cpp
    ${PROTOCOL_DIR}/Http/Client/Request.cpp
    ${PROTOCOL_DIR}/Http/Client/Response.cpp
    ${PROTOCOL_DIR}/Http/Server/Socket.cpp
    ${PROTOCOL_DIR}/Http/Server/Server.cpp
    ${PROTOCOL_DIR}/Http/Server/Response.cpp
)

if (WIN32)
    list(APPEND PROTOCOL_SOURCES
        ${PROTOCOL_DIR}/Socket/Windows/Secure/SchannelClient.cpp
        ${PROTOCOL_DIR}/Socket/Windows/Secure/SchannelServer.cpp
        ${PROTOCOL_DIR}/Socket/Windows/Secure/TlsMessageBuffer.cpp
    )
endif()

# NebulaProtocol.dll
add_library(NebulaProtocolDll SHARED ${PROTOCOL_SOURCES})
set_target_properties(NebulaProtocolDll PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    OUTPUT_NAME "NebulaProtocol"
)
target_include_directories(NebulaProtocolDll PUBLIC
    ${PROTOCOL_DIR}
    ${BASE_DIR}
    ${CMAKE_BINARY_DIR}/Protocol
    $<TARGET_PROPERTY:libssh2_static,INTERFACE_INCLUDE_DIRECTORIES>
)
if (WIN32)
    target_link_libraries(NebulaProtocolDll PRIVATE
        NebulaBase Ws2_32 crypt32 bcrypt libssh2_static)
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(NebulaProtocolDll PRIVATE
        NebulaBase OpenSSL::SSL OpenSSL::Crypto pthread rt libssh2_static)
endif()

# Runner 실행 파일
add_executable(NebulaProtocolRunner main.cpp)
target_include_directories(NebulaProtocolRunner PRIVATE ${PROTOCOL_DIR} ${BASE_DIR})
target_link_libraries(NebulaProtocolRunner PRIVATE NebulaProtocolDll)
if (WIN32)
    target_link_libraries(NebulaProtocolRunner PRIVATE Ws2_32)
endif()

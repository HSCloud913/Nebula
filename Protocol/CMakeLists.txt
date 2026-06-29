cmake_minimum_required(VERSION 3.28)
project(NebulaProtocol
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header only 항목만 추가
set(HEADERS
        Socket/SocketBase.h
        Http/HttpBase.h
        Http/Interface/IParseHeaders.h
)

set(SOURCES
        Socket/TcpSocket.cpp
        Socket/TlsSocket.cpp
        Socket/UdpSocket.cpp

        Ipc/Pipe.cpp
        Ipc/SharedMemory.cpp
        Ipc/MessageQueue.cpp
        Ipc/Semaphore.cpp

        Ftp/FtpClient.cpp

        Sftp/SftpClient.cpp

        Http/HttpUtil.cpp
        Http/Client/Socket.cpp
        Http/Client/ConnectionPool.cpp
        Http/Client/Request.cpp
        Http/Client/Response.cpp
        Http/Server/Socket.cpp
        Http/Server/Server.cpp
        Http/Server/Response.cpp
)

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Base)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Protocol)

if (WIN32)
    # Header only 항목만 추가
    set(WINDOWS_HEADERS
            Socket/Windows/Base/WindowsSocketBase.h
            Socket/Windows/Secure/SspiWrapper.h
    )

    set(WINDOWS_SOURCES
            Socket/Windows/Secure/SchannelClient.cpp
            Socket/Windows/Secure/SchannelServer.cpp
            Socket/Windows/Secure/TlsMessageBuffer.cpp
    )

    target_sources(${PROJECT_NAME} PRIVATE ${WINDOWS_HEADERS} ${WINDOWS_SOURCES})
    target_link_libraries(${PROJECT_NAME} PRIVATE Ws2_32 crypt32 bcrypt libssh2_static NebulaBase)
else ()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto pthread rt libssh2_static NebulaBase)
endif ()

add_subdirectory(Test)
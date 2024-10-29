cmake_minimum_required(VERSION 3.28)
project(Protocol
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
        main.cpp

        Json/Json.cpp
        Json/JsonValue.cpp

#        Socket/Socket.cpp
#        Socket/TlsSocket.cpp
#
#        Http/HttpUtil.cpp
#        Http/Client/Socket.cpp
#        Http/Client/Request.cpp
#        Http/Client/Response.cpp
#        Http/Server/Socket.cpp
#        Http/Server/Server.cpp
#        Http/Server/Response.cpp
)

set(HEADERS
        Json/Json.h
        Json/JsonValue.h

        #        Socket/Socket.h
#        Socket/SocketBase.h
#        Socket/TlsSocket.h
#
#        Http/HttpBase.h
#        Http/HttpUtil.h
#        Http/Client/Socket.h
#        Http/Client/Request.h
#        Http/Client/Response.h
#        Http/Interface/IParseHeaders.h
#        Http/Server/Socket.h
#        Http/Server/Server.h
#        Http/Server/Response.h
)

# add_library(${PROJECT_NAME} ${SOURCES} ${HEADERS})
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Base)

if (WIN32)
    #[[ Nebula protocol (windows only) ]]
    set(WINDOWS_SOURCES
#            Socket/Windows/Secure/SchannelClient.cpp
#            Socket/Windows/Secure/SchannelServer.cpp
#            Socket/Windows/Secure/TlsMessageBuffer.cpp
    )

    set(WINDOWS_HEADERS
#            Socket/Windows/Base/WindowsSocketBase.h
#            Socket/Windows/Secure/SchannelBase.h
#            Socket/Windows/Secure/SchannelClient.h
#            Socket/Windows/Secure/SchannelServer.h
#            Socket/Windows/Secure/SspiWrapper.h
#            Socket/Windows/Secure/TlsMessageBuffer.h
    )

    target_sources(${PROJECT_NAME} PRIVATE ${WINDOWS_SOURCES} ${WINDOWS_HEADERS})
    target_link_libraries(${PROJECT_NAME} PRIVATE Ws2_32 crypt32 NebulaBase)
else ()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto pthread NebulaBase)
endif ()

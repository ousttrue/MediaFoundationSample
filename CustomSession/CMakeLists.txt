ADD_EXECUTABLE(CustomSession WIN32
    customplayer.h
    customplayer.cpp
    customplayer.rc
    resource.h
    winmain.cpp
    )

TARGET_COMPILE_DEFINITIONS(CustomSession PUBLIC
    UNICODE=1
    _UNICODE=1
    )

TARGET_LINK_LIBRARIES(CustomSession
    Mf
    Mfplat
    Mfuuid
    strmiids
    )


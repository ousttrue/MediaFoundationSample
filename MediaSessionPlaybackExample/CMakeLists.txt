ADD_EXECUTABLE(MediaSessionPlaybackExample WIN32
    player.h
    player.cpp
    player.rc
    resource.h
    winmain.cpp
    )

TARGET_COMPILE_DEFINITIONS(MediaSessionPlaybackExample PUBLIC
    UNICODE=1
    _UNICODE=1
    )

TARGET_LINK_LIBRARIES(MediaSessionPlaybackExample
    Mf
    Mfplat
    Mfuuid
    strmiids
    )


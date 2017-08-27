FILE(GLOB FILES *.h *.cpp *.def *.rc)

ADD_LIBRARY(CustomVideoRenderer SHARED
    ${FILES}
    )

TARGET_COMPILE_DEFINITIONS(CustomVideoRenderer PUBLIC
    UNICODE=1
    _UNICODE=1
    )

TARGET_LINK_LIBRARIES(CustomVideoRenderer
    Mf
    Mfplat
    Mfuuid
    strmiids
    wmcodecdspuuid
    )


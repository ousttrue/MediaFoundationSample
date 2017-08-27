#pragma once
#include <windows.h>

// {8C5C51AD-F400-4B2A-BD36-4990D07420B4}
DEFINE_GUID(CLSID_CustomVideoRenderer, 
0x8c5c51ad, 0xf400, 0x4b2a, 0xbd, 0x36, 0x49, 0x90, 0xd0, 0x74, 0x20, 0xb4);

STDAPI CreateCustomVideoRenderer(REFIID riid, void **ppvObject);


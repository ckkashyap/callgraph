set pinpath=d:\pin-2.13-62141-msvc11-windows

cl /MT /EHs- /EHa- /wd4530 /DTARGET_WINDOWS /DBIGARRAY_MULTIPLIER=1 /D_CRT_SECURE_NO_DEPRECATE /D_SECURE_SCL=0 /nologo /Gy /DTARGET_IA32 /DHOST_IA32  /I%pinpath%/source/include/pin /I%pinpath%/source/include/pin/gen /I%pinpath%/extras/components/include /I%pinpath%/extras/xed2-ia32/include /I%pinpath%/source/tools/InstLib /O2  /c /Fopintool.obj pintool.cpp
link /DLL /EXPORT:main /NODEFAULTLIB /NOLOGO /INCREMENTAL:NO /MACHINE:x86 /ENTRY:Ptrace_DllMainCRTStartup@12 /BASE:0x55000000 /OPT:REF  /out:pintool.dll pintool.obj  /LIBPATH:%pinpath%/ia32/lib /LIBPATH:%pinpath%/ia32/lib-ext /LIBPATH:%pinpath%/extras/xed2-ia32/lib pin.lib libxed.lib libcpmt.lib libcmt.lib pinvm.lib kernel32.lib ntdll-32.lib



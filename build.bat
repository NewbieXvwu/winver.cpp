windres --input-format=rc --output-format=coff --target=pe-i386 winver.rc -o winver.res
i686-w64-mingw32-g++ -o winver.exe winver.cpp winver.res -mwindows -march=i386 -mtune=pentium3 -std=gnu++17 -D_WIN32_WINDOWS=0x0410 -D_WIN32_WINNT=0x0501 -Wl,--subsystem=windows -static-libgcc -static-libstdc++ -lcomctl32 -lkernel32 -luser32 -ladvapi32 -lshcore -lgdi32 -lole32
pause
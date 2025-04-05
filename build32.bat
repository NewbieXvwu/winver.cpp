windres --input-format=rc --output-format=coff --target=pe-i386 winver.rc -o winver.res
i686-w64-mingw32-g++ -o winver-x86.exe winver.cpp winver.res -mwindows -march=pentium -std=gnu++17 -D_WIN32_WINDOWS=0x0400 -Wl,--subsystem=windows -static-libgcc -static-libstdc++ -static -lcomctl32 -lkernel32 -luser32 -lgdi32 -lole32 -lshlwapi -ladvapi32 -Wl,--gc-sections -O3
pause

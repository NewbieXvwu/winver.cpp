windres --input-format=rc --output-format=coff --target=pe-x86-64 winver.rc -o winver.res
x86_64-w64-mingw32-g++ -o winver-x64.exe winver.cpp winver.res -mwindows -march=x86-64 -mtune=generic -std=gnu++17 -D_WIN32_WINDOWS=0x0410 -D_WIN32_WINNT=0x0502 -Wl,--subsystem=windows -static-libgcc -static-libstdc++ -lcomctl32 -lkernel32 -luser32 -ladvapi32 -lshcore -lgdi32 -lole32 -ldwmapi -luxtheme -lshlwapi -Wl,--gc-sections -Ofast -funroll-loops -fpeel-loops -fpredictive-commoning -floop-interchange -floop-unroll-and-jam -finline-functions -fipa-cp -fipa-ra -fdevirtualize -foptimize-sibling-calls -ffast-math -fomit-frame-pointer -freorder-blocks -freorder-functions -fstrength-reduce -ftree-vectorize
pause

@echo off
rem Build the dsb task manager from anywhere (no MSYS2 shell needed).
setlocal
set "BASH=C:\msys64\usr\bin\bash.exe"
if not exist "%BASH%" (
  echo MSYS2 bash not found at %BASH%
  exit /b 1
)
"%BASH%" -lc "export MSYSTEM=UCRT64; export PATH=/ucrt64/bin:/usr/bin:$PATH; cd /c/Users/zengrong/Documents/workspace/task/dsb && ./build.sh %*"
endlocal

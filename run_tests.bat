@echo off
rem Builds everything, runs every C++ test suite and the NetworkX cross-check on every dataset.
rem Exits with an error code if anything fails.

cd /d "%~dp0"
call "%~dp0build.bat" || exit /b 1

set FAILED=0
for %%t in (test_day2 test_day3 test_day4 test_eval) do (
    echo.
    echo === %%t
    bin\%%t.exe . || set FAILED=1
)
echo.
echo === test_day6
bin\test_day6.exe || set FAILED=1

for %%d in (toy demo city large) do (
    echo.
    echo === NetworkX cross-check: data\%%d
    python cpp\tests\verify_with_networkx.py data/%%d || set FAILED=1
)

echo.
if %FAILED%==0 (echo ALL TESTS PASSED) else (echo SOME TESTS FAILED & exit /b 1)

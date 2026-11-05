@echo off
rem Builds the FoodLoop C++ engine and tests into bin\. Run from the project root.
rem Requires g++ with C++14 support.

cd /d "%~dp0"
if not exist bin mkdir bin

set SRC=cpp\src\data.cpp cpp\src\graph.cpp cpp\src\dijkstra.cpp cpp\src\greedy.cpp cpp\src\feasibility.cpp cpp\src\mcmf.cpp cpp\src\allocation.cpp cpp\src\knapsack.cpp cpp\src\trips.cpp cpp\src\metrics.cpp
set FLAGS=-std=c++14 -O2 -Wall -Wextra -Icpp\include

g++ %FLAGS% %SRC% cpp\src\main.cpp -o bin\foodloop.exe || exit /b 1
g++ %FLAGS% %SRC% cpp\tests\test_day2.cpp -o bin\test_day2.exe || exit /b 1
g++ %FLAGS% %SRC% cpp\tests\test_day3.cpp -o bin\test_day3.exe || exit /b 1
g++ %FLAGS% %SRC% cpp\tests\test_day4.cpp -o bin\test_day4.exe || exit /b 1
g++ %FLAGS% %SRC% cpp\tests\test_eval.cpp -o bin\test_eval.exe || exit /b 1
g++ %FLAGS% %SRC% cpp\tests\test_day6.cpp -o bin\test_day6.exe || exit /b 1

echo Build OK: bin\foodloop.exe and 5 test programs

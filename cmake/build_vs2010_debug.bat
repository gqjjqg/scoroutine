cd ..
mkdir build
cd build
cmake .. -DCMAKE_CONFIGURATION_TYPES=Debug -G"Visual Studio 10 2010" -DTEST=1
pause
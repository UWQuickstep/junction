sudo apt-get install git -y
sudo apt-get install vim -y
sudo apt-get install feh -y
sudo apt-get install cmake -y
sudo apt-get install python-cairo -y
cd ..
git clone https://github.com/efficient/libcuckoo.git
cd libcuckoo
mkdir build
cd build
cmake -D CMAKE_BUILD_TYPE=Release ..
cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_EXAMPLES=1 -DBUILD_TESTS=1 ..
make all -j
sudo make install
make test
cd ../..
git clone https://github.com/preshing/turf.git
cd junction
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DJUNCTION_WITH_LIBCUCKOO=1 ..
make all -j
sudo make install
cd ../samples/MapScalabilityTests
python TestAllMaps.py
python RenderGraphs.py

#figure out how to unoptimize the code to use gdb 
#then work on pinning threads to cpus
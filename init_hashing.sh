sudo apt-get update
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
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_EXAMPLES=1 -DBUILD_TESTS=1 ..
make all -j
sudo make install
make test
cd ../..
git clone https://github.com/preshing/turf.git
cd junction
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DJUNCTION_WITH_LIBCUCKOO=1 ..
make all -j
sudo make install
cd ../samples/MapScalabilityTests
python TestAllMaps.py ../..
python RenderGraphs.py

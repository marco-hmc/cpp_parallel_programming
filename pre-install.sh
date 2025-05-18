sudo apt update
sudo apt install libspdlog-dev
sudo apt install libgtest-dev


git clone https://github.com/google/benchmark.git
git clone https://github.com/google/googletest.git benchmark/googletest

cd benchmark
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_ENABLE_GTEST_TESTS=OFF ..
make -j$(nproc)
sudo make install
sudo ldconfig  # 更新共享库缓存


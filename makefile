.PHONY: config build clean

PrefixPath:="G:/Local/lib/Boost/boost_1_69_0-msvc-14.1;H:/Projects/FoneTool/installed;H:/Projects/FoneTool/installed/vcpkg/installed"
BuildConfig:=Debug
# BuildConfig:=RelWithDebInfo

config: clean
	@echo "Configuring..."
	mkdir -p ./build/.build_static_static_x86_vc14.1_xp
	cmake -G "Visual Studio 17 2022" -A Win32 -T v141_xp \
	-S . -B ./build/.build_static_static_x86_vc14.1_xp \
	-DCMAKE_MODULE_PATH:PATH="H:/Projects/FoneTool/cmake" \
	-DCMAKE_PREFIX_PATH:STRING=$(PrefixPath) \
	-DCMAKE_INSTALL_PREFIX:PATH="../downloader_0.2.5.0_static_static_x86_vc14.1_xp" \
	-DCMAKE_TOOLCHAIN_FILE:STRING="H:/Projects/FoneTool/installed/vcpkg/scripts/buildsystems/vcpkg.cmake" \
	-DDOWNLOADER_STATIC_RUNTIME="ON" \
	-DDOWNLOADER_BUILD_SHARED_LIB="OFF" \
	-DDOWNLOADER_BUILD_EXAMPLE="OFF" \
	-DDOWNLOADER_ADDITIONAL_INCLUDE_DIR="H:/Projects/FoneTool/include"

build:
	@echo "Building..."
	cd ./build/.build_static_static_x86_vc14.1_xp && cmake --build . --config $(BuildConfig)
	cd ./build/.build_static_static_x86_vc14.1_xp && cmake --install . --config $(BuildConfig)

clean:
	@echo "Cleaning..."
	rm -rf ./build/.build_*/
	rm -rf ./build/.cmake
	rm -rf ./build/CMakeFiles
	rm -rf ./build/CMakeCache.txt
	rm -rf ./build/downloader_*/

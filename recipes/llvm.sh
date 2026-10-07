#!/bin/sh
set -eu
umask 022
rm -rf /work/llvm && mkdir -p /work/llvm && cd /work/llvm
tar --no-same-owner -xf /src/llvm-project-15.0.7.src.tar.xz --strip-components=1
export CXXFLAGS="$CXXFLAGS -include cstdint"
cmake -S llvm -B build -G Ninja \
	-DCMAKE_BUILD_TYPE=MinSizeRel \
	-DCMAKE_INSTALL_PREFIX=/usr \
	-DLLVM_TARGETS_TO_BUILD="X86;AMDGPU" \
	-DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
	-DLLVM_BUILD_LLVM_DYLIB=ON \
	-DLLVM_LINK_LLVM_DYLIB=ON \
	-DLLVM_ENABLE_RTTI=ON \
	-DLLVM_ENABLE_EH=ON \
	-DLLVM_INCLUDE_TESTS=OFF \
	-DLLVM_INCLUDE_EXAMPLES=OFF \
	-DLLVM_INCLUDE_BENCHMARKS=OFF \
	-DLLVM_INCLUDE_DOCS=OFF
ninja -C build
mkdir -p /out/llvm/usr/lib /out/clang/usr/bin /out/clang/usr/lib
cp -a build/lib/libLLVM*.so* /out/llvm/usr/lib/
cp -a build/lib/libLTO*.so* /out/llvm/usr/lib/
cp -a build/lib/libRemarks*.so* /out/llvm/usr/lib/
cp -a build/lib/libclang-cpp*.so* /out/clang/usr/lib/
cp -a build/lib/clang /out/clang/usr/lib/
for b in clang clang-15 clang++ clangd clang-format; do
	if [ -e "build/bin/$b" ]; then
		cp -a "build/bin/$b" /out/clang/usr/bin/
	fi
done
cd /out/llvm/usr/lib
real=$(ls -S libLLVM*.so* | head -1)
[ -e libLLVM-15.so ] || ln -sf "$real" libLLVM-15.so
[ -e libLLVM.so ] || ln -sf "$real" libLLVM.so
cd /out/clang/usr/lib/clang
rm -rf analyze-c++ analyze-cc ccc-analyzer intercept-cc
rm -rf */include/arm* */include/opencl* */include/riscv* */include/hexagon* */include/hvx*
rm -rf */include/msa.h */include/htm* */include/vecintrin.h */include/s390intrin.h */include/velintrin*
rm -rf */include/wasm* */include/__clang_cuda* */include/__clang_hip* */include/cuda_wrappers
rm -rf */include/openmp_wrappers */include/ppc_wrappers */include/llvm_libc_wrappers */include/altivec.h

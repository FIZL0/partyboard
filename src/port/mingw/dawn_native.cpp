// N138: the MinGW build links aurora against the prebuilt (MSVC) webgpu_dawn.dll. Its C API is ABI-neutral, but
// aurora's gpu.cpp also constructs dawn::native::DawnInstanceDescriptor, whose out-of-line constructor the DLL exports
// only under its MSVC-mangled name. This is that constructor for GCC's name: Dawn's own (src/dawn/native/DawnNative.cpp)
// only sets sType, and the members' default initializers come from the header. The struct is still not handed to Dawn
// on MinGW (cmake/patches/aurora/0015): GCC and MSVC lay it out differently (tail padding of wgpu::ChainedStruct).
#include <dawn/native/DawnNative.h>

namespace dawn::native {
DawnInstanceDescriptor::DawnInstanceDescriptor() { sType = wgpu::SType::DawnInstanceDescriptor; }
} // namespace dawn::native

# DONE

### Netplay (in order)
- [x] **N1** Rust crate `net/` (`pbnet`, staticlib, C ABI header `include/port/pbnet.h` generated or hand-written), built by CMake through cargo (Corrosion or `add_custom_command`) and linked into `dol`; one exported `pbnet_version()` called at boot and logged. Desktop Linux build must stay green. → net/Cargo.toml · net/src/lib.rs · CMakeLists.txt · include/port/pbnet.h | t: cargo test in net/

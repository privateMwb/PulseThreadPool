from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get
import os


class Conan(ConanFile):
    # ── Retargeting this recipe for a new library ───────────────────
    # Edit these fields (and the class name above) — everything below
    # derives from them. Version is handled by a separate script, not
    # edited here.
    name = "threadpoolpro"
    cmake_name = "ThreadPoolPro"  # matches project()'s name in the top-level CMakeLists.txt
    version = "1.0.0"

    url = "https://github.com/privateMwb/ThreadPoolPro"
    description = "Work-stealing C++ thread pool with lock-free per-worker queues, type-erased SBO task storage, and a lightweight Future replacing std::packaged_task/std::future."
    topics = (
        "threadpool",
        "work-stealing",
        "concurrency",
        "cpp",
        "lock-free",
    )
    
    # ──────────────────────────────────────────────────────────────

    # library, not "header-library": src/ThreadPoolPro/*.cpp now exist
    # and compile to a real static lib (see CMakeLists.txt's auto-detect)
    # — this must track that, or CMakeDeps generates an INTERFACE target
    # with nothing to link, and consumers fail at link time even though
    # headers resolve fine.
    package_type = "library"

    license = "MIT"
    author = "privateMwb"

    settings = "os", "compiler", "build_type", "arch"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }

    default_options = {
        "shared": False,
        "fPIC": True,
    }

    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "include/*",
        "src/*",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    # No package_id() override: this is a compiled static library now,
    # so (unlike header-only) each settings/options combination needs
    # its own package id — the default behavior is correct here.

    def validate(self):
        check_min_cppstd(self, 23)

    def source(self):
        get(
            self,
            **self.conan_data["sources"][self.version],
            strip_root=True,
        )

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure(
            variables={
                "BUILD_TESTS": "OFF",
                "BUILD_BENCHMARKS": "OFF",
                "BUILD_REGRESSION": "OFF",
                "BUILD_EXAMPLES": "OFF",
            }
        )
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

        copy(
            self,
            "LICENSE",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", self.cmake_name)
        self.cpp_info.set_property("cmake_target_name", f"{self.cmake_name}::{self.cmake_name}")
        self.cpp_info.libs = [self.cmake_name]
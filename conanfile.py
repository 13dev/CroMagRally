from conan import ConanFile


class CroMagRallyConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "PkgConfigDeps", "MesonToolchain"

    def requirements(self):
        self.requires("sdl/3.2.14")
        self.requires("spdlog/1.15.3")
        self.requires("gamenetworkingsockets/1.4.1")

    def configure(self):
        # Reduce SDL3 dependencies by disabling optional features
        self.options["sdl/*"].wayland = False
        self.options["sdl/*"].pulseaudio = False
        self.options["sdl/*"].pipewire = False
        self.options["sdl/*"].alsa = True

    def build_requirements(self):
        self.tool_requires("meson/[>=1.0.0]")
        self.tool_requires("pkgconf/[>=1.9.0]")

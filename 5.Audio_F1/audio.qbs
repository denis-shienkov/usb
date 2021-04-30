import qbs

CppApplication {
    name: "audio"

    cpp.cLanguageVersion: "gnu99"
    cpp.positionIndependentCode: false

    cpp.assemblerFlags: [
        "-ahls",
        "-mapcs-32",
    ]

    cpp.commonCompilerFlags: [
        "-Wall",
        "-fno-builtin",
        "-gdwarf-2",
    ]

    cpp.driverLinkerFlags: [
        "-nostartfiles",
    ]

    cpp.staticLibraries: [
        "c",
        "gcc",
        "m",
        "nosys",
    ]

    cpp.defines: [
        "GCC_ARMCM3",
        "STM32F103xB",
        "VECT_TAB_FLASH",
    ]

    cpp.driverFlags: [
        "-mcpu=cortex-m3",
        "-mthumb",
    ]

    cpp.includePaths: [
        "lib/include"
    ]

    cpp.libraryPaths: [
        "lib/linker_scripts"
    ]

    Group {
        name: "sources"
        prefix: "src/"
        files: [ "*.h", "*.c" ]
    }

    Group {
        name: "includes"
        prefix: "lib/include/"
        files: [ "*.h" ]
    }

    Group {
        name: "Startup"
        files: [ "lib/startup_code/startup_stm32f10x_md.s" ]
    }

    Group {
        name: "Linker scripts"
        fileTags: ["linkerscript"]
        files: [ "lib/linker_scripts/stm32f10xC8.ld" ]
    }
}

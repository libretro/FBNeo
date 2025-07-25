DEBUG = 0
DEBUG_ASAN = 0
DEBUG_UBSAN = 0
HAVE_GRIFFIN = 0
# fastcall only works on x86_32
FASTCALL = 0
# fastmath should improve performance with every arch, and there is no known issue with fbneo
FASTMATH = 1
USE_SPEEDHACKS = 1
EXTERNAL_ZLIB = 0
INCLUDE_7Z_SUPPORT = 1
# enable x86_64 dynarec used in killer instinct
USE_X64_DRC = 0
AUTOGEN_DATS = 0
HAVE_NEON = 0
USE_EXPERIMENTAL_FLAGS = 0
USE_CYCLONE = 0
HAVE_UWP = 0
# Use a temporary file to hold the list of objects, as it can exceed windows shell command limits
SPLIT_UP_LINK ?= 0
REGEN_HEADERS ?= 0

SPACE :=
SPACE := $(SPACE) $(SPACE)
BACKSLASH :=
BACKSLASH := \$(BACKSLASH)
filter_out1 = $(filter-out $(firstword $1),$1)
filter_out2 = $(call filter_out1,$(call filter_out1,$1))

# system platform
ifeq ($(platform),)
	platform = unix
	ifeq ($(shell uname -a),)
		platform = win
		EXE_EXT=.exe
	else ifneq ($(findstring Darwin,$(shell uname -a)),)
		platform = osx
		arch = intel
		ifeq ($(shell uname -p),arm)
			arch = arm
		endif
		ifeq ($(shell uname -p),powerpc)
			arch = ppc
		endif
	else ifneq ($(findstring MINGW,$(shell uname -a)),)
		platform = win
		EXE_EXT=.exe
	endif
else ifneq (,$(findstring armv,$(platform)))
	ifeq (,$(findstring classic_,$(platform)))
		override platform += unix
	endif
else ifneq (,$(findstring rpi,$(platform)))
	override platform += unix
endif

MAIN_FBNEO_DIR	:= ../..
VERSION_SCRIPT	:= $(MAIN_FBNEO_DIR)/burner/libretro/link.T

# TARGET
ifeq ($(SUBSET),)
	TARGET_NAME := fbneo
	INCLUDED_MAKEFILE = Makefile.all
else
	TARGET_NAME := fbneo_$(SUBSET)
	INCLUDED_MAKEFILE = Makefile.$(SUBSET)
endif

# Unix
ifneq (,$(findstring unix,$(platform)))
	TARGET := $(TARGET_NAME)_libretro.so
	fpic := -fPIC
	SHARED := -shared -Wl,-no-undefined -Wl,--version-script=$(VERSION_SCRIPT)
	ENDIANNESS_DEFINES := -DLSB_FIRST
	LDFLAGS += -lpthread

	# Raspberry Pi
	ifneq (,$(findstring rpi1,$(platform)))
		PLATFORM_DEFINES := -marm -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard
		HAVE_NEON = 0
		USE_CYCLONE = 1
	else ifneq (,$(findstring rpi2,$(platform)))
		PLATFORM_DEFINES := -marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
		HAVE_NEON = 1
		USE_CYCLONE = 1
	else ifneq (,$(findstring rpi3,$(platform)))
		ifneq (,$(findstring rpi3_64,$(platform)))
			PLATFORM_DEFINES := -march=armv8-a+crc+simd -mtune=cortex-a53
		else
			PLATFORM_DEFINES := -marm -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
			HAVE_NEON = 1
			USE_CYCLONE = 1
		endif
	else ifneq (,$(findstring rpi4_64,$(platform)))
		PLATFORM_DEFINES := -march=armv8-a+crc+simd -mtune=cortex-a72
	else ifneq (,$(findstring rpi5_64,$(platform)))
		PLATFORM_DEFINES := -march=armv8.2-a+crc+simd -mtune=cortex-a76
	endif

	# Solaris
	ifneq (,$(findstring solaris,$(platform)))
		CC := gcc
		SHARED := -shared
	endif

	# Generic ARM
	ifneq (,$(findstring armv,$(platform)))
		PLATFORM_DEFINES :=
		ifneq (,$(findstring android,$(platform)))
			CC = arm-linux-androideabi-gcc
			CXX = arm-linux-androideabi-g++
			PLATFORM_DEFINES += -DANDROID -Dlog2\(x\)=\(log\(x\)/1.4426950408889634\)
		endif
		USE_CYCLONE = 1
		ifneq (,$(findstring neon,$(platform)))
			HAVE_NEON = 1
		endif
	endif

# OS X
else ifeq ($(platform), osx)
	TARGET := $(TARGET_NAME)_libretro.dylib
	fpic := -fPIC
	SHARED := -dynamiclib
	ifeq ($(shell uname -p),powerpc)
		ENDIANNESS_DEFINES =  -DWORDS_BIGENDIAN
	else
		ENDIANNESS_DEFINES := -DLSB_FIRST
	endif
	OSXVER = `sw_vers -productVersion | cut -d. -f 2`
	OSX_LT_MAVERICKS = `(( $(OSXVER) <= 9)) && echo "YES"`
	ifeq ($(OSX_LT_MAVERICKS),"YES")
		fpic += -mmacosx-version-min=10.5
	endif
   ifeq ($(CROSS_COMPILE),1)
		TARGET_RULE   = -target $(LIBRETRO_APPLE_PLATFORM) -isysroot $(LIBRETRO_APPLE_ISYSROOT)
		CFLAGS   += $(TARGET_RULE)
		CPPFLAGS += $(TARGET_RULE)
		CXXFLAGS += $(TARGET_RULE)
		LDFLAGS  += $(TARGET_RULE)
   endif
	ifeq ($(UNIVERSAL),1)
		CFLAGS += $(ARCHFLAGS)
		CXXFLAGS += $(ARCHFLAGS)
		LDFLAGS += $(ARCHFLAGS)
	endif

# iOS
else ifneq (,$(findstring ios,$(platform)))
	TARGET := $(TARGET_NAME)_libretro_ios.dylib
	fpic := -fPIC
	SHARED := -dynamiclib
	HAVE_NEON = 1
	#USE_CYCLONE = 1
	ifeq ($(IOSSDK),)
		IOSSDK := $(shell xcodebuild -version -sdk iphoneos Path)
	endif
	ifeq ($(platform),ios-arm64)
		CC = cc -arch arm64 -isysroot $(IOSSDK)
		CXX = c++ -arch arm64 -isysroot $(IOSSDK)
	else
		CC = cc -arch armv7 -isysroot $(IOSSDK)
		CXX = c++ -arch armv7 -isysroot $(IOSSDK)
	endif
	ENDIANNESS_DEFINES := -DLSB_FIRST
        MINVERSION :=
	ifeq ($(platform),$(filter $(platform),ios9 ios-arm64))
		CFLAGS += -DIOS9
		CXXFLAGS += -DIOS9
                MINVERSION = -miphoneos-version-min=8.0
	else
		CFLAGS += -DIOS
		CXXFLAGS += -DIOS
                MINVERSION = -miphoneos-version-min=5.0
	endif
        CFLAGS += $(MINVERSION)
        CXXFLAGS += $(MINVERSION)

else ifeq ($(platform), tvos-arm64)
	TARGET := $(TARGET_NAME)_libretro_tvos.dylib
	fpic := -fPIC
	SHARED := -dynamiclib
	ifeq ($(IOSSDK),)
		IOSSDK := $(shell xcodebuild -version -sdk appletvos Path)
	endif
	ENDIANNESS_DEFINES := -DLSB_FIRST
	CFLAGS += -DIOS9
	CXXFLAGS += -DIOS9
	CC = cc -arch arm64 -isysroot $(IOSSDK)
	CXX = c++ -arch arm64 -isysroot $(IOSSDK)

# Theos iOS
else ifeq ($(platform), theos_ios)
	DEPLOYMENT_IOSVERSION = 5.0
	TARGET = iphone:latest:$(DEPLOYMENT_IOSVERSION)
	ARCHS = armv7 armv7s
	TARGET_IPHONEOS_DEPLOYMENT_VERSION=$(DEPLOYMENT_IOSVERSION)
	THEOS_BUILD_DIR := objs
	include $(THEOS)/makefiles/common.mk
	LIBRARY_NAME = $(TARGET_NAME)_libretro_ios
	ENDIANNESS_DEFINES := -DLSB_FIRST
	CFLAGS += -DIOS
	USE_CYCLONE = 1

# QNX
else ifeq ($(platform), qnx)
	TARGET := $(TARGET_NAME)_libretro_$(platform).so
	fpic := -fPIC
	SHARED := -lcpp -lm -shared -Wl,-no-undefined -Wl,--version-script=$(VERSION_SCRIPT)
	ENDIANNESS_DEFINES := -DLSB_FIRST
	CC = qcc -Vgcc_ntoarmv7le
	CXX = QCC -Vgcc_ntoarmv7le_cpp
	AR = qcc -Vgcc_ntoarmv7le
	PLATFORM_DEFINES := -D__BLACKBERRY_QNX__ -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp
	HAVE_NEON = 1
	USE_CYCLONE = 1

# Lightweight PS3 Homebrew SDK
else ifneq (,$(filter $(platform), ps3 psl1ght))
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	PLATFORM_DEFINES += -D__PS3__
	ifeq ($(platform), psl1ght)
       PLATFORM_DEFINES += -D__PSL1GHT__
	endif
	ENDIANNESS_DEFINES =  -DWORDS_BIGENDIAN
	EXTERNAL_ZLIB = 1
	STATIC_LINKING = 1
	INCLUDE_7Z_SUPPORT = 0

	CC = $(PS3DEV)/ppu/bin/ppu-$(COMMONLV)gcc$(EXE_EXT)
	CXX = $(PS3DEV)/ppu/bin/ppu-$(COMMONLV)g++$(EXE_EXT)
	AR = $(PS3DEV)/ppu/bin/ppu-$(COMMONLV)ar$(EXE_EXT)

# Vita
else ifeq ($(platform), vita)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = arm-vita-eabi-gcc$(EXE_EXT)
	CC_AS = arm-vita-eabi-gcc$(EXE_EXT)
	CXX = arm-vita-eabi-g++$(EXE_EXT)
	AR = arm-vita-eabi-ar$(EXE_EXT)
	PLATFORM_DEFINES += -DVITA
	ENDIANNESS_DEFINES := -DLSB_FIRST
	CFLAGS += -mfloat-abi=hard -fsingle-precision-constant
	CXXFLAGS += -mfloat-abi=hard -fsingle-precision-constant -fpermissive -fno-rtti
	EXTERNAL_ZLIB = 1
	STATIC_LINKING = 1
	USE_CYCLONE = 1

# CTR (3DS)
else ifeq ($(platform), ctr)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = $(DEVKITARM)/bin/arm-none-eabi-gcc$(EXE_EXT)
	CXX = $(DEVKITARM)/bin/arm-none-eabi-g++$(EXE_EXT)
	AR = $(DEVKITARM)/bin/arm-none-eabi-ar$(EXE_EXT)
	PLATFORM_DEFINES += -D_3DS
	ENDIANNESS_DEFINES := -DLSB_FIRST
	CFLAGS += -march=armv6k -mtune=mpcore -mfloat-abi=hard
	CXXFLAGS += -march=armv6k -mtune=mpcore -mfloat-abi=hard -fpermissive -fno-rtti
	EXTERNAL_ZLIB = 1
	STATIC_LINKING = 1
	USE_CYCLONE = 1

# Xbox 360
else ifeq ($(platform), xenon)
	TARGET := $(TARGET_NAME)_libretro_xenon360.a
	CC = xenon-gcc$(EXE_EXT)
	CXX = xenon-g++$(EXE_EXT)
	AR = xenon-ar$(EXE_EXT)
	ENDIANNESS_DEFINES = -DWORDS_BIGENDIAN
	PLATFORM_DEFINES := -D__LIBXENON__ -m32 -D__ppc__
	STATIC_LINKING = 1

# Nintendo Switch (libtransistor)
else ifeq ($(platform), switch)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	include $(LIBTRANSISTOR_HOME)/libtransistor.mk
	STATIC_LINKING=1
	ENDIANNESS_DEFINES := -DLSB_FIRST
	PLATFORM_DEFINES := -marm -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
	HAVE_NEON = 1
	USE_CYCLONE = 1

# Nintendo Switch (libnx)
else ifeq ($(platform), libnx)
	include $(DEVKITPRO)/libnx/switch_rules
	AR="$(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-ar$(BINARY_EXT)"
	EXT=a
	TARGET := $(TARGET_NAME)_libretro_$(platform).$(EXT)
	DEFINES := -DSWITCH=1 -U__linux__ -U__linux -DRARCH_INTERNAL
	CFLAGS	:=	$(DEFINES) -g \
		-fPIE -I$(LIBNX)/include/ -ffunction-sections -fdata-sections -ftls-model=local-exec -Wl,--allow-multiple-definition -specs=$(LIBNX)/switch.specs
	CFLAGS += $(INCDIRS)
	CFLAGS	+=	$(INCLUDE)  -D__SWITCH__ -DHAVE_LIBNX
	CXXFLAGS := $(ASFLAGS) $(CFLAGS) -fno-rtti -std=gnu++11
	CFLAGS += -std=gnu11
	STATIC_LINKING = 1
	# 2021-05-18 : disabling 7z again for libnx, it seems the lib version linked to RA is old
	INCLUDE_7Z_SUPPORT = 0
	ENDIANNESS_DEFINES := -DLSB_FIRST

# Funkey S
else ifeq ($(platform), funkey)
	TARGET := $(TARGET_NAME)_libretro.so
	fpic := -fPIC
	SHARED := -shared -Wl,-no-undefined -Wl,--version-script=$(VERSION_SCRIPT)
	PLATFORM_FLAGS = -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
	USE_EXPERIMENTAL_FLAGS = 1
	HAVE_NEON = 1
	USE_CYCLONE = 1
	ARCH = arm
	ENDIANNESS_DEFINES := -DLSB_FIRST
	ifeq ($(shell echo `$(CC) -dumpversion` "< 4.9" | bc -l), 1)
		PLATFORM_FLAGS += -march=armv7-a
	else
		PLATFORM_FLAGS += -march=armv7ve
		# If gcc is 5.0 or later
		ifeq ($(shell echo `$(CC) -dumpversion` ">= 5" | bc -l), 1)
			LDFLAGS += -static-libgcc -static-libstdc++
		endif
	endif
	CFLAGS += $(PLATFORM_FLAGS)
	CXXFLAGS += $(PLATFORM_FLAGS)

# Classic Platforms ####################
# Platform affix = classic_<ISA>_<µARCH>
# Help at https://modmyclassic.com/comp

# (armv7 a7, hard point, neon based) ### 
# NESC, SNESC, C64 mini 
else ifeq ($(platform), classic_armv7_a7)
	TARGET := $(TARGET_NAME)_libretro.so
	fpic := -fPIC
	SHARED := -shared -Wl,-no-undefined -Wl,--version-script=$(VERSION_SCRIPT)
	PLATFORM_FLAGS = -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
	USE_EXPERIMENTAL_FLAGS = 1
	HAVE_NEON = 1
	USE_CYCLONE = 1
	ARCH = arm
	ENDIANNESS_DEFINES := -DLSB_FIRST
	ifeq ($(shell echo `$(CC) -dumpversion` "< 4.9" | bc -l), 1)
		PLATFORM_FLAGS += -march=armv7-a
	else
		PLATFORM_FLAGS += -march=armv7ve
		# If gcc is 5.0 or later
		ifeq ($(shell echo `$(CC) -dumpversion` ">= 5" | bc -l), 1)
			LDFLAGS += -static-libgcc -static-libstdc++
		endif
	endif
	CFLAGS += $(PLATFORM_FLAGS)
	CXXFLAGS += $(PLATFORM_FLAGS)

# (armv8 a35, hard point, neon based) ###
# Playstation Classic
else ifeq ($(platform), classic_armv8_a35)
	TARGET := $(TARGET_NAME)_libretro.so
	fpic := -fPIC
	SHARED := -shared -Wl,-no-undefined -Wl,--version-script=$(VERSION_SCRIPT)
	PLATFORM_FLAGS = -marm -mtune=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard -march=armv8-a
	USE_EXPERIMENTAL_FLAGS = 1
	HAVE_NEON = 1
	USE_CYCLONE = 1
	ARCH = arm
	ENDIANNESS_DEFINES := -DLSB_FIRST
	LDFLAGS += -static-libgcc -static-libstdc++
	CFLAGS += $(PLATFORM_FLAGS)
	CXXFLAGS += $(PLATFORM_FLAGS)
#######################################

# Nintendo Game Cube / Wii / WiiU
else ifneq (,$(filter $(platform), ngc wii wiiu))
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	PATH := $(PATH):$(DEVKITPPC)/bin
	CC = powerpc-eabi-gcc$(EXE_EXT)
	CXX = powerpc-eabi-g++$(EXE_EXT)
	AR = powerpc-eabi-ar$(EXE_EXT)
	ENDIANNESS_DEFINES =  -DWORDS_BIGENDIAN
	PLATFORM_DEFINES := -ffunction-sections -fdata-sections -D__wiiu__ -D__wut__ -UPPC
	EXTERNAL_ZLIB = 1
	STATIC_LINKING = 1

	# Nintendo WiiU
	ifneq (,$(findstring wiiu,$(platform)))
		PLATFORM_DEFINES += -DGEKKO -DWIIU -DHW_RVL -mcpu=750 -meabi -mhard-float

	# Nintendo Wii
	else ifneq (,$(findstring wii,$(platform)))
		PLATFORM_DEFINES += -DGEKKO -DHW_RVL -mrvl -mcpu=750 -meabi -mhard-float

	# Nintendo Game Cube
	else ifneq (,$(findstring ngc,$(platform)))
		PLATFORM_DEFINES += -DGEKKO -DHW_DOL -mrvl -mcpu=750 -meabi -mhard-float
	endif

# Emscripten
else ifeq ($(platform), emscripten)
	TARGET := $(TARGET_NAME)_libretro_$(platform).bc
	ENDIANNESS_DEFINES := -DLSB_FIRST
	INCLUDE_7Z_SUPPORT = 0
	EXTERNAL_ZLIB = 1
	STATIC_LINKING = 1

# Windows MSVC 2017 all architectures
else ifneq (,$(findstring windows_msvc2017,$(platform)))

	ENDIANNESS_DEFINES := -DLSB_FIRST

	PlatformSuffix = $(subst windows_msvc2017_,,$(platform))
	ifneq (,$(findstring desktop,$(PlatformSuffix)))
		WinPartition = desktop
		MSVC2017CompileFlags = -DWINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP -EHsc -FS
		LDFLAGS += -MANIFEST -LTCG:incremental -NXCOMPAT -DYNAMICBASE -DEBUG -OPT:REF -INCREMENTAL:NO -SUBSYSTEM:WINDOWS -MANIFESTUAC:"level='asInvoker' uiAccess='false'" -OPT:ICF -ERRORREPORT:PROMPT -NOLOGO -TLBID:1
		LIBS := kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib
	else ifneq (,$(findstring uwp,$(PlatformSuffix)))
		WinPartition = uwp
		MSVC2017CompileFlags = -DWINAPI_FAMILY=WINAPI_FAMILY_APP -D_WINDLL -D_UNICODE -DUNICODE -D__WRL_NO_DEFAULT_LIB__ -EHsc -FS
		LDFLAGS += -APPCONTAINER -NXCOMPAT -DYNAMICBASE -MANIFEST:NO -LTCG -OPT:REF -SUBSYSTEM:CONSOLE -MANIFESTUAC:NO -OPT:ICF -ERRORREPORT:PROMPT -NOLOGO -TLBID:1 -DEBUG:FULL -WINMD:NO
		LIBS := WindowsApp.lib
		HAVE_UWP = 1
	endif

	CFLAGS += $(MSVC2017CompileFlags)
	CXXFLAGS += $(MSVC2017CompileFlags)

	TargetArchMoniker = $(subst $(WinPartition)_,,$(PlatformSuffix))

	CC  = cl.exe
	CXX = cl.exe
	LD = link.exe

	reg_query = $(call filter_out2,$(subst $2,,$(shell reg query "$2" -v "$1" 2>nul)))
	fix_path = $(subst $(SPACE),\ ,$(subst \,/,$1))

	ProgramFiles86w := $(shell cmd //c "echo %PROGRAMFILES(x86)%")
	ProgramFiles86 := $(shell cygpath "$(ProgramFiles86w)")

	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir := $(WindowsSdkDir)

	WindowsSDKVersion ?= $(firstword $(foreach folder,$(subst $(subst \,/,$(WindowsSdkDir)Include/),,$(wildcard $(call fix_path,$(WindowsSdkDir)Include\*))),$(if $(wildcard $(call fix_path,$(WindowsSdkDir)Include/$(folder)/um/Windows.h)),$(folder),)))$(BACKSLASH)
	WindowsSDKVersion := $(WindowsSDKVersion)

	VsInstallBuildTools = $(ProgramFiles86)/Microsoft Visual Studio/2017/BuildTools
	VsInstallEnterprise = $(ProgramFiles86)/Microsoft Visual Studio/2017/Enterprise
	VsInstallProfessional = $(ProgramFiles86)/Microsoft Visual Studio/2017/Professional
	VsInstallCommunity = $(ProgramFiles86)/Microsoft Visual Studio/2017/Community

	VsInstallRoot ?= $(shell if [ -d "$(VsInstallBuildTools)" ]; then echo "$(VsInstallBuildTools)"; fi)
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallEnterprise)" ]; then echo "$(VsInstallEnterprise)"; fi)
	endif
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallProfessional)" ]; then echo "$(VsInstallProfessional)"; fi)
	endif
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallCommunity)" ]; then echo "$(VsInstallCommunity)"; fi)
	endif
	VsInstallRoot := $(VsInstallRoot)

	VcCompilerToolsVer := $(shell cat "$(VsInstallRoot)/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" | grep -o '[0-9\.]*')
	VcCompilerToolsDir := $(VsInstallRoot)/VC/Tools/MSVC/$(VcCompilerToolsVer)

	WindowsSDKSharedIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\shared")
	WindowsSDKUCRTIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\ucrt")
	WindowsSDKUMIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\um")
	WindowsSDKUCRTLibDir := $(shell cygpath -w "$(WindowsSdkDir)\Lib\$(WindowsSDKVersion)\ucrt\$(TargetArchMoniker)")
	WindowsSDKUMLibDir := $(shell cygpath -w "$(WindowsSdkDir)\Lib\$(WindowsSDKVersion)\um\$(TargetArchMoniker)")

	# For some reason the HostX86 compiler doesn't like compiling for x64
	# ("no such file" opening a shared library), and vice-versa.
	# Work around it for now by using the strictly x86 compiler for x86, and x64 for x64.
	# NOTE: What about ARM?
	ifneq (,$(findstring x64,$(TargetArchMoniker)))
		VCCompilerToolsBinDir := $(VcCompilerToolsDir)\bin\HostX64
	else
		VCCompilerToolsBinDir := $(VcCompilerToolsDir)\bin\HostX86
	endif

	PATH := $(shell IFS=$$'\n'; cygpath "$(VCCompilerToolsBinDir)/$(TargetArchMoniker)"):$(PATH)
	PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VsInstallRoot)/Common7/IDE")
	INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VcCompilerToolsDir)/include")
	LIB := $(shell IFS=$$'\n'; cygpath -w "$(VcCompilerToolsDir)/lib/$(TargetArchMoniker)")
	ifneq (,$(findstring uwp,$(PlatformSuffix)))
		LIB := $(shell IFS=$$'\n'; cygpath -w "$(LIB)/store")
	endif

	export INCLUDE := $(INCLUDE);$(WindowsSDKSharedIncludeDir);$(WindowsSDKUCRTIncludeDir);$(WindowsSDKUMIncludeDir)
	export LIB := $(LIB);$(WindowsSDKUCRTLibDir);$(WindowsSDKUMLibDir)
	TARGET := $(TARGET_NAME)_libretro.dll
	PSS_STYLE :=2
	LDFLAGS += -DLL

# Windows
else
	TARGET := $(TARGET_NAME)_libretro.dll
	CC ?= gcc
	CXX ?= g++
	SHARED := -shared -Wl,-no-undefined -Wl,--version-script=$(VERSION_SCRIPT)
	LDFLAGS += -static-libgcc -static-libstdc++
	ENDIANNESS_DEFINES := -DLSB_FIRST
	SPLIT_UP_LINK = 1
endif

# some platforms (3ds, maybe others) are unable to find "main" entry symbol if you build with their sdk
# so let's use standard compiler for header generation since this is cross-built anyway
CC_SYSTEM = gcc
CXX_SYSTEM = g++

include Makefile.common
include $(INCLUDED_MAKEFILE)

FBNEO_CXXOBJ := $(SOURCES_CXX:.cpp=.o)
FBNEO_COBJ := $(SOURCES_C:.c=.o)
FBNEO_SOBJ := $(SOURCES_S:.S=.o)

OBJS := $(FBNEO_COBJ) $(FBNEO_CXXOBJ) $(FBNEO_SOBJ)
DEPS := $(OBJS:.o=.d)

FBNEO_DEFINES += -D__LIBRETRO__ -D_FILE_OFFSET_BITS=64 \
	$(ENDIANNESS_DEFINES) \
	$(PLATFORM_DEFINES) \
	$(NEON_FLAGS) \
	$(ARM_FLAGS)

INCFLAGS := $(foreach dir,$(INCLUDE_DIRS),-I$(dir))

ifeq ($(USE_SPEEDHACKS), 1)
	FBNEO_DEFINES += -DUSE_SPEEDHACKS
endif

ifeq ($(FASTCALL), 1)
	FBNEO_DEFINES += -DFASTCALL
endif

ifeq ($(FASTMATH), 1)
	ifeq (,$(findstring msvc,$(platform)))
		# 2019-06-26 : 
		# we got some evidence that -ffast-math is actually not safe for fbneo
		# adding -frounding-math to fix arithmetic in neogeo_run.cpp, it might fix other stuff
		# clang might need a minimum version for -frounding-math support
		CFLAGS += -ffast-math -frounding-math
		CXXFLAGS += -ffast-math -frounding-math
	endif
endif

ifeq ($(USE_EXPERIMENTAL_FLAGS), 1)
	EXPERIMENTAL_FLAGS = -fwhole-program -fuse-linker-plugin \
		-fdata-sections -ffunction-sections -Wl,--gc-sections \
		-fno-stack-protector -fno-ident \
		-falign-functions=1 -falign-jumps=1 -falign-loops=1 \
		-fno-unwind-tables -fno-asynchronous-unwind-tables -fno-unroll-loops \
		-fmerge-all-constants -fno-math-errno
	ifeq ($(shell $(CC) -v 2>&1 | grep -c "gcc version"), 1)
		EXPERIMENTAL_FLAGS += -flto=4
	endif
	CFLAGS += $(EXPERIMENTAL_FLAGS)
	CXXFLAGS += $(EXPERIMENTAL_FLAGS)
endif

ifeq ($(DEBUG_ASAN), 1)
	DEBUG = 1
	DEBUG_UBSAN = 0
	CFLAGS += -fsanitize=address
	CXXFLAGS += -fsanitize=address
	LDFLAGS += -lasan -fsanitize=address
endif

ifeq ($(DEBUG_UBSAN), 1)
	DEBUG = 1
	CFLAGS += -fsanitize=undefined
	CXXFLAGS += -fsanitize=undefined
	LDFLAGS += -lubsan -fsanitize=undefined
endif

ifeq ($(DEBUG), 1)
	CFLAGS += -O0 -g -DFBNEO_DEBUG
	CXXFLAGS += -O0 -g -DFBNEO_DEBUG
else
	ifeq (,$(findstring msvc,$(platform)))
		# sadly, we can't use -Os for memory constrained systems here, the performance impact is too big
		CFLAGS += -O3 -DNDEBUG -fomit-frame-pointer
		CXXFLAGS += -O3 -DNDEBUG -fomit-frame-pointer
	else
		CFLAGS += -O2 -DNDEBUG
		CXXFLAGS += -O2 -DNDEBUG
	endif
endif

CFLAGS += $(fpic) $(FBNEO_DEFINES) -MMD
CXXFLAGS += $(fpic) $(FBNEO_DEFINES) -MMD
LDFLAGS += $(fpic)

ifeq (,$(findstring msvc,$(platform)))
	CFLAGS += -fforce-addr  \
		-Wall -Wno-long-long -Wno-sign-compare -Wno-uninitialized -Wno-unused \
		-Wno-sequence-point -Wno-strict-aliasing
	CXXFLAGS += -fforce-addr \
		-Wall -W -Wshadow -Wno-long-long -Wno-write-strings \
		-Wunknown-pragmas -Wundef -Wno-conversion -Wno-missing-braces -Wno-multichar \
		-Wuninitialized -Wpointer-arith -Wno-inline -Wno-unused-value \
		-Wno-sequence-point -Wno-extra -Wno-strict-aliasing

	ifeq (,$(filter $(platform), ps3 sncps3))
		CFLAGS += -Wno-write-strings -Wno-pedantic
		CXXFLAGS += -Wno-write-strings -pedantic -Wno-address -Wno-narrowing -Wno-pedantic
	endif
endif

# gcc specific flags
ifeq ($(shell $(CC) -v 2>&1 | grep -c "gcc version"), 1)
	CFLAGS += -ffloat-store
	CXXFLAGS += -ffloat-store
	ifeq ($(shell expr `$(CC) -dumpversion | cut -f1 -d.` \>= 5), 1)
		CFLAGS += -finline-limit=1200 -fcheck-new
		CXXFLAGS += -finline-limit=1200 -fcheck-new
	endif
endif

PERL = perl$(EXE_EXT)
M68KMAKE_EXE = m68kmake$(EXE_EXT)
CTVMAKE_EXE = ctvmake$(EXE_EXT)
PGM_SPRITE_CREATE_EXE = pgmspritecreate$(EXE_EXT)
EXE_PREFIX = ./

# backward compatibility (make less than 4.4 doesn't support .WAIT)
.WAIT:

.PHONY: all clean generate-files generate-files-clean clean-objs

ifeq ($(REGEN_HEADERS), 1)
all: generate-files .WAIT $(TARGET)
else
all: $(TARGET)
endif

generate-files-clean:
	rm -rf $(FBNEO_GENERATED_DIR)/
	rm -rf $(FBNEO_CPU_DIR)/m68k/m68kops.c
	rm -rf $(FBNEO_CPU_DIR)/m68k/m68kops.h
	rm -rf $(MAIN_FBNEO_DIR)/../gamelist.txt

generate-files:
	@mkdir -p $(FBNEO_GENERATED_DIR) 2>/dev/null || /bin/true
	@echo "Generating $(FBNEO_GENERATED_DIR)/driverlist.h..."
	@echo ""
	$(PERL) $(FBNEO_SCRIPTS_DIR)/gamelist.pl -o $(FBNEO_GENERATED_DIR)/driverlist.h -l $(MAIN_FBNEO_DIR)/../gamelist.txt $(SOURCES_CXX)
	@echo ""
	@echo "Generating $(FBNEO_GENERATED_DIR)/neo_sprite_func.h..."
	@echo ""
	@echo "Generating $(FBNEO_GENERATED_DIR)/neo_sprite_func_table.h..."
	@echo ""
	$(PERL) $(FBNEO_SCRIPTS_DIR)/neo_sprite_func.pl -o $(FBNEO_GENERATED_DIR)/neo_sprite_func.h
	@echo ""
	@echo "Generating $(FBNEO_GENERATED_DIR)/psikyo_tile_func.h..."
	@echo ""
	@echo "Generating $(FBNEO_GENERATED_DIR)/psikyo_tile_func_table.h..."
	@echo ""
	$(PERL) $(FBNEO_SCRIPTS_DIR)/psikyo_tile_func.pl -o $(FBNEO_GENERATED_DIR)/psikyo_tile_func.h
	@echo "Generating $(FBNEO_GENERATED_DIR)/cave_sprite_func.h..."
	@echo ""
	@echo "Generating[ $(FBNEO_GENERATED_DIR)/cave_tile_func_table.h"
	@echo ""
	$(PERL) $(FBNEO_SCRIPTS_DIR)/cave_sprite_func.pl -o $(FBNEO_GENERATED_DIR)/cave_sprite_func.h
	$(PERL) $(FBNEO_SCRIPTS_DIR)/cave_tile_func.pl -o $(FBNEO_GENERATED_DIR)/cave_tile_func.h
	@echo ""
	@echo "Generate $(FBNEO_GENERATED_DIR)/toa_gp9001_func_table.h"
	@echo ""
	$(PERL) $(FBNEO_SCRIPTS_DIR)/toa_gp9001_func.pl -o $(FBNEO_GENERATED_DIR)/toa_gp9001_func.h
	$(CXX_SYSTEM) $(GENERATE_OPTS) -o $(PGM_SPRITE_CREATE_EXE) $(FBNEO_BURN_DRIVERS_DIR)/pgm/pgm_sprite_create.cpp
	@echo ""
	@echo "Generating $(FBNEO_GENERATED_DIR)/pgm_sprite.h..."
	@echo ""
	$(EXE_PREFIX)$(PGM_SPRITE_CREATE_EXE) > $(FBNEO_GENERATED_DIR)/pgm_sprite.h
	$(CC_SYSTEM) $(GENERATE_OPTS) -o $(M68KMAKE_EXE) $(FBNEO_CPU_DIR)/m68k/m68kmake.c
	$(EXE_PREFIX)$(M68KMAKE_EXE) $(FBNEO_CPU_DIR)/m68k/ $(FBNEO_CPU_DIR)/m68k/m68k_in.c
	$(CXX_SYSTEM) $(GENERATE_OPTS) -o $(CTVMAKE_EXE) $(FBNEO_BURN_DRIVERS_DIR)/capcom/ctv_make.cpp
	@echo ""
	@echo "Generating $(FBNEO_GENERATED_DIR)/ctv.h..."
	@echo ""
	$(EXE_PREFIX)$(CTVMAKE_EXE) > $(FBNEO_GENERATED_DIR)/ctv.h

OBJOUT   = -o
LINKOUT  = -o

ifneq (,$(findstring msvc,$(platform)))
	OBJOUT = -Fo
	LINKOUT = -out:
ifeq ($(STATIC_LINKING),1)
	LD ?= lib.exe
	STATIC_LINKING=0

	ifeq ($(DEBUG), 1)
		CFLAGS += -MTd
		CXXFLAGS += -MTd
	else
		CFLAGS += -MT
		CXXFLAGS += -MT
	endif
else
	LD = link.exe

	ifeq ($(DEBUG), 1)
		CFLAGS += -MDd
		CXXFLAGS += -MDd
	else
		CFLAGS += -MD
		CXXFLAGS += -MD
	endif
endif
else
	LD = $(CXX)
endif

%.o: %.c
	$(CC) -c $(OBJOUT)$@ $< $(CFLAGS) $(INCFLAGS)

# Disabling file_stream_transform for those files
# In the following file, it's lacking a stdout implementation
$(MAIN_FBNEO_DIR)/cpu/mips3/x64/mips3_x64.o: $(MAIN_FBNEO_DIR)/cpu/mips3/x64/mips3_x64.cpp
	$(CXX) -c $(OBJOUT)$@ $< $(CXXFLAGS) $(INCFLAGS) -DSKIP_STDIO_REDEFINES

$(MAIN_FBNEO_DIR)/cpu/mips3_intf.o: $(MAIN_FBNEO_DIR)/cpu/mips3_intf.cpp
	$(CXX) -c $(OBJOUT)$@ $< $(CXXFLAGS) $(INCFLAGS) -DSKIP_STDIO_REDEFINES

%.o: %.cpp
	$(CXX) -c $(OBJOUT)$@ $< $(CXXFLAGS) $(INCFLAGS)

%.o: %.S
	$(CC) $(CFLAGS) $(INCFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
ifeq ($(STATIC_LINKING), 1)
ifeq ($(SPLIT_UP_LINK), 1)
	$(AR) rcs $@ $(foreach OBJS,$(OBJS),$(NEWLINE) $(AR) q $@ $(OBJS))
else
	$(AR) rcs $@ $(OBJS)
endif
else
ifeq ($(SPLIT_UP_LINK), 1)
	$(file >$@.in,$(OBJS))
	$(LD) $(LINKOUT)$@ $(SHARED) @$@.in $(LDFLAGS) $(LIBS)
	@rm $@.in
else
	$(LD) $(LINKOUT)$@ $(SHARED) $^ $(LDFLAGS) $(LIBS)
endif
endif

-include $(DEPS)

clean-objs:
ifeq ($(SPLIT_UP_LINK), 1)
	$(file >$@.in,$(OBJS) $(DEPS))
	xargs rm -f <$@.in
	rm -f $@.in
else
	rm -f $(OBJS) $(DEPS)
endif

clean: clean-objs
	rm -f $(TARGET)
	rm -f $(M68KMAKE_EXE)
	rm -f $(PGM_SPRITE_CREATE_EXE)
	rm -f $(CTVMAKE_EXE)

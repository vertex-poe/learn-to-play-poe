# Sourced by bash --init-file. Finds VS via vswhere, loads MSVC x64 env into bash.

# Load normal interactive config first so aliases/prompt are set up.
[ -f ~/.bashrc ] && source ~/.bashrc

# Call vswhere.exe directly — Git Bash can run .exe files without going through cmd,
# which avoids quoting/path-conversion issues entirely.
_vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"

if [ ! -f "$_vswhere" ]; then
    echo "WARNING: vswhere.exe not found — is Visual Studio 2022 installed?" >&2
    unset _vswhere
    return 0
fi

# -products '*' is required: by default vswhere only considers full VS
# editions (Community/Professional/Enterprise) and silently excludes
# Build Tools-only installs.
_vs_install=$("$_vswhere" -latest -products '*' -property installationPath 2>/dev/null | tr -d '\r')

if [ -z "$_vs_install" ]; then
    echo "WARNING: VS not found via vswhere — MSVC environment not loaded." >&2
    unset _vswhere _vs_install
    return 0
fi

# Convert to POSIX path for bash directory operations.
_vs_posix=$(cygpath -u "$_vs_install")

# Find the latest installed MSVC toolset version.
_msvc_ver=$(ls "${_vs_posix}/VC/Tools/MSVC/" 2>/dev/null | sort -V | tail -1 | tr -d '\r')
_msvc="${_vs_posix}/VC/Tools/MSVC/${_msvc_ver}"

# Find the latest installed Windows SDK version.
_sdk_root="/c/Program Files (x86)/Windows Kits/10"
_sdk_ver=$(ls "${_sdk_root}/Include/" 2>/dev/null | sort -V | tail -1 | tr -d '\r')

# Add compiler and SDK tools to PATH.
export PATH="${_msvc}/bin/HostX64/x64:${_sdk_root}/bin/${_sdk_ver}/x64:${PATH}"

# Set INCLUDE and LIB so cl.exe and link.exe find system headers and libraries.
export INCLUDE="$(cygpath -w "${_msvc}/include");$(cygpath -w "${_sdk_root}/Include/${_sdk_ver}/ucrt");$(cygpath -w "${_sdk_root}/Include/${_sdk_ver}/um");$(cygpath -w "${_sdk_root}/Include/${_sdk_ver}/shared")"
export LIB="$(cygpath -w "${_msvc}/lib/x64");$(cygpath -w "${_sdk_root}/Lib/${_sdk_ver}/ucrt/x64");$(cygpath -w "${_sdk_root}/Lib/${_sdk_ver}/um/x64")"

unset _vswhere _vs_install _vs_posix _msvc_ver _msvc _sdk_root _sdk_ver

command -v cl.exe >/dev/null && echo "MSVC ready (cl.exe on PATH)"
